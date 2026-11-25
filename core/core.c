/* core.c: entry points. In the app the core runs inside the launcher process (tube_core_start); built with
 * -DCORE_MAIN it is a standalone program for testing on the Mac:
 *   tubed <data dir> <js dir> [token]     prints "http://127.0.0.1:<port>/<token>" and serves until killed. */
#include "core.h"
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* Starts everything; writes the base address for the UI into url. Returns 0 on success. */
int tube_core_start(const char *data_dir, const char *js_dir, char *url, size_t urlsz) {
    char token[40]; unsigned char rnd[16] = {0}; int i;
    int f = open("/dev/urandom", O_RDONLY);
    if (f < 0 || read(f, rnd, sizeof rnd) != (ssize_t)sizeof rnd) { if (f >= 0) close(f); return -1; }
    close(f);
    for (i = 0; i < 16; i++) snprintf(token + 2 * i, 3, "%02x", rnd[i]);
    net_init();
    yt_init(data_dir, js_dir);
    dl_init();
    int port = server_start(token);
    if (!port) return -1;
    snprintf(url, urlsz, "http://127.0.0.1:%d/%s", port, token);
    return 0;
}

#ifdef CORE_MAIN
int main(int argc, char **argv) {
    char url[200];
    if (argc < 3) { fprintf(stderr, "usage: tubed <data dir> <js dir>\n"); return 2; }
    if (tube_core_start(argv[1], argv[2], url, sizeof url)) { fprintf(stderr, "tubed: cannot start\n"); return 1; }
    printf("%s\n", url); fflush(stdout);
    for (;;) pause();
}
#endif
