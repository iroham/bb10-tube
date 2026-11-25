/* yt.c: the account (device-code sign-in, token refresh), the bootstrap (current TV client version, visitor id,
 * player script -> signature timestamp + cached solver input) and the API call wrapper.
 * Tokens live in <data>/auth.json (mode 600) and are never logged. */
#include "core.h"
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define CLIENT_ID "861556708454-d6dlm3lh05idd8npek18k6be8ba3oc68.apps.googleusercontent.com"   /* YouTube on TV's public client */
#define CLIENT_SECRET "SboVhoG9s0rNafixCSGGKXAT"
#define SCOPE "http://gdata.youtube.com https://www.googleapis.com/auth/youtube-paid-content"
#define BOOT_MAX_AGE_MS (6 * 3600 * 1000LL)

extern char **environ;

static struct {
    pthread_mutex_t mu;                       /* guards everything below */
    pthread_cond_t cv;
    char data[512], js[512];
    /* account */
    char *access, *refresh; int64_t expires_ms;
    char *device_code; int64_t device_until;
    /* bootstrap */
    char version[40], visitor[256], player_url[160], player_id[24];
    int sts; int64_t boot_at;
    int boot_ok, player_ok, booting, kick;
    char msg[160];
    /* solved n values */
    struct { char n[64], out[64]; } nc[16]; int nci;
} Y = { .mu = PTHREAD_MUTEX_INITIALIZER, .cv = PTHREAD_COND_INITIALIZER };
static pthread_mutex_t g_auth_mu = PTHREAD_MUTEX_INITIALIZER;    /* one token refresh at a time */
static pthread_mutex_t g_js_mu = PTHREAD_MUTEX_INITIALIZER;      /* one jsrun at a time */

const char *yt_data_dir(void) { return Y.data; }
int yt_signed_in(void) { pthread_mutex_lock(&Y.mu); int r = Y.refresh != NULL; pthread_mutex_unlock(&Y.mu); return r; }
int yt_sts(void) { pthread_mutex_lock(&Y.mu); int r = Y.sts; pthread_mutex_unlock(&Y.mu); return r; }
void yt_client_version(char *out, size_t n) { pthread_mutex_lock(&Y.mu); snprintf(out, n, "%s", Y.version); pthread_mutex_unlock(&Y.mu); }
static void set_msg(const char *m) { pthread_mutex_lock(&Y.mu); snprintf(Y.msg, sizeof Y.msg, "%s", m); pthread_mutex_unlock(&Y.mu); LOG("yt: %s", m); }

