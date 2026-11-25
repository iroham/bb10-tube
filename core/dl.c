/* dl.c: videos kept on the phone for later. A download is the same HLS the player gets, written to
 * <data>/dl/<video id>/: v.m3u8, <n>.ts, meta.json (title etc. from the UI), state.json, thumb.jpg.
 * One download runs at a time; an interrupted one continues with the segments that are missing. */
#include "core.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

enum { QUEUED, RUNNING, DONE, FAILED, PAUSED };
static const char *const state_name[] = { "queued", "running", "done", "failed", "paused" };
typedef struct { char id[20]; int used, height, nseg, done, state, cancel, w, h, dur; long long bytes; char err[120]; } Job;
static Job g_jobs[16];
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;      /* the job table */
static pthread_mutex_t g_run = PTHREAD_MUTEX_INITIALIZER;     /* one download at a time */

static int id_ok(const char *id) {
    size_t n = strlen(id), i;
    if (n < 6 || n > 16) return 0;
    for (i = 0; i < n; i++) if (!((id[i] >= 'a' && id[i] <= 'z') || (id[i] >= 'A' && id[i] <= 'Z') || (id[i] >= '0' && id[i] <= '9') || id[i] == '-' || id[i] == '_')) return 0;
    return 1;
}
static void dir_of(const char *id, char *out, size_t n) { snprintf(out, n, "%s/dl/%s", yt_data_dir(), id); }
static Job *job_find(const char *id) { int i; for (i = 0; i < 16; i++) if (g_jobs[i].used && !strcmp(g_jobs[i].id, id)) return &g_jobs[i]; return NULL; }
static void save_state(const Job *j) {
    char p[700]; Buf b = {0};
    dir_of(j->id, p, sizeof p); strncat(p, "/state.json", sizeof p - strlen(p) - 1);
    buf_fmt(&b, "{\"height\":%d,\"nseg\":%d,\"done\":%d,\"bytes\":%lld,\"w\":%d,\"h\":%d,\"dur\":%d,\"complete\":%s}", j->height, j->nseg, j->done, j->bytes, j->w, j->h, j->dur, j->state == DONE ? "true" : "false");
    write_file(p, b.p, b.n, 0600);
    buf_free(&b);
}
static void finish(Job *j, int state, const char *err) {
    pthread_mutex_lock(&g_mu);
    j->state = state; snprintf(j->err, sizeof j->err, "%s", err ? err : "");
    save_state(j);
    pthread_mutex_unlock(&g_mu);
    LOG("dl %s: %s %s (%d/%d)", j->id, state_name[state], j->err, j->done, j->nseg);
}

