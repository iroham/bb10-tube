/* net.c: HTTPS for the core, on the phone's own libcurl + OpenSSL (checked: reaches Google's servers with
 * certificate verification). One curl handle per thread (keep-alive), DNS and TLS sessions shared between threads. */
#include "core.h"
#include <curl/curl.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

static pthread_key_t g_key;
static CURLSH *g_share;
static pthread_mutex_t g_share_mu[CURL_LOCK_DATA_LAST];

#ifdef __QNX__
/* OpenSSL 1.0.x needs the application to provide its locks when several threads use it */
#include <openssl/crypto.h>
static pthread_mutex_t *g_ssl_mu;
static void ssl_lock_cb(int mode, int n, const char *file, int line) {
    (void)file; (void)line;
    if (mode & CRYPTO_LOCK) pthread_mutex_lock(&g_ssl_mu[n]); else pthread_mutex_unlock(&g_ssl_mu[n]);
}
static unsigned long ssl_id_cb(void) { return (unsigned long)pthread_self(); }
static void ssl_threads(void) {
    if (CRYPTO_get_locking_callback()) return;            /* someone (Qt) already did */
    int n = CRYPTO_num_locks(), i;
    g_ssl_mu = malloc(sizeof *g_ssl_mu * (size_t)n);
    for (i = 0; i < n; i++) pthread_mutex_init(&g_ssl_mu[i], NULL);
    CRYPTO_set_id_callback(ssl_id_cb);
    CRYPTO_set_locking_callback(ssl_lock_cb);
}
#else
static void ssl_threads(void) {}
#endif

static void share_lock(CURL *h, curl_lock_data d, curl_lock_access a, void *u) { (void)h; (void)a; (void)u; pthread_mutex_lock(&g_share_mu[d]); }
static void share_unlock(CURL *h, curl_lock_data d, void *u) { (void)h; (void)u; pthread_mutex_unlock(&g_share_mu[d]); }
static void handle_free(void *h) { if (h) curl_easy_cleanup(h); }

void net_init(void) {
    int i;
    curl_global_init(CURL_GLOBAL_ALL);
    ssl_threads();
    for (i = 0; i < CURL_LOCK_DATA_LAST; i++) pthread_mutex_init(&g_share_mu[i], NULL);
    g_share = curl_share_init();
    curl_share_setopt(g_share, CURLSHOPT_LOCKFUNC, share_lock);
    curl_share_setopt(g_share, CURLSHOPT_UNLOCKFUNC, share_unlock);
    curl_share_setopt(g_share, CURLSHOPT_SHARE, CURL_LOCK_DATA_SSL_SESSION);
    pthread_key_create(&g_key, handle_free);
}

typedef struct { NetSink sink; void *ctx; NetRes *res; } Xfer;
static size_t on_data(char *p, size_t s, size_t n, void *u) {
    Xfer *x = u; size_t len = s * n;
    if (x->sink) return x->sink(x->ctx, (const uint8_t *)p, len) == len ? len : 0;
    if (x->res->body.n + len > (64u << 20)) return 0;
    buf_put(&x->res->body, p, len);
    return len;
}

int net_req(const char *url, const char *const *hdrs, const void *post, size_t postlen, int timeout_s, NetSink sink, void *ctx, NetRes *res) {
    CURL *c = pthread_getspecific(g_key);
    if (!c) { c = curl_easy_init(); pthread_setspecific(g_key, c); }
    else curl_easy_reset(c);
    if (!c) return -1;
    Xfer x = { sink, ctx, res };
    struct curl_slist *hl = NULL;
    res->status = 0; res->body.n = 0; res->err[0] = 0;
    for (; hdrs && *hdrs; hdrs++) hl = curl_slist_append(hl, *hdrs);
    hl = curl_slist_append(hl, "Expect:");
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_SHARE, g_share);
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, hl);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, on_data);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &x);
    curl_easy_setopt(c, CURLOPT_ERRORBUFFER, res->err);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 25L);
    if (timeout_s) curl_easy_setopt(c, CURLOPT_TIMEOUT, (long)timeout_s);
    if (!sink) curl_easy_setopt(c, CURLOPT_ACCEPT_ENCODING, "");     /* gzip for text; media stays as is */
    if (post) {
        curl_easy_setopt(c, CURLOPT_POST, 1L);
        curl_easy_setopt(c, CURLOPT_POSTFIELDS, post);
        curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, (long)postlen);
    }
    CURLcode r = curl_easy_perform(c);
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &res->status);
    curl_slist_free_all(hl);
    if (r != CURLE_OK && !res->err[0]) strncpy(res->err, curl_easy_strerror(r), sizeof res->err - 1);
    buf_zero(&res->body);
    return r == CURLE_OK ? 0 : -1;
}