/* ---------------- account ---------------- */
static void auth_save(void) {      /* Y.mu held */
    char path[600]; Buf b = {0};
    snprintf(path, sizeof path, "%s/auth.json", Y.data);
    if (!Y.refresh) { unlink(path); return; }
    buf_str(&b, "{\"access_token\":"); json_escape(&b, Y.access ? Y.access : "");
    buf_str(&b, ",\"refresh_token\":"); json_escape(&b, Y.refresh);
    buf_fmt(&b, ",\"expires_at\":%lld}", (long long)Y.expires_ms);
    write_file(path, b.p, b.n, 0600);
    buf_free(&b);
}
static void auth_load(void) {
    char path[600];
    snprintf(path, sizeof path, "%s/auth.json", Y.data);
    char *j = read_file(path, NULL);
    if (!j) return;
    Y.access = json_str(j, "access_token"); Y.refresh = json_str(j, "refresh_token"); Y.expires_ms = json_num(j, "expires_at", 0);
    if (Y.refresh && !*Y.refresh) { free(Y.refresh); Y.refresh = NULL; }
    free(j);
}
static long oauth_post(const char *url, const char *json, Buf *out) {
    static const char *const h[] = { "Content-Type: application/json", "User-Agent: Mozilla/5.0 (ChromiumStylePlatform) Cobalt/Version", NULL };
    NetRes r = {0};
    net_req(url, h, json, strlen(json), 30, NULL, NULL, &r);
    out->n = 0; buf_put(out, r.body.p, r.body.n); buf_zero(out);
    buf_free(&r.body);
    return r.status;
}
int yt_auth_start(Buf *out) {
    Buf req = {0}, res = {0}; unsigned char rnd[16] = {0}; int i;
    int f = open("/dev/urandom", O_RDONLY);
    if (f >= 0) { if (read(f, rnd, sizeof rnd) < 0) {} close(f); }
    buf_str(&req, "{\"client_id\":\"" CLIENT_ID "\",\"scope\":\"" SCOPE "\",\"device_id\":\"");
    for (i = 0; i < 16; i++) buf_fmt(&req, "%02x", rnd[i]);
    buf_str(&req, "\",\"device_model\":\"ytlr::\"}"); buf_zero(&req);
    oauth_post("https://www.youtube.com/o/oauth2/device/code", (char *)req.p, &res);
    char *dc = res.p ? json_str((char *)res.p, "device_code") : NULL, *uc = res.p ? json_str((char *)res.p, "user_code") : NULL;
    char *vu = res.p ? json_str((char *)res.p, "verification_url") : NULL;
    if (dc && uc) {
        long long exp = json_num((char *)res.p, "expires_in", 1800);
        pthread_mutex_lock(&Y.mu);
        free(Y.device_code); Y.device_code = dc; dc = NULL; Y.device_until = now_ms() + exp * 1000;
        pthread_mutex_unlock(&Y.mu);
        buf_str(out, "{\"user_code\":"); json_escape(out, uc);
        buf_str(out, ",\"verification_url\":"); json_escape(out, vu ? vu : "https://www.google.com/device");
        buf_fmt(out, ",\"interval\":%lld,\"expires_in\":%lld}", json_num((char *)res.p, "interval", 5), exp);
    } else buf_str(out, "{\"error\":\"The sign-in service did not answer. Check the connection and try again.\"}");
    free(dc); free(uc); free(vu); buf_free(&req); buf_free(&res);
    return 0;
}
static void take_tokens(const char *json) {
    char *a = json_str(json, "access_token"), *r = json_str(json, "refresh_token");
    long long exp = json_num(json, "expires_in", 3600);
    pthread_mutex_lock(&Y.mu);
    if (a) { free(Y.access); Y.access = a; Y.expires_ms = now_ms() + exp * 1000; }
    if (r) { free(Y.refresh); Y.refresh = r; }
    auth_save();
    pthread_mutex_unlock(&Y.mu);
}
int yt_auth_poll(Buf *out) {
    Buf req = {0}, res = {0};
    pthread_mutex_lock(&Y.mu);
    if (!Y.device_code || now_ms() > Y.device_until) { pthread_mutex_unlock(&Y.mu); buf_str(out, "{\"state\":\"error\",\"error\":\"The code expired. Start again.\"}"); return 0; }
    buf_str(&req, "{\"client_id\":\"" CLIENT_ID "\",\"client_secret\":\"" CLIENT_SECRET "\",\"code\":"); json_escape(&req, Y.device_code);
    pthread_mutex_unlock(&Y.mu);
    buf_str(&req, ",\"grant_type\":\"http://oauth.net/grant_type/device/1.0\"}"); buf_zero(&req);
    oauth_post("https://www.youtube.com/o/oauth2/token", (char *)req.p, &res);
    char *err = res.p ? json_str((char *)res.p, "error") : NULL;
    if (res.p && json_get((char *)res.p, "access_token", NULL)) {
        take_tokens((char *)res.p);
        pthread_mutex_lock(&Y.mu); free(Y.device_code); Y.device_code = NULL; pthread_mutex_unlock(&Y.mu);
        buf_str(out, "{\"state\":\"ok\"}");
        LOG("yt: signed in");
    } else if (!res.p || !res.n || (err && (!strcmp(err, "authorization_pending") || !strcmp(err, "slow_down")))) buf_str(out, "{\"state\":\"pending\"}");
    else { buf_str(out, "{\"state\":\"error\",\"error\":"); json_escape(out, err ? err : "sign-in failed"); buf_str(out, "}"); }
    free(err); buf_free(&req); buf_free(&res);
    return 0;
}
void yt_auth_signout(void) {
    pthread_mutex_lock(&Y.mu);
    free(Y.access); free(Y.refresh); Y.access = Y.refresh = NULL; Y.expires_ms = 0;
    auth_save();
    pthread_mutex_unlock(&Y.mu);
    LOG("yt: signed out");
}
/* a valid access token (malloc'd) or NULL when signed out / refresh failed */
static char *token(int force) {
    char *out = NULL;
    pthread_mutex_lock(&g_auth_mu);
    pthread_mutex_lock(&Y.mu);
    if (!Y.refresh) { pthread_mutex_unlock(&Y.mu); pthread_mutex_unlock(&g_auth_mu); return NULL; }
    if (!force && Y.access && now_ms() < Y.expires_ms - 120000) out = strdup(Y.access);
    Buf req = {0}, res = {0};
    if (!out) { buf_str(&req, "{\"client_id\":\"" CLIENT_ID "\",\"client_secret\":\"" CLIENT_SECRET "\",\"refresh_token\":"); json_escape(&req, Y.refresh); }
    pthread_mutex_unlock(&Y.mu);
    if (!out) {
        buf_str(&req, ",\"grant_type\":\"refresh_token\"}"); buf_zero(&req);
        long st = oauth_post("https://www.youtube.com/o/oauth2/token", (char *)req.p, &res);
        if (res.p && json_get((char *)res.p, "access_token", NULL)) {
            take_tokens((char *)res.p);
            pthread_mutex_lock(&Y.mu); out = Y.access ? strdup(Y.access) : NULL; pthread_mutex_unlock(&Y.mu);
            LOG("yt: access token refreshed");
        } else {
            char *err = res.p ? json_str((char *)res.p, "error") : NULL;
            LOG("yt: token refresh failed (http %ld, %s)", st, err ? err : "no answer");
            if (err && !strcmp(err, "invalid_grant")) yt_auth_signout();      /* access was withdrawn */
            free(err);
        }
    }
    buf_free(&req); buf_free(&res);
    pthread_mutex_unlock(&g_auth_mu);
    return out;
}