static void *dl_thread(void *arg) {
    Job *j = arg;
    char dir[600], p[700];
    int attempt;
    dir_of(j->id, dir, sizeof dir);
    pthread_mutex_lock(&g_run);
    if (j->cancel) { pthread_mutex_unlock(&g_run); finish(j, PAUSED, ""); return NULL; }
    pthread_mutex_lock(&g_mu); j->state = RUNNING; pthread_mutex_unlock(&g_mu);
    /* the picture for the list, kept with the video */
    snprintf(p, sizeof p, "%s/meta.json", dir);
    char *meta = read_file(p, NULL), *thumb = meta ? json_str(meta, "thumb") : NULL;
    snprintf(p, sizeof p, "%s/thumb.jpg", dir);
    if (thumb && !strncmp(thumb, "https://", 8) && access(p, R_OK)) {
        NetRes r = {0};
        if (!net_req(thumb, NULL, NULL, 0, 25, NULL, NULL, &r) && r.status == 200 && r.body.n) write_file(p, r.body.p, r.body.n, 0600);
        buf_free(&r.body);
    }
    free(thumb); free(meta);
    for (attempt = 0; attempt < 5 && !j->cancel; attempt++) {
        Buf ans = {0}, seg = {0}; const char *ct; int idx, broken = 0;
        play_open(j->id, j->height, 0, "", &ans, 1);
        buf_zero(&ans);
        int sid = (int)json_num((char *)ans.p, "id", 0);
        char *status = json_str((char *)ans.p, "status");
        if (!status || strcmp(status, "OK") || !sid) {
            char *reason = json_str((char *)ans.p, "reason");
            int waiting = status && !strcmp(status, "WAIT");
            if (!waiting) { pthread_mutex_unlock(&g_run); finish(j, FAILED, reason ? reason : "The video could not be opened."); free(reason); free(status); buf_free(&ans); return NULL; }
            free(reason); free(status); buf_free(&ans); sleep(5); continue;
        }
        free(status);
        pthread_mutex_lock(&g_mu);
        j->height = (int)json_num((char *)ans.p, "height", j->height); j->w = (int)json_num((char *)ans.p, "width", 0);
        j->h = (int)json_num((char *)ans.p, "height_px", 0); j->dur = (int)json_num((char *)ans.p, "duration", 0);
        pthread_mutex_unlock(&g_mu);
        buf_free(&ans);
        if (play_serve(sid, "v.m3u8", &seg, &ct) != 200) { play_stop(sid); buf_free(&seg); sleep(3); continue; }
        buf_zero(&seg);
        int n = 0; const char *q = (char *)seg.p;
        while ((q = strstr(q, "#EXTINF"))) { n++; q += 7; }
        snprintf(p, sizeof p, "%s/v.m3u8", dir);
        write_file(p, seg.p, seg.n, 0600);
        long long bytes = 0; int done = 0; struct stat st;
        for (idx = 0; idx < n; idx++) { snprintf(p, sizeof p, "%s/%d.ts", dir, idx); if (!stat(p, &st) && st.st_size > 0) { bytes += st.st_size; done++; } }
        pthread_mutex_lock(&g_mu); j->nseg = n; j->done = done; j->bytes = bytes; pthread_mutex_unlock(&g_mu);
        for (idx = 0; idx < n && !j->cancel && !broken; idx++) {
            char name[24];
            snprintf(p, sizeof p, "%s/%d.ts", dir, idx);
            if (!stat(p, &st) && st.st_size > 0) continue;
            snprintf(name, sizeof name, "%d.ts", idx);
            seg.n = 0;
            int code = play_serve(sid, name, &seg, &ct);
            if (code != 200 || !seg.n) { broken = 1; break; }
            if (write_file(p, seg.p, seg.n, 0600)) { play_stop(sid); buf_free(&seg); pthread_mutex_unlock(&g_run); finish(j, FAILED, "Not enough space on the phone."); return NULL; }
            pthread_mutex_lock(&g_mu); j->done++; j->bytes += (long long)seg.n; if (j->done % 10 == 0) save_state(j); pthread_mutex_unlock(&g_mu);
            attempt = 0;
        }
        play_stop(sid);
        buf_free(&seg);
        if (!broken && !j->cancel && j->done >= n && n > 0) { pthread_mutex_unlock(&g_run); finish(j, DONE, ""); return NULL; }
        if (broken) sleep(3);
    }
    pthread_mutex_unlock(&g_run);
    finish(j, j->cancel ? PAUSED : FAILED, j->cancel ? "" : "The connection kept failing. Try again.");
    return NULL;
}

void dl_init(void) { char p[600]; snprintf(p, sizeof p, "%s/dl", yt_data_dir()); mkdir(p, 0700); }

