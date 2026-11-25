/* httpd: the local HTTP side of the native app's core. BB10's media player takes HLS only over HTTP, and the QML
 * UI talks to the core with XMLHttpRequest, so the core listens on 127.0.0.1 only (never on the network) and every
 * path starts with a per-session token: /<token>/...
 * This first version serves files below a root directory (GET/HEAD, Range), enough to test the hardware player.
 *   httpd <port> <token> <root dir>
 * One thread per connection; connections are few (the player, the UI). */
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *g_token, *g_root;

static const char *mime(const char *p) {
    const char *e = strrchr(p, '.');
    if (!e) return "application/octet-stream";
    if (!strcmp(e, ".m3u8")) return "application/vnd.apple.mpegurl";
    if (!strcmp(e, ".ts")) return "video/mp2t";
    if (!strcmp(e, ".mp4")) return "video/mp4";
    if (!strcmp(e, ".json")) return "application/json";
    return "application/octet-stream";
}

static int send_all(int fd, const char *b, size_t n) {
    while (n) {
        ssize_t w = send(fd, b, n, 0);
        if (w <= 0) { if (w < 0 && errno == EINTR) continue; return -1; }
        b += w; n -= (size_t)w;
    }
    return 0;
}

static void reply(int fd, int code, const char *text) {
    char h[256];
    int n = snprintf(h, sizeof h, "HTTP/1.1 %d %s\r\nContent-Length: 0\r\nConnection: close\r\n\r\n", code, text);
    send_all(fd, h, (size_t)n);
}

static void *serve(void *arg) {
    int fd = (int)(intptr_t)arg;
    char req[4096];
    size_t have = 0;
    for (;;) {                                    /* keep-alive: one request after another */
        char *end;
        while (req[have] = 0, !(end = strstr(req, "\r\n\r\n"))) {   /* QNX has no memmem; a request is text */
            if (have == sizeof req - 1) goto done;
            ssize_t r = recv(fd, req + have, sizeof req - 1 - have, 0);
            if (r <= 0) goto done;
            have += (size_t)r;
        }
        *end = 0;
        size_t used = (size_t)(end - req) + 4;
        char method[8] = "", path[1024] = "";
        sscanf(req, "%7s %1023s", method, path);
        int head = !strcmp(method, "HEAD");
        size_t tl = strlen(g_token);
        char *q = strchr(path, '?');
        if (q) *q = 0;
        if ((strcmp(method, "GET") && !head) || path[0] != '/' || strncmp(path + 1, g_token, tl) || path[1 + tl] != '/' || strstr(path, "..")) {
            reply(fd, 403, "Forbidden");
            goto done;
        }
        char file[2048];
        snprintf(file, sizeof file, "%s%s", g_root, path + 1 + tl);
        struct stat st;
        int f = open(file, O_RDONLY);
        if (f < 0 || fstat(f, &st) || !S_ISREG(st.st_mode)) { if (f >= 0) close(f); reply(fd, 404, "Not Found"); goto next; }
        long long from = 0, to = (long long)st.st_size - 1;
        int partial = 0;
        char *rg = strcasestr(req, "\nRange: bytes=");
        if (rg) {
            rg += 14;
            long long a = -1, b = -1;
            if (*rg == '-') { b = atoll(rg + 1); from = st.st_size - b; if (from < 0) from = 0; }
            else { a = atoll(rg); from = a; char *d = strchr(rg, '-'); if (d && d[1] >= '0' && d[1] <= '9') to = atoll(d + 1); }
            if (to > (long long)st.st_size - 1) to = (long long)st.st_size - 1;
            if (from > to) { close(f); reply(fd, 416, "Range Not Satisfiable"); goto next; }
            partial = 1;
        }
        char h[512];
        int n = snprintf(h, sizeof h, "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %lld\r\nAccept-Ranges: bytes\r\n", partial ? "206 Partial Content" : "200 OK", mime(file), to - from + 1);
        if (partial) n += snprintf(h + n, sizeof h - (size_t)n, "Content-Range: bytes %lld-%lld/%lld\r\n", from, to, (long long)st.st_size);
        n += snprintf(h + n, sizeof h - (size_t)n, "Cache-Control: no-store\r\n\r\n");
        if (send_all(fd, h, (size_t)n)) { close(f); goto done; }
        if (!head) {
            static __thread char buf[65536];
            lseek(f, (off_t)from, SEEK_SET);
            long long left = to - from + 1;
            while (left > 0) {
                ssize_t r = read(f, buf, left > (long long)sizeof buf ? sizeof buf : (size_t)left);
                if (r <= 0 || send_all(fd, buf, (size_t)r)) { close(f); goto done; }
                left -= r;
            }
        }
        close(f);
    next:
        memmove(req, req + used, have - used);
        have -= used;
    }
done:
    close(fd);
    return NULL;
}

int main(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "usage: httpd <port> <token> <root dir>\n"); return 2; }
    g_token = argv[2];
    g_root = argv[3];
    signal(SIGPIPE, SIG_IGN);
    int s = socket(AF_INET, SOCK_STREAM, 0), one = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons((unsigned short)atoi(argv[1]));
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);                 /* loopback only */
    if (bind(s, (struct sockaddr *)&a, sizeof a) || listen(s, 8)) { perror("httpd: bind/listen"); return 1; }
    for (;;) {
        int c = accept(s, NULL, NULL);
        if (c < 0) continue;
        pthread_t t;
        pthread_attr_t at;
        pthread_attr_init(&at);
        pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
        if (pthread_create(&t, &at, serve, (void *)(intptr_t)c)) close(c);
        pthread_attr_destroy(&at);
    }
}