/* ---------------- bootstrap ---------------- */
static void boot_save(void) {     /* Y.mu held */
    char path[600]; Buf b = {0};
    snprintf(path, sizeof path, "%s/boot.json", Y.data);
    buf_str(&b, "{\"clientVersion\":"); json_escape(&b, Y.version);
    buf_str(&b, ",\"visitorData\":"); json_escape(&b, Y.visitor);
    buf_str(&b, ",\"playerUrl\":"); json_escape(&b, Y.player_url);
    buf_str(&b, ",\"playerId\":"); json_escape(&b, Y.player_id);
    buf_fmt(&b, ",\"sts\":%d,\"at\":%lld}", Y.sts, (long long)Y.boot_at);
    write_file(path, b.p, b.n, 0600);
    buf_free(&b);
}
static void pre_path(char *out, size_t n, const char *id) { snprintf(out, n, "%s/cache/pre-%s.js", Y.data, id); }
static void boot_load(void) {
    char path[600];
    snprintf(path, sizeof path, "%s/boot.json", Y.data);
    char *j = read_file(path, NULL);
    if (!j) return;
    char *v = json_str(j, "clientVersion"), *vd = json_str(j, "visitorData"), *pu = json_str(j, "playerUrl"), *pi = json_str(j, "playerId");
    if (v && vd && pu && pi && *v && *vd) {
        snprintf(Y.version, sizeof Y.version, "%s", v); snprintf(Y.visitor, sizeof Y.visitor, "%s", vd);
        snprintf(Y.player_url, sizeof Y.player_url, "%s", pu); snprintf(Y.player_id, sizeof Y.player_id, "%s", pi);
        Y.sts = (int)json_num(j, "sts", 0); Y.boot_at = json_num(j, "at", 0);
        Y.boot_ok = 1;
        pre_path(path, sizeof path, Y.player_id);
        Y.player_ok = Y.sts > 0 && access(path, R_OK) == 0;
    }
    free(v); free(vd); free(pu); free(pi); free(j);
}
/* value of "key":"..." in a page */
static char *page_str(const char *page, const char *key) {
    char pat[80];
    snprintf(pat, sizeof pat, "\"%s\":\"", key);
    const char *p = strstr(page, pat);
    return p ? json_str(p + strlen(pat) - 1, "") : NULL;
}
static int run_js(const char *in_js, const char *out_path) {
    char jsrun[600], lib[600], glue[600], core[600], solve[600];
    snprintf(jsrun, sizeof jsrun, "%s/jsrun", Y.js); snprintf(lib, sizeof lib, "%s/yt.solver.lib.min.js", Y.js);
    snprintf(glue, sizeof glue, "%s/glue.js", Y.js); snprintf(core, sizeof core, "%s/yt.solver.core.js", Y.js);
    snprintf(solve, sizeof solve, "%s/solve.js", Y.js);
    char outarg[700];
    snprintf(outarg, sizeof outarg, "--out=%s", out_path);
    char *argv[] = { jsrun, outarg, (char *)in_js, lib, glue, core, solve, NULL };
    pid_t pid; int st = -1;
    int r = posix_spawn(&pid, jsrun, NULL, NULL, argv, environ);
    if (r) { LOG("yt: cannot start %s (%d)", jsrun, r); return -1; }
    while (waitpid(pid, &st, 0) < 0) {}
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}
static int prepare_player(const char *url, const char *id) {
    char full[400], ppath[600], in[600], out[600], pre[600];
    snprintf(full, sizeof full, "https://www.youtube.com%s", url);
    snprintf(ppath, sizeof ppath, "%s/cache/player.js", Y.data); snprintf(in, sizeof in, "%s/cache/prep-in.js", Y.data);
    snprintf(out, sizeof out, "%s/cache/prep-out.js", Y.data); pre_path(pre, sizeof pre, id);
    static const char *const h[] = { "User-Agent: " UA_TV, NULL };
    NetRes r = {0};
    set_msg("Getting the player (first run after an update takes about a minute)");
    if (net_req(full, h, NULL, 0, 120, NULL, NULL, &r) || r.status != 200 || r.body.n < 100000) { LOG("yt: player download failed: http %ld %s", r.status, r.err); buf_free(&r.body); return -1; }
    const char *s = strstr((char *)r.body.p, "signatureTimestamp:");
    int sts = s ? atoi(s + 19) : 0;
    if (!sts) { LOG("yt: no signature timestamp in the player"); buf_free(&r.body); return -1; }
    write_file(ppath, r.body.p, r.body.n, 0600);
    buf_free(&r.body);
    Buf b = {0};
    buf_str(&b, "globalThis.__in={mode:\"prep\",player:"); json_escape(&b, ppath); buf_str(&b, "};");
    write_file(in, b.p, b.n, 0600); buf_free(&b);
    int64_t t0 = now_ms();
    pthread_mutex_lock(&g_js_mu);
    int rc = run_js(in, out);
    pthread_mutex_unlock(&g_js_mu);
    unlink(ppath); unlink(in);
    struct stat st;
    if (rc || stat(out, &st) || st.st_size < 100000) { LOG("yt: player preparation failed (%d)", rc); unlink(out); return -1; }
    /* drop older versions, then publish the new one */
    char dir[600]; snprintf(dir, sizeof dir, "%s/cache", Y.data);
    DIR *d = opendir(dir); struct dirent *e;
    while (d && (e = readdir(d))) if (!strncmp(e->d_name, "pre-", 4)) { char p[900]; snprintf(p, sizeof p, "%s/%s", dir, e->d_name); unlink(p); }
    if (d) closedir(d);
    rename(out, pre);
    LOG("yt: player %s prepared in %lld ms, sts %d", id, (long long)(now_ms() - t0), sts);
    return sts;
}
static void do_boot(void) {
    static const char *const h[] = { "User-Agent: " UA_TV, "Accept-Language: en-US,en", NULL };
    NetRes r = {0};
    if (net_req("https://www.youtube.com/tv", h, NULL, 0, 40, NULL, NULL, &r) || r.status != 200) {
        LOG("yt: bootstrap page failed: http %ld %s", r.status, r.err);
        set_msg("Cannot reach the service. Check the connection.");
        buf_free(&r.body); return;
    }
    const char *page = (char *)r.body.p;
    char *ver = page_str(page, "INNERTUBE_CONTEXT_CLIENT_VERSION"), *vis = page_str(page, "visitorData");
    if (!ver) ver = page_str(page, "clientVersion");
    char purl[160] = "", pid[24] = "";
    const char *p = strstr(page, "/s/player/");
    if (p) {
        size_t n = strcspn(p, "\"'\\ <>");
        if (n < sizeof purl) { memcpy(purl, p, n); purl[n] = 0; }
        size_t m = strcspn(p + 10, "/");
        if (m < sizeof pid) { memcpy(pid, p + 10, m); pid[m] = 0; }
    }
    if (!ver || !vis || !*purl || !*pid) { LOG("yt: bootstrap page has no %s", !ver ? "client version" : !vis ? "visitor id" : "player"); set_msg("The service page changed; cannot start."); free(ver); free(vis); buf_free(&r.body); return; }
    buf_free(&r.body);
    char pre[600]; pre_path(pre, sizeof pre, pid);
    pthread_mutex_lock(&Y.mu);
    int same_player = !strcmp(pid, Y.player_id) && Y.sts > 0 && access(pre, R_OK) == 0;
    snprintf(Y.version, sizeof Y.version, "%s", ver);
    if (!Y.visitor[0]) snprintf(Y.visitor, sizeof Y.visitor, "%s", vis);     /* keep one visitor id */
    Y.boot_ok = 1; Y.boot_at = now_ms();
    if (same_player) { Y.player_ok = 1; Y.msg[0] = 0; boot_save(); }
    pthread_cond_broadcast(&Y.cv);
    pthread_mutex_unlock(&Y.mu);
    free(ver); free(vis);
    LOG("yt: bootstrap ok, player %s%s", pid, same_player ? " (cached)" : " (new)");
    if (same_player) return;
    int sts = prepare_player(purl, pid);
    pthread_mutex_lock(&Y.mu);
    if (sts > 0) {
        snprintf(Y.player_url, sizeof Y.player_url, "%s", purl); snprintf(Y.player_id, sizeof Y.player_id, "%s", pid);
        Y.sts = sts; Y.player_ok = 1; Y.msg[0] = 0; Y.nci = 0; memset(Y.nc, 0, sizeof Y.nc);
        boot_save();
    } else { snprintf(Y.msg, sizeof Y.msg, "Could not prepare the player."); Y.boot_at = 0; }
    pthread_cond_broadcast(&Y.cv);
    pthread_mutex_unlock(&Y.mu);
}
static void *boot_thread(void *arg) {
    (void)arg;
    for (;;) {
        pthread_mutex_lock(&Y.mu);
        while (!Y.kick) pthread_cond_wait(&Y.cv, &Y.mu);
        Y.kick = 0; Y.booting = 1;
        pthread_mutex_unlock(&Y.mu);
        do_boot();
        pthread_mutex_lock(&Y.mu);
        Y.booting = 0;
        pthread_cond_broadcast(&Y.cv);
        pthread_mutex_unlock(&Y.mu);
    }
    return NULL;
}
/* start a bootstrap when there is none or it is old; force: now (the player request was refused) */
static void boot_check(int force) {
    pthread_mutex_lock(&Y.mu);
    if (!Y.booting && (force || !Y.boot_ok || !Y.player_ok || now_ms() - Y.boot_at > BOOT_MAX_AGE_MS)) {
        static int64_t last;
        if (force || now_ms() - last > 20000) { last = now_ms(); Y.kick = 1; pthread_cond_broadcast(&Y.cv); }
    }
    pthread_mutex_unlock(&Y.mu);
}
static int wait_for(int *flag, int max_s) {
    int64_t until = now_ms() + max_s * 1000LL;
    boot_check(0);
    pthread_mutex_lock(&Y.mu);
    while (!*flag && now_ms() < until) {
        struct timespec ts; int64_t t = now_ms() + 500;
        ts.tv_sec = (time_t)(t / 1000); ts.tv_nsec = (long)(t % 1000) * 1000000L;
        pthread_cond_timedwait(&Y.cv, &Y.mu, &ts);
    }
    int ok = *flag;
    pthread_mutex_unlock(&Y.mu);
    return ok ? 0 : -1;
}
int yt_wait_player(int max_s) { return wait_for(&Y.player_ok, max_s); }

