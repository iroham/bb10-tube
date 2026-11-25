/* server.c: the core's local HTTP side. Listens on 127.0.0.1 only (never on the network); every path starts with a
 * per-run token. The QML UI uses it with XMLHttpRequest, BB10's media player fetches HLS from it.
 *   GET  /<t>/status                         account + bootstrap state
 *   GET  /<t>/auth/start | poll | signout    device-code sign-in
 *   POST /<t>/api/<endpoint>                 YouTube API call (JSON body; the client context is added here)
 *   GET  /<t>/play?v=<id>&q=<height>&t=<s>   start playback -> JSON with the HLS address
 *   GET  /<t>/hls/<session>/v.m3u8 | <n>.ts | state
 *   POST /<t>/dl/start?v=&q= (body: details), GET /dl/list /dl/pause?v= /dl/remove?v=, GET /dlf/<id>/v.m3u8 | <n>.ts
 *   GET  /<t>/stop                           end playback sessions
 *   GET  /<t>/img?u=<url>                    thumbnail into the cache; answers with the local file path
 *   GET|POST /<t>/store/<name>               small JSON file for the UI (settings)
 *   GET  /<t>/get?u=<url>[&auth=1]           plain GET of a Google/YouTube address (suggestions, captions, pings) */
#include "core.h"
#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

static char g_token[80];
static int g_port;

int spawn_thread(void *(*fn)(void *), void *arg) {
    pthread_t t; pthread_attr_t at;
    pthread_attr_init(&at);
    pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
    pthread_attr_setstacksize(&at, 512 * 1024);
    int r = pthread_create(&t, &at, fn, arg);
    pthread_attr_destroy(&at);
    return r;
}