void dl_start(const char *id, int height, const char *meta_json, Buf *out) {
    char dir[600], p[700];
    if (!id_ok(id)) { buf_str(out, "{\"ok\":false}"); return; }
    dir_of(id, dir, sizeof dir); mkdir(dir, 0700);
    snprintf(p, sizeof p, "%s/meta.json", dir);
    if (meta_json && strlen(meta_json) > 2) write_file(p, meta_json, strlen(meta_json), 0600);
    pthread_mutex_lock(&g_mu);
    Job *j = job_find(id); int i, start = 0;
    if (!j) for (i = 0; i < 16 && !j; i++) if (!g_jobs[i].used || (g_jobs[i].state >= DONE && g_jobs[i].state != RUNNING)) { j = &g_jobs[i]; memset(j, 0, sizeof *j); j->used = 1; snprintf(j->id, sizeof j->id, "%s", id); j->state = DONE; }
    if (j && j->state != QUEUED && j->state != RUNNING) { j->height = height; j->state = QUEUED; j->cancel = 0; j->err[0] = 0; start = 1; }
    pthread_mutex_unlock(&g_mu);
    if (start && spawn_thread(dl_thread, j)) { j->state = FAILED; start = 0; }
    buf_fmt(out, "{\"ok\":%s}", j ? "true" : "false");
}
void dl_pause(const char *id) { pthread_mutex_lock(&g_mu); Job *j = job_find(id); if (j) j->cancel = 1; pthread_mutex_unlock(&g_mu); }
void dl_remove(const char *id) {
    char dir[600], p[900]; int i;
    if (!id_ok(id)) return;
    dl_pause(id);
    for (i = 0; i < 100; i++) {           /* let a running download let go of the folder */
        pthread_mutex_lock(&g_mu); Job *j = job_find(id); int busy = j && (j->state == RUNNING || j->state == QUEUED); pthread_mutex_unlock(&g_mu);
        if (!busy) break;
        usleep(100000);
    }
    pthread_mutex_lock(&g_mu); Job *j = job_find(id); if (j) j->used = 0; pthread_mutex_unlock(&g_mu);
    dir_of(id, dir, sizeof dir);
    DIR *d = opendir(dir); struct dirent *e;
    while (d && (e = readdir(d))) { if (e->d_name[0] == '.') continue; snprintf(p, sizeof p, "%s/%s", dir, e->d_name); unlink(p); }
    if (d) closedir(d);
    rmdir(dir);
}
void dl_list(Buf *out) {
    char root[600], p[900];
    snprintf(root, sizeof root, "%s/dl", yt_data_dir());
    DIR *d = opendir(root); struct dirent *e; int n = 0;
    buf_str(out, "[");
    while (d && (e = readdir(d))) {
        if (e->d_name[0] == '.' || !id_ok(e->d_name)) continue;
        snprintf(p, sizeof p, "%s/%s/meta.json", root, e->d_name);
        char *meta = read_file(p, NULL);
        snprintf(p, sizeof p, "%s/%s/state.json", root, e->d_name);
        char *st = read_file(p, NULL);
        int height = st ? (int)json_num(st, "height", 0) : 0, nseg = st ? (int)json_num(st, "nseg", 0) : 0, done = st ? (int)json_num(st, "done", 0) : 0;
        long long bytes = st ? json_num(st, "bytes", 0) : 0;
        int w = st ? (int)json_num(st, "w", 0) : 0, h = st ? (int)json_num(st, "h", 0) : 0, dur = st ? (int)json_num(st, "dur", 0) : 0;
        size_t l; const char *c = st ? json_get(st, "complete", &l) : NULL;
        int state = c && *c == 't' ? DONE : PAUSED; char err[120] = "";
        pthread_mutex_lock(&g_mu);
        Job *j = job_find(e->d_name);
        if (j) { state = j->state; if (j->nseg) { nseg = j->nseg; done = j->done; bytes = j->bytes; } if (j->height) height = j->height; if (j->w) { w = j->w; h = j->h; dur = j->dur; } snprintf(err, sizeof err, "%s", j->err); }
        pthread_mutex_unlock(&g_mu);
        snprintf(p, sizeof p, "%s/%s/thumb.jpg", root, e->d_name);
        buf_fmt(out, "%s{\"id\":\"%s\",\"height\":%d,\"nseg\":%d,\"done\":%d,\"bytes\":%lld,\"w\":%d,\"h\":%d,\"dur\":%d,\"state\":\"%s\",\"err\":", n++ ? "," : "", e->d_name, height, nseg, done, bytes, w, h, dur, state_name[state]);
        json_escape(out, err);
        buf_str(out, ",\"thumb\":"); json_escape(out, access(p, R_OK) ? "" : p);
        buf_str(out, ",\"meta\":"); buf_str(out, meta && meta[0] == '{' ? meta : "{}");
        buf_str(out, "}");
        free(meta); free(st);
    }
    if (d) closedir(d);
    buf_str(out, "]");
}
int dl_file(const char *id, const char *name, Buf *out, const char **ctype) {
    char p[800]; size_t l = 0, i, nl = strlen(name);
    if (!id_ok(id) || nl > 20) return 404;
    for (i = 0; i < nl; i++) if (!((name[i] >= '0' && name[i] <= '9') || (name[i] >= 'a' && name[i] <= 'z') || name[i] == '.')) return 404;
    if (strstr(name, "..")) return 404;
    snprintf(p, sizeof p, "%s/dl/%s/%s", yt_data_dir(), id, name);
    char *d = read_file(p, &l);
    if (!d) return 404;
    *ctype = strstr(name, ".m3u8") ? "application/vnd.apple.mpegurl" : "video/mp2t";
    buf_put(out, d, l); free(d);
    return 200;
}