void yt_status_json(Buf *out) {
    boot_check(0);
    pthread_mutex_lock(&Y.mu);
    buf_fmt(out, "{\"signedIn\":%s,\"ready\":%s,\"player\":%s,\"msg\":", Y.refresh ? "true" : "false", Y.boot_ok ? "true" : "false", Y.player_ok ? "true" : "false");
    json_escape(out, Y.msg);
    buf_str(out, "}");
    pthread_mutex_unlock(&Y.mu);
}

void yt_init(const char *data_dir, const char *js_dir) {
    char p[600];
    /* absolute paths: QNX starts a spawned program without regard to our working directory */
    char abs1[PATH_MAX], abs2[PATH_MAX];
    snprintf(Y.data, sizeof Y.data, "%s", realpath(data_dir, abs1) ? abs1 : data_dir);
    snprintf(Y.js, sizeof Y.js, "%s", realpath(js_dir, abs2) ? abs2 : js_dir);
    snprintf(p, sizeof p, "%s/cache", Y.data); mkdir(p, 0700);
    snprintf(p, sizeof p, "%s/cache/img", Y.data); mkdir(p, 0700);
    auth_load(); boot_load();
    spawn_thread(boot_thread, NULL);
    boot_check(0);
}

/* ---------------- API ---------------- */
long yt_get(const char *url, int auth, Buf *out) {
    Buf ah = {0}; const char *h[4]; int n = 0;
    h[n++] = "User-Agent: " UA_TV;
    char *tok = auth ? token(0) : NULL;
    if (tok) { buf_fmt(&ah, "Authorization: Bearer %s", tok); buf_zero(&ah); h[n++] = (char *)ah.p; }
    h[n] = NULL;
    NetRes r = {0};
    net_req(url, h, NULL, 0, 30, NULL, NULL, &r);
    out->n = 0; buf_put(out, r.body.p, r.body.n); buf_zero(out);
    buf_free(&r.body); buf_free(&ah); free(tok);
    return r.status;
}