static int send_all(int fd, const void *p, size_t n) {
    const char *b = p;
    while (n) {
        ssize_t w = send(fd, b, n, 0);
        if (w <= 0) { if (w < 0 && errno == EINTR) continue; return -1; }
        b += w; n -= (size_t)w;
    }
    return 0;
}
static int respond(int fd, int code, const char *ctype, const void *body, size_t len, int head) {
    char h[400];
    const char *text = code == 200 ? "OK" : code == 206 ? "Partial Content" : code == 404 ? "Not Found" : code == 403 ? "Forbidden" : code == 400 ? "Bad Request" : "Error";
    int n = snprintf(h, sizeof h, "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %lu\r\nCache-Control: no-store\r\nConnection: keep-alive\r\n\r\n",
                     code, text, ctype ? ctype : "text/plain", (unsigned long)len);
    if (send_all(fd, h, (size_t)n)) return -1;
    return head || !len ? 0 : send_all(fd, body, len);
}
static int qparam(const char *query, const char *name, char *out, size_t outsz) {
    size_t nl = strlen(name); const char *p = query;
    while (p && *p) {
        if (!strncmp(p, name, nl) && p[nl] == '=') {
            size_t l = strcspn(p + nl + 1, "&");
            if (l >= outsz) return -1;
            memcpy(out, p + nl + 1, l); out[l] = 0; url_decode(out);
            return 0;
        }
        p = strchr(p, '&'); if (p) p++;
    }
    return -1;
}
/* only Google's video/image hosts may be fetched through the core */
static int host_ok(const char *url, int images) {
    static const char *const any[] = { "youtube.com", "googleapis.com", "google.com", "googlevideo.com", "ytimg.com", "ggpht.com", "googleusercontent.com", NULL };
    static const char *const img[] = { "ytimg.com", "ggpht.com", "googleusercontent.com", NULL };
    if (strncmp(url, "https://", 8)) return 0;
    const char *h = url + 8; size_t hl = strcspn(h, "/?#:"); int i;
    const char *const *list = images ? img : any;
    if (memchr(h, '@', hl)) return 0;
    for (i = 0; list[i]; i++) {
        size_t l = strlen(list[i]);
        if (hl == l && !strncasecmp(h, list[i], l)) return 1;
        if (hl > l && h[hl - l - 1] == '.' && !strncasecmp(h + hl - l, list[i], l)) return 1;
    }
    return 0;
}
static void img_path(const char *url, char *out, size_t n) {
    uint64_t h = 1469598103934665603ULL; const unsigned char *p = (const unsigned char *)url;
    for (; *p; p++) { h ^= *p; h *= 1099511628211ULL; }
    snprintf(out, n, "%s/cache/img/%016llx.jpg", yt_data_dir(), (unsigned long long)h);
}
/* keep the thumbnail cache small: at start, drop files not used for 3 days */
static void img_sweep(void) {
    char dir[600], p[900]; struct stat st; time_t now = time(NULL);
    snprintf(dir, sizeof dir, "%s/cache/img", yt_data_dir());
    DIR *d = opendir(dir); struct dirent *e;
    while (d && (e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
        if (!stat(p, &st) && now - st.st_mtime > 3 * 86400) unlink(p);
    }
    if (d) closedir(d);
}

static void route(int fd, const char *method, char *path, const char *body, size_t blen, int head) {
    char *query = strchr(path, '?');
    if (query) *query++ = 0; else query = "";
    Buf out = {0};
    if (!strcmp(path, "/status")) { yt_status_json(&out); respond(fd, 200, "application/json", out.p, out.n, head); }
    else if (!strcmp(path, "/auth/start")) { yt_auth_start(&out); respond(fd, 200, "application/json", out.p, out.n, head); }
    else if (!strcmp(path, "/auth/poll")) { yt_auth_poll(&out); respond(fd, 200, "application/json", out.p, out.n, head); }
    else if (!strcmp(path, "/auth/signout")) { play_stop_all(); yt_auth_signout(); respond(fd, 200, "application/json", "{}", 2, head); }
    else if (!strncmp(path, "/api/", 5) && !strcmp(method, "POST")) {
        long st = yt_api(path + 5, body && *body ? body : "{}", &out);
        respond(fd, st == 200 ? 200 : st ? (int)st : 502, "application/json", out.p, out.n, head);
    } else if (!strcmp(path, "/play")) {
        char v[32] = "", q[12] = "", t[12] = "", base[160];
        qparam(query, "v", v, sizeof v); qparam(query, "q", q, sizeof q); qparam(query, "t", t, sizeof t);
        snprintf(base, sizeof base, "http://127.0.0.1:%d/%s", g_port, g_token);
        if (!*v) respond(fd, 400, NULL, "", 0, head);
        else { play_start(v, atoi(q) > 0 ? atoi(q) : 720, atoi(t), base, &out); respond(fd, 200, "application/json", out.p, out.n, head); }
    } else if (!strncmp(path, "/hls/", 5)) {
        int id = atoi(path + 5); const char *name = strchr(path + 5, '/'), *ctype = "text/plain";
        int code = name ? play_serve(id, name + 1, &out, &ctype) : 404;
        respond(fd, code, ctype, out.p, code == 200 ? out.n : 0, head);
    } else if (!strcmp(path, "/dl/list")) { dl_list(&out); respond(fd, 200, "application/json", out.p, out.n, head); }
    else if (!strcmp(path, "/dl/start") || !strcmp(path, "/dl/remove") || !strcmp(path, "/dl/pause")) {
        char v[32] = "", q[12] = "";
        qparam(query, "v", v, sizeof v); qparam(query, "q", q, sizeof q);
        if (!*v) respond(fd, 400, NULL, "", 0, head);
        else {
            if (path[4] == 's') dl_start(v, atoi(q) > 0 ? atoi(q) : 720, body && *body ? body : "{}", &out);
            else if (path[4] == 'r') { dl_remove(v); buf_str(&out, "{}"); }
            else { dl_pause(v); buf_str(&out, "{}"); }
            respond(fd, 200, "application/json", out.p, out.n, head);
        }
    } else if (!strncmp(path, "/dlf/", 5)) {
        char id[32]; const char *name = strchr(path + 5, '/'), *ctype = "text/plain";
        size_t il = name ? (size_t)(name - path - 5) : 0;
        int code = 404;
        if (name && il < sizeof id) { memcpy(id, path + 5, il); id[il] = 0; code = dl_file(id, name + 1, &out, &ctype); }
        respond(fd, code, ctype, out.p, code == 200 ? out.n : 0, head);
    } else if (!strcmp(path, "/stop")) { play_stop_all(); respond(fd, 200, "application/json", "{}", 2, head); }
    else if (!strcmp(path, "/img")) {
        char *u = malloc(strlen(query) + 1), file[700];
        if (qparam(query, "u", u, strlen(query) + 1) || !host_ok(u, 1)) respond(fd, 400, NULL, "", 0, head);
        else {
            struct stat st;
            img_path(u, file, sizeof file);
            if (stat(file, &st) || st.st_size == 0) {
                static const char *const h[] = { "User-Agent: " UA_TV, NULL };
                NetRes r = {0};
                if (!net_req(u, h, NULL, 0, 25, NULL, NULL, &r) && r.status == 200 && r.body.n > 0 && r.body.n < (4u << 20)) write_file(file, r.body.p, r.body.n, 0600);
                else file[0] = 0;
                buf_free(&r.body);
            }
            respond(fd, file[0] ? 200 : 404, "text/plain", file, strlen(file), head);
        }
        free(u);
    } else if (!strcmp(path, "/get")) {
        char *u = malloc(strlen(query) + 1), a[4] = "";
        qparam(query, "auth", a, sizeof a);
        if (qparam(query, "u", u, strlen(query) + 1) || !host_ok(u, 0)) respond(fd, 400, NULL, "", 0, head);
        else { long st = yt_get(u, a[0] == '1', &out); respond(fd, st ? (int)st : 502, "text/plain; charset=utf-8", out.p, out.n, head); }
        free(u);
    } else if (!strncmp(path, "/store/", 7)) {
        /* small JSON files for the UI (settings, resume positions): GET reads, POST writes. Names: letters only. */
        const char *k = path + 7; size_t i, kl = strlen(k); char file[700];
        for (i = 0; i < kl; i++) if (k[i] < 'a' || k[i] > 'z') break;
        if (!kl || i < kl || kl > 24) respond(fd, 400, NULL, "", 0, head);
        else {
            snprintf(file, sizeof file, "%s/%s.json", yt_data_dir(), k);
            if (!strcmp(method, "POST")) { write_file(file, body, blen, 0600); respond(fd, 200, "application/json", "{}", 2, head); }
            else { size_t l = 0; char *t = read_file(file, &l); respond(fd, 200, "application/json", t ? t : "{}", t ? l : 2, head); free(t); }
        }
    } else respond(fd, 404, NULL, "", 0, head);
    buf_free(&out);
}

static void *serve(void *arg) {
    int fd = (int)(intptr_t)arg;
    Buf in = {0};
    for (;;) {                                         /* keep-alive: one request after another */
        char *end = NULL;
        for (;;) {
            buf_zero(&in);
            if (in.n && (end = strstr((char *)in.p, "\r\n\r\n"))) break;
            if (in.n > 65536) goto done;
            char tmp[8192];
            ssize_t r = recv(fd, tmp, sizeof tmp, 0);
            if (r <= 0) goto done;
            buf_put(&in, tmp, (size_t)r);
        }
        size_t hlen = (size_t)(end - (char *)in.p) + 4, clen = 0;
        *end = 0;
        char *cl = strcasestr((char *)in.p, "\nContent-Length:");
        if (cl) clen = (size_t)strtoul(cl + 16, NULL, 10);
        if (clen > (1u << 20)) goto done;
        while (in.n < hlen + clen) {
            char tmp[8192];
            ssize_t r = recv(fd, tmp, sizeof tmp, 0);
            if (r <= 0) goto done;
            buf_put(&in, tmp, (size_t)r);
        }
        char method[8] = "", path[4096] = "";
        sscanf((char *)in.p, "%7s %4095s", method, path);
        size_t tl = strlen(g_token);
        int head = !strcmp(method, "HEAD");
        if (path[0] != '/' || strncmp(path + 1, g_token, tl) || path[1 + tl] != '/' || strstr(path, "..")) { respond(fd, 403, NULL, "", 0, head); goto done; }
        char *body = malloc(clen + 1);
        memcpy(body, in.p + hlen, clen); body[clen] = 0;
        route(fd, method, path + 1 + tl, body, clen, head);
        free(body);
        memmove(in.p, in.p + hlen + clen, in.n - hlen - clen);
        in.n -= hlen + clen;
    }
done:
    buf_free(&in);
    close(fd);
    return NULL;
}

static void *accept_loop(void *arg) {
    int s = (int)(intptr_t)arg;
    for (;;) {
        int c = accept(s, NULL, NULL);
        if (c < 0) { if (errno == EINTR) continue; usleep(100000); continue; }
        if (spawn_thread(serve, (void *)(intptr_t)c)) close(c);
    }
    return NULL;
}

int server_start(const char *token) {
    snprintf(g_token, sizeof g_token, "%s", token);
    signal(SIGPIPE, SIG_IGN);
    img_sweep();
    int s = socket(AF_INET, SOCK_STREAM, 0), one = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in a; socklen_t al = sizeof a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);                 /* loopback only; port chosen by the system */
    if (bind(s, (struct sockaddr *)&a, sizeof a) || listen(s, 16) || getsockname(s, (struct sockaddr *)&a, &al)) { close(s); return 0; }
    g_port = ntohs(a.sin_port);
    if (spawn_thread(accept_loop, (void *)(intptr_t)s)) { close(s); return 0; }
    return g_port;
}