long yt_api(const char *endpoint, const char *body, Buf *out) {
    if (wait_for(&Y.boot_ok, 30)) { out->n = 0; buf_str(out, "{\"error\":{\"message\":\"Cannot reach the service.\"}}"); buf_zero(out); return 0; }
    int attempt; long status = 0;
    for (attempt = 0; attempt < 2; attempt++) {
        Buf req = {0}, hv = {0}, hx = {0}, ha = {0}, url = {0};
        char *tok = token(attempt > 0);
        pthread_mutex_lock(&Y.mu);
        buf_str(&req, "{\"context\":{\"client\":{\"clientName\":\"TVHTML5\",\"clientVersion\":"); json_escape(&req, Y.version);
        buf_str(&req, ",\"hl\":\"en\",\"gl\":\"US\",\"platform\":\"TV\",\"clientScreen\":\"WATCH\",\"deviceMake\":\"Samsung\",\"deviceModel\":\"SmartTV\","
                      "\"osName\":\"Tizen\",\"osVersion\":\"6.5\",\"browserName\":\"Cobalt\",\"userAgent\":\"" UA_TV "\",\"visitorData\":");
        json_escape(&req, Y.visitor); buf_str(&req, "}}");
        buf_fmt(&hv, "X-YouTube-Client-Version: %s", Y.version); buf_zero(&hv);
        buf_fmt(&hx, "X-Goog-Visitor-Id: %s", Y.visitor); buf_zero(&hx);
        pthread_mutex_unlock(&Y.mu);
        const char *b = body; while (*b == ' ' || *b == '\n') b++;
        if (*b == '{') { b++; while (*b == ' ' || *b == '\n') b++; }
        if (*b && *b != '}') { buf_byte(&req, ','); buf_str(&req, b); } else buf_byte(&req, '}');
        buf_zero(&req);
        buf_fmt(&url, "https://www.youtube.com/youtubei/v1/%s%sprettyPrint=false", endpoint, strchr(endpoint, '?') ? "&" : "?"); buf_zero(&url);
        const char *h[10]; int n = 0;
        h[n++] = "Content-Type: application/json"; h[n++] = "User-Agent: " UA_TV; h[n++] = "X-YouTube-Client-Name: 7";
        h[n++] = (char *)hv.p; h[n++] = (char *)hx.p; h[n++] = "Origin: https://www.youtube.com";
        if (tok) { buf_fmt(&ha, "Authorization: Bearer %s", tok); buf_zero(&ha); h[n++] = (char *)ha.p; }
        h[n] = NULL;
        NetRes r = {0};
        int64_t t0 = now_ms();
        net_req((char *)url.p, h, req.p, req.n, 40, NULL, NULL, &r);
        status = r.status;
        LOG("api %s: http %ld, %lu bytes, %lld ms", endpoint, status, (unsigned long)r.body.n, (long long)(now_ms() - t0));
        out->n = 0; buf_put(out, r.body.p, r.body.n); buf_zero(out);
        if (!status) LOG("yt: %s: %s", endpoint, r.err);
        buf_free(&r.body); buf_free(&req); buf_free(&hv); buf_free(&hx); buf_free(&ha); buf_free(&url);
        int retry = status == 401 && tok != NULL;
        free(tok);
        if (!retry) break;
    }
    return status;
}

/* ---------------- n ---------------- */
int yt_solve_n(const char *n, char *out, size_t outsz) {
    int i, rc = -1;
    if (strlen(n) >= sizeof Y.nc[0].n) return -1;
    for (i = 0; n[i]; i++) if (!((n[i] >= 'a' && n[i] <= 'z') || (n[i] >= 'A' && n[i] <= 'Z') || (n[i] >= '0' && n[i] <= '9') || n[i] == '-' || n[i] == '_')) return -1;
    pthread_mutex_lock(&g_js_mu);
    pthread_mutex_lock(&Y.mu);
    for (i = 0; i < 16; i++) if (Y.nc[i].n[0] && !strcmp(Y.nc[i].n, n)) { snprintf(out, outsz, "%s", Y.nc[i].out); rc = 0; }
    char pre[600], in[600], res[600];
    pre_path(pre, sizeof pre, Y.player_id);
    pthread_mutex_unlock(&Y.mu);
    if (rc) {
        snprintf(in, sizeof in, "%s/cache/n-in.js", Y.data); snprintf(res, sizeof res, "%s/cache/n-out.json", Y.data);
        Buf b = {0};
        buf_str(&b, "globalThis.__in={pre:"); json_escape(&b, pre); buf_fmt(&b, ",n:[\"%s\"]};", n);
        write_file(in, b.p, b.n, 0600); buf_free(&b);
        int64_t t0 = now_ms();
        int ex = run_js(in, res);
        char *j = read_file(res, NULL), path[80];
        snprintf(path, sizeof path, "n.%s", n);
        char *v = j ? json_str(j, path) : NULL;
        if (v && *v && strlen(v) < sizeof Y.nc[0].out) {
            snprintf(out, outsz, "%s", v); rc = 0;
            pthread_mutex_lock(&Y.mu);
            snprintf(Y.nc[Y.nci].n, sizeof Y.nc[0].n, "%s", n); snprintf(Y.nc[Y.nci].out, sizeof Y.nc[0].out, "%s", v); Y.nci = (Y.nci + 1) % 16;
            pthread_mutex_unlock(&Y.mu);
            LOG("yt: n solved in %lld ms", (long long)(now_ms() - t0));
        } else { char *e = j ? json_str(j, "error") : NULL; LOG("yt: n solve failed (exit %d): %s", ex, e ? e : "no output"); free(e); }
        free(v); free(j); unlink(in); unlink(res);
    }
    pthread_mutex_unlock(&g_js_mu);
    return rc;
}
