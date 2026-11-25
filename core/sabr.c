/* sabr.c: playback. For one video: the player request, the choice of an H.264 + AAC pair, and a session that pulls
 * the media with YouTube's server-driven streaming (SABR: POST a protobuf state, get UMP-framed fragments back) and
 * hands it to BB10's player as VOD HLS: the playlist comes from the video's segment index, each HLS segment is one
 * video fragment plus the audio of the same time span, remuxed to MPEG-TS when the player asks for it.
 * The session downloads a few segments ahead of the player's last request and restarts at a new position on seek. */
#include "core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define AHEAD 3                 /* video segments kept ready beyond the one the player asked for last */
#define MAXSEG 48

typedef struct { int seq, done; int64_t start_ms, dur_ms; Buf d; } Seg;
typedef struct {
    int itag; char lmt[24]; char *xtags;
    Track t; int have_init; Buf init;
    Seg *seg[MAXSEG]; int nseg;
    int first, last; int64_t first_ms, end_ms;     /* contiguous run reported to the server */
} Strm;
typedef struct { int itag, init, seq, skip; int64_t start_ms, dur_ms; } Hdr;
typedef struct Session {
    int id, refs, stop, gone, dl;
    pthread_mutex_t mu; pthread_cond_t cv;
    char *url; Buf ustreamer, cookie; char version[40];
    Strm v, a; int height;
    int want;                   /* HLS index the player asked for last (-1: none yet) */
    int run_idx;                /* index the current download run started at (-1: not known yet) */
    int restart;                /* >= 0: restart the run at this index */
    int64_t start_ms;           /* position of the current run's first request */
    int gen;                    /* bumped on restart; a running request of an older gen is abandoned */
    int rn, err; char errmsg[120];
    int backoff_ms;
    TsState ts; Buf cache; int cache_idx;
    /* response parser state (download thread only) */
    Buf in; Hdr hdr[256]; int my_gen, got;
} Session;

static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static Session *g_ses[8];
static int g_next_id = 1;

/* ---- protobuf ---- */
static void pb_varint(Buf *b, uint64_t v) { do { int c = (int)(v & 0x7f); v >>= 7; buf_byte(b, c | (v ? 0x80 : 0)); } while (v); }
static void pb_int(Buf *b, int f, uint64_t v) { pb_varint(b, (uint64_t)f << 3); pb_varint(b, v); }
static void pb_bytes(Buf *b, int f, const void *p, size_t n) { pb_varint(b, (uint64_t)f << 3 | 2); pb_varint(b, n); buf_put(b, p, n); }
static void pb_float(Buf *b, int f, float v) { pb_varint(b, (uint64_t)f << 3 | 5); buf_put(b, &v, 4); }
/* reader: next field; returns 0 at the end or on damage */
static int pb_next(const uint8_t **pp, const uint8_t *end, int *field, uint64_t *val, const uint8_t **bytes, size_t *blen) {
    const uint8_t *p = *pp; uint64_t k = 0, v = 0; int s = 0;
    if (p >= end) return 0;
    for (;;) { if (p >= end || s > 63) return 0; int c = *p++; k |= (uint64_t)(c & 0x7f) << s; s += 7; if (c < 0x80) break; }
    *field = (int)(k >> 3); *bytes = NULL; *blen = 0; *val = 0;
    switch (k & 7) {
    case 0: for (s = 0;;) { if (p >= end || s > 63) return 0; int c = *p++; v |= (uint64_t)(c & 0x7f) << s; s += 7; if (c < 0x80) break; } *val = v; break;
    case 2: for (s = 0;;) { if (p >= end || s > 63) return 0; int c = *p++; v |= (uint64_t)(c & 0x7f) << s; s += 7; if (c < 0x80) break; }
        if (v > (uint64_t)(end - p)) return 0;
        *bytes = p; *blen = (size_t)v; p += v; break;
    case 5: if (end - p < 4) return 0; p += 4; break;
    case 1: if (end - p < 8) return 0; p += 8; break;
    default: return 0;
    }
    *pp = p;
    return 1;
}

/* ---- session data ---- */
static Seg *seg_find(Strm *st, int seq) { int i; for (i = 0; i < st->nseg; i++) if (st->seg[i]->seq == seq) return st->seg[i]; return NULL; }
static void seg_drop(Strm *st, int i) { buf_free(&st->seg[i]->d); free(st->seg[i]); st->seg[i] = st->seg[--st->nseg]; }
static void strm_clear(Strm *st) { while (st->nseg) seg_drop(st, 0); st->first = st->last = 0; st->first_ms = st->end_ms = 0; }
static int64_t v_start90(Session *s, int idx) {
    if (idx >= s->v.t.nseg) return INT64_MAX;
    return (int64_t)(s->v.t.seg_start[idx] * 90000 / s->v.t.timescale);
}
static int64_t v_start_ms(Session *s, int idx) { return (int64_t)(s->v.t.seg_start[idx] * 1000 / s->v.t.timescale); }
static void evict(Session *s) {             /* s->mu held */
    int i, keep = s->want;                 /* keep the previous segment too (seq = index + 1) */
    for (i = 0; i < s->v.nseg;) if (s->v.seg[i]->done && s->v.seg[i]->seq < keep) seg_drop(&s->v, i); else i++;
    if (s->a.t.nseg && s->v.t.nseg && keep >= 1) {
        int64_t lim = v_start90(s, keep - 1);
        for (i = 0; i < s->a.nseg;) {
            Seg *g = s->a.seg[i];
            int64_t e90 = g->seq <= s->a.t.nseg ? (int64_t)(s->a.t.seg_start[g->seq] * 90000 / s->a.t.timescale) : INT64_MAX;
            if (g->done && e90 <= lim) seg_drop(&s->a, i); else i++;
        }
    }
}
static void fail(Session *s, const char *msg) { if (!s->err) { s->err = 1; snprintf(s->errmsg, sizeof s->errmsg, "%s", msg); LOG("play %d: %s", s->id, msg); } pthread_cond_broadcast(&s->cv); }

/* ---- response parts ---- */
static void part(Session *s, int type, const uint8_t *p, size_t n) {
    const uint8_t *e = p + n, *b; size_t bl; uint64_t v; int f;
    if (type == 20) {
        Hdr h = {0}; int id = -1;
        while (pb_next(&p, e, &f, &v, &b, &bl)) {
            if (f == 1) id = (int)v; else if (f == 3) h.itag = (int)v; else if (f == 8) h.init = (int)v; else if (f == 9) h.seq = (int)v;
            else if (f == 11) h.start_ms = (int64_t)v; else if (f == 12) h.dur_ms = (int64_t)v;
        }
        if (id >= 0 && id < 256) s->hdr[id] = h;
    } else if ((type == 21 || type == 22) && n >= 1) {
        Hdr *h = &s->hdr[p[0]];
        Strm *st = h->itag == s->v.itag ? &s->v : h->itag == s->a.itag ? &s->a : NULL;
        if (!st || h->skip) return;
        pthread_mutex_lock(&s->mu);
        if (s->gen != s->my_gen) { pthread_mutex_unlock(&s->mu); return; }
        if (h->init) {
            if (st->have_init) h->skip = 1;
            else if (type == 21) buf_put(&st->init, p + 1, n - 1);
            else {
                if (mp4_init(&st->t, st->init.p, st->init.n) || !st->t.nseg) fail(s, "This video's format is not supported.");
                else {
                    st->have_init = 1;
                    if (st == &s->v && s->run_idx < 0) {
                        int i = 0; uint64_t t = (uint64_t)s->start_ms * st->t.timescale / 1000;
                        while (i + 1 < st->t.nseg && st->t.seg_start[i + 1] <= t) i++;
                        s->run_idx = i;
                    }
                    LOG("play %d: itag %d init, %d segments", s->id, st->itag, st->t.nseg);
                }
                buf_free(&st->init);
                pthread_cond_broadcast(&s->cv);
            }
        } else {
            Seg *g = seg_find(st, h->seq);
            if (g && g->done) h->skip = 1;
            else if (type == 21) {
                if (!g) {
                    if (st->nseg == MAXSEG) { pthread_mutex_unlock(&s->mu); h->skip = 1; return; }
                    g = calloc(1, sizeof *g); g->seq = h->seq; g->start_ms = h->start_ms; g->dur_ms = h->dur_ms; st->seg[st->nseg++] = g;
                }
                buf_put(&g->d, p + 1, n - 1);
            } else if (g) {
                g->done = 1; s->got++;
                if (!st->first || h->seq != st->last + 1) { st->first = h->seq; st->first_ms = h->start_ms; }
                st->last = h->seq; st->end_ms = h->start_ms + h->dur_ms;
                pthread_cond_broadcast(&s->cv);
            }
        }
        pthread_mutex_unlock(&s->mu);
    } else if (type == 35) {
        while (pb_next(&p, e, &f, &v, &b, &bl)) {
            if (f == 7 && b) { pthread_mutex_lock(&s->mu); s->cookie.n = 0; buf_put(&s->cookie, b, bl); pthread_mutex_unlock(&s->mu); }
            else if (f == 4) s->backoff_ms = (int)v;
        }
    } else if (type == 43) {
        while (pb_next(&p, e, &f, &v, &b, &bl)) if (f == 1 && b) {
            pthread_mutex_lock(&s->mu); free(s->url); s->url = malloc(bl + 1); memcpy(s->url, b, bl); s->url[bl] = 0; pthread_mutex_unlock(&s->mu);
            LOG("play %d: redirected", s->id);
        }
    } else if (type == 44) { pthread_mutex_lock(&s->mu); fail(s, "The server refused the stream."); pthread_mutex_unlock(&s->mu); }
    else if (type == 46) { pthread_mutex_lock(&s->mu); fail(s, "reload"); pthread_mutex_unlock(&s->mu); }
}
static int ump_int(const uint8_t *b, size_t n, size_t *i, uint32_t *v) {
    if (*i >= n) return 0;
    int c = b[*i], k = c < 128 ? 1 : c < 192 ? 2 : c < 224 ? 3 : c < 240 ? 4 : 5;
    if (*i + (size_t)k > n) return 0;
    const uint8_t *p = b + *i;
    *v = k == 1 ? (uint32_t)c : k == 2 ? (uint32_t)(c & 0x3f) + 64u * p[1] : k == 3 ? (uint32_t)(c & 0x1f) + 32u * (p[1] + 256u * p[2])
       : k == 4 ? (uint32_t)(c & 0x0f) + 16u * (p[1] + 256u * (p[2] + 256u * p[3])) : (uint32_t)p[1] | (uint32_t)p[2] << 8 | (uint32_t)p[3] << 16 | (uint32_t)p[4] << 24;
    *i += (size_t)k;
    return 1;
}
static size_t sink(void *ctx, const uint8_t *p, size_t n) {
    Session *s = ctx;
    if (s->stop || s->gen != s->my_gen || s->err) return 0;
    buf_put(&s->in, p, n);
    size_t at = 0;
    for (;;) {
        size_t i = at; uint32_t type, len;
        if (!ump_int(s->in.p, s->in.n, &i, &type) || !ump_int(s->in.p, s->in.n, &i, &len)) break;
        if (len > (32u << 20)) return 0;
        if (s->in.n - i < len) break;
        part(s, (int)type, s->in.p + i, len);
        at = i + len;
    }
    if (at) { memmove(s->in.p, s->in.p + at, s->in.n - at); s->in.n -= at; }
    return n;
}

/* ---- request ---- */
static void fmt_id(Buf *b, const Strm *st) {
    pb_int(b, 1, (uint64_t)st->itag); pb_int(b, 2, strtoull(st->lmt, NULL, 10));
    if (st->xtags && *st->xtags) pb_bytes(b, 3, st->xtags, strlen(st->xtags));
}
static void build_request(Session *s, Buf *req) {       /* s->mu held */
    Buf t = {0}, u = {0};
    int64_t pos = s->start_ms;
    if (s->v.first && s->a.first) pos = s->v.end_ms < s->a.end_ms ? s->v.end_ms : s->a.end_ms;
    else if (s->v.first) pos = s->v.end_ms;
    pb_int(&t, 16, (uint64_t)s->height); pb_int(&t, 21, (uint64_t)s->height); pb_int(&t, 23, 5000000); pb_int(&t, 28, (uint64_t)pos);
    pb_int(&t, 34, 1); pb_float(&t, 35, 1.0f); pb_int(&t, 40, 0);
    pb_bytes(req, 1, t.p, t.n);
    const Strm *both[2] = { &s->a, &s->v }; int i;
    for (i = 0; i < 2; i++) if (both[i]->first) { t.n = 0; fmt_id(&t, both[i]); pb_bytes(req, 2, t.p, t.n); }
    for (i = 0; i < 2; i++) if (both[i]->first) {
        const Strm *st = both[i]; uint64_t d = (uint64_t)(st->end_ms - st->first_ms);
        t.n = 0; u.n = 0; fmt_id(&u, st); pb_bytes(&t, 1, u.p, u.n);
        pb_int(&t, 2, (uint64_t)st->first_ms); pb_int(&t, 3, d); pb_int(&t, 4, (uint64_t)st->first); pb_int(&t, 5, (uint64_t)st->last);
        u.n = 0; pb_int(&u, 1, (uint64_t)st->first_ms); pb_int(&u, 2, d); pb_int(&u, 3, 1000); pb_bytes(&t, 6, u.p, u.n);
        pb_bytes(req, 3, t.p, t.n);
    }
    pb_int(req, 4, (uint64_t)pos); pb_bytes(req, 5, s->ustreamer.p, s->ustreamer.n);
    t.n = 0; fmt_id(&t, &s->a); pb_bytes(req, 16, t.p, t.n);
    t.n = 0; fmt_id(&t, &s->v); pb_bytes(req, 17, t.p, t.n);
    t.n = 0; pb_int(&t, 16, 7); pb_bytes(&t, 17, s->version, strlen(s->version)); pb_bytes(&t, 18, "Tizen", 5); pb_bytes(&t, 19, "6.5", 3);
    pb_bytes(&t, 12, "Samsung", 7); pb_bytes(&t, 13, "SmartTV", 7);
    u.n = 0; pb_bytes(&u, 1, t.p, t.n);
    if (s->cookie.n) pb_bytes(&u, 3, s->cookie.p, s->cookie.n);
    pb_bytes(req, 19, u.p, u.n);
    buf_free(&t); buf_free(&u);
}
static int want_more(Session *s) {          /* s->mu held */
    if (!s->v.have_init || !s->a.have_init || !s->v.first) return 1;
    if (s->want < 0) return 0;              /* nothing asked for yet: do not download ahead (the player may seek first) */
    int target = s->want + 1 + AHEAD;
    if (target > s->v.t.nseg) target = s->v.t.nseg;
    if (s->v.last < target) return 1;
    /* audio up to the end of the last video segment we hold */
    if (s->a.last < s->a.t.nseg && s->a.end_ms < s->v.end_ms) return 1;
    return 0;
}
static void session_free(Session *s) {
    strm_clear(&s->v); strm_clear(&s->a);
    track_free(&s->v.t); track_free(&s->a.t); buf_free(&s->v.init); buf_free(&s->a.init); free(s->v.xtags); free(s->a.xtags);
    free(s->url); buf_free(&s->ustreamer); buf_free(&s->cookie); buf_free(&s->cache); buf_free(&s->in);
    pthread_mutex_destroy(&s->mu); pthread_cond_destroy(&s->cv);
    free(s);
}
static void *run(void *arg) {
    Session *s = arg;
    static const char *const h[] = { "User-Agent: " UA_TV, "Content-Type: application/x-protobuf", "Accept: application/vnd.yt-ump",
                                     "Accept-Encoding: identity", "Origin: https://www.youtube.com", NULL };
    int fails = 0, idle = 0;
    for (;;) {
        Buf req = {0}, url = {0};
        pthread_mutex_lock(&s->mu);
        while (!s->stop && s->restart < 0 && (s->err || !want_more(s))) pthread_cond_wait(&s->cv, &s->mu);
        if (s->stop) { pthread_mutex_unlock(&s->mu); break; }
        if (s->restart >= 0) {
            strm_clear(&s->v); strm_clear(&s->a);
            s->run_idx = s->restart; s->want = s->restart; s->start_ms = v_start_ms(s, s->restart) + 5;
            s->restart = -1; s->gen++; s->err = 0; fails = idle = 0;
            LOG("play %d: seek to segment %d (%lld ms)", s->id, s->run_idx, (long long)s->start_ms);
        }
        evict(s);
        build_request(s, &req);
        buf_fmt(&url, "%s&rn=%d", s->url, ++s->rn); buf_zero(&url);
        s->my_gen = s->gen; s->got = 0; s->in.n = 0; s->backoff_ms = 0;
        memset(s->hdr, 0, sizeof s->hdr);
        pthread_mutex_unlock(&s->mu);
        NetRes r = {0};
        int64_t t0 = now_ms();
        int rc = net_req((char *)url.p, h, req.p, req.n, 0, sink, s, &r);
        buf_free(&req); buf_free(&url); buf_free(&r.body);
        pthread_mutex_lock(&s->mu);
        /* fragments that did not finish in this response are dropped */
        int i;
        for (i = 0; i < s->v.nseg;) if (!s->v.seg[i]->done) seg_drop(&s->v, i); else i++;
        for (i = 0; i < s->a.nseg;) if (!s->a.seg[i]->done) seg_drop(&s->a, i); else i++;
        int stale = s->gen != s->my_gen || s->stop;
        if (!stale) {
            if (r.status == 200 && s->got) { fails = idle = 0; if (s->rn % 10 == 1) LOG("play %d: +%d fragments in %lld ms (video to %d, audio to %d)", s->id, s->got, (long long)(now_ms() - t0), s->v.last, s->a.last); }
            else if (r.status == 200 && !rc) { if (++idle > 12) fail(s, "The stream stopped."); }
            else if (r.status == 403) fail(s, "reload");
            else if (++fails > 5) fail(s, "Connection problem.");
            else LOG("play %d: request failed (http %ld, %s), retry %d", s->id, r.status, r.err, fails);
        }
        int wait = stale ? 0 : s->got ? 0 : s->backoff_ms > 0 ? s->backoff_ms : fails ? 500 << fails : 400;
        pthread_mutex_unlock(&s->mu);
        if (wait > 0) usleep((useconds_t)(wait > 8000 ? 8000 : wait) * 1000);
    }
    /* stopped: leave the table, wait for readers, free */
    for (;;) {
        pthread_mutex_lock(&g_mu);
        if (!s->refs) { int i; for (i = 0; i < 8; i++) if (g_ses[i] == s) g_ses[i] = NULL; pthread_mutex_unlock(&g_mu); break; }
        pthread_mutex_unlock(&g_mu);
        usleep(100000);
    }
    LOG("play %d: closed", s->id);
    session_free(s);
    return NULL;
}

void play_stop_all(void) {
    int i;
    pthread_mutex_lock(&g_mu);
    for (i = 0; i < 8; i++) if (g_ses[i] && !g_ses[i]->dl) { Session *s = g_ses[i]; pthread_mutex_lock(&s->mu); s->stop = 1; pthread_cond_broadcast(&s->cv); pthread_mutex_unlock(&s->mu); }
    pthread_mutex_unlock(&g_mu);
}
void play_stop(int id) {
    int i;
    pthread_mutex_lock(&g_mu);
    for (i = 0; i < 8; i++) if (g_ses[i] && g_ses[i]->id == id) { Session *s = g_ses[i]; pthread_mutex_lock(&s->mu); s->stop = 1; pthread_cond_broadcast(&s->cv); pthread_mutex_unlock(&s->mu); }
    pthread_mutex_unlock(&g_mu);
}

/* ---- HLS side ---- */
static void wait_ms(Session *s, int ms) {
    struct timespec ts; int64_t t = now_ms() + ms;
    ts.tv_sec = (time_t)(t / 1000); ts.tv_nsec = (long)(t % 1000) * 1000000L;
    pthread_cond_timedwait(&s->cv, &s->mu, &ts);
}
static int serve_segment(Session *s, int idx, Buf *out) {      /* s->mu held */
    int64_t deadline = now_ms() + 20000;
    if (idx < 0 || !s->v.have_init || idx >= s->v.t.nseg) return 404;
    if (s->cache_idx == idx && s->cache.n) { buf_put(out, s->cache.p, s->cache.n); return 200; }
    for (;;) {
        if (s->stop) return 410;
        if (s->err) return 502;
        if (s->want != idx) { s->want = idx; pthread_cond_broadcast(&s->cv); }
        Seg *vs = seg_find(&s->v, idx + 1);
        int64_t b0 = v_start90(s, idx), b1 = v_start90(s, idx + 1);
        if (vs && vs->done && s->a.have_init) {
            /* audio fragments overlapping [b0, b1) */
            int j, ready = 1;
            for (j = 0; j < s->a.t.nseg && ready; j++) {
                int64_t a0 = (int64_t)(s->a.t.seg_start[j] * 90000 / s->a.t.timescale), a1 = (int64_t)(s->a.t.seg_start[j + 1] * 90000 / s->a.t.timescale);
                if (a1 <= b0 || a0 >= b1) continue;
                Seg *g = seg_find(&s->a, j + 1);
                if (!(g && g->done) && !(s->a.first && j + 1 < s->a.first)) ready = 0;     /* earlier than the run: play without */
            }
            if (ready) {
                Sample *vsm = NULL; int nv = 0;
                if (mp4_fragment(&s->v.t, vs->d.p, vs->d.n, &vsm, &nv)) return 500;
                AFrame *af = NULL; int na = 0, cap = 0;
                for (j = 0; j < s->a.t.nseg; j++) {
                    int64_t a0 = (int64_t)(s->a.t.seg_start[j] * 90000 / s->a.t.timescale), a1 = (int64_t)(s->a.t.seg_start[j + 1] * 90000 / s->a.t.timescale);
                    if (a1 <= b0 || a0 >= b1) continue;
                    Seg *g = seg_find(&s->a, j + 1); Sample *as; int n, k;
                    if (!g || !g->done || mp4_fragment(&s->a.t, g->d.p, g->d.n, &as, &n)) continue;
                    for (k = 0; k < n; k++) {
                        int64_t pts = as[k].pts * 90000 / (int64_t)s->a.t.timescale;
                        if (pts < b0 || pts >= b1) continue;
                        if (na == cap) { cap = cap ? cap * 2 : 512; af = realloc(af, sizeof *af * (size_t)cap); }
                        af[na].pts90 = pts; af[na].p = g->d.p + as[k].off; af[na].size = as[k].size; na++;
                    }
                    free(as);
                }
                s->cache.n = 0;
                ts_segment(&s->cache, &s->ts, &s->v.t, vs->d.p, vsm, nv, &s->a.t, af, na);
                s->cache_idx = idx;
                free(vsm); free(af);
                buf_put(out, s->cache.p, s->cache.n);
                return 200;
            }
        } else if (s->restart < 0 && s->run_idx >= 0 && !vs) {
            /* not held and not coming soon: before this run, well after it, or already passed and let go (the
             * player went back, or seeks inside what it had read ahead): move the run */
            int top = (s->v.last > s->run_idx ? s->v.last : s->run_idx) + AHEAD + 3;
            if (idx < s->run_idx || idx + 1 > top || idx + 1 <= s->v.last) { s->restart = idx; pthread_cond_broadcast(&s->cv); }
        }
        if (now_ms() > deadline) { LOG("play %d: segment %d did not arrive (run from %d, held to %d)", s->id, idx, s->run_idx, s->v.last); return 504; }
        wait_ms(s, 500);
    }
}
int play_serve(int id, const char *name, Buf *out, const char **ctype) {
    Session *s = NULL; int i, code;
    pthread_mutex_lock(&g_mu);
    for (i = 0; i < 8; i++) if (g_ses[i] && g_ses[i]->id == id && !g_ses[i]->stop) { s = g_ses[i]; s->refs++; }
    pthread_mutex_unlock(&g_mu);
    if (!s) return 404;
    pthread_mutex_lock(&s->mu);
    if (!strcmp(name, "v.m3u8")) {
        *ctype = "application/vnd.apple.mpegurl";
        if (!s->v.have_init) code = 503;
        else {
            const Track *t = &s->v.t; uint32_t maxd = 0;
            for (i = 0; i < t->nseg; i++) if (t->seg_dur[i] > maxd) maxd = t->seg_dur[i];
            buf_fmt(out, "#EXTM3U\n#EXT-X-VERSION:3\n#EXT-X-TARGETDURATION:%u\n#EXT-X-MEDIA-SEQUENCE:0\n#EXT-X-PLAYLIST-TYPE:VOD\n", (maxd + t->timescale - 1) / t->timescale);
            for (i = 0; i < t->nseg; i++) buf_fmt(out, "#EXTINF:%.3f,\n%d.ts\n", (double)t->seg_dur[i] / t->timescale, i);
            buf_str(out, "#EXT-X-ENDLIST\n");
            code = 200;
        }
    } else if (!strcmp(name, "state")) {
        *ctype = "application/json";
        buf_fmt(out, "{\"err\":%d,\"msg\":", s->err); json_escape(out, s->errmsg); buf_fmt(out, ",\"want\":%d,\"last\":%d}", s->want, s->v.last);
        code = 200;
    } else { *ctype = "video/mp2t"; code = serve_segment(s, atoi(name), out); }
    pthread_mutex_unlock(&s->mu);
    pthread_mutex_lock(&g_mu); s->refs--; pthread_mutex_unlock(&g_mu);
    return code;
}

/* ---- start ---- */
static char *url_param(const char *url, const char *name) {       /* malloc'd raw value */
    size_t nl = strlen(name); const char *p = strchr(url, '?');
    while (p) {
        p++;
        if (!strncmp(p, name, nl) && p[nl] == '=') { size_t l = strcspn(p + nl + 1, "&"); char *v = malloc(l + 1); memcpy(v, p + nl + 1, l); v[l] = 0; return v; }
        p = strchr(p, '&');
    }
    return NULL;
}
static char *url_with_param(const char *url, const char *name, const char *value) {
    size_t nl = strlen(name); const char *p = strchr(url, '?'); Buf b = {0};
    while (p) {
        p++;
        if (!strncmp(p, name, nl) && p[nl] == '=') {
            size_t l = strcspn(p + nl + 1, "&");
            buf_put(&b, url, (size_t)(p + nl + 1 - url)); buf_str(&b, value); buf_str(&b, p + nl + 1 + l); buf_zero(&b);
            return (char *)b.p;
        }
        p = strchr(p, '&');
    }
    return strdup(url);
}
static void raw_field(Buf *out, const char *name, const char *json, const char *path) {
    size_t l; const char *p = json_get(json, path, &l);
    buf_fmt(out, ",\"%s\":", name);
    if (p) buf_put(out, p, l); else buf_str(out, "null");
}
static void answer_error(Buf *out, const char *status, const char *reason) {
    buf_str(out, "{\"status\":"); json_escape(out, status); buf_str(out, ",\"reason\":"); json_escape(out, reason); buf_str(out, "}");
}

void play_start(const char *video_id, int max_height, int start_s, const char *base_url, Buf *out) { play_open(video_id, max_height, start_s, base_url, out, 0); }

/* dl: a download session; it does not end the playback session and is not ended by a new one */
void play_open(const char *video_id, int max_height, int start_s, const char *base_url, Buf *out, int dl) {
    Buf req = {0}, res = {0};
    int64_t t0 = now_ms();
    if (!dl) play_stop_all();
    if (yt_wait_player(150)) { answer_error(out, "WAIT", "The player is still being prepared. Try again in a moment."); return; }
    buf_str(&req, "{\"videoId\":"); json_escape(&req, video_id);
    buf_fmt(&req, ",\"contentCheckOk\":true,\"racyCheckOk\":true,\"playbackContext\":{\"contentPlaybackContext\":{\"signatureTimestamp\":%d,\"html5Preference\":\"HTML5_PREF_WANTS\"}}}", yt_sts());
    buf_zero(&req);
    long st = yt_api("player", (char *)req.p, &res);
    buf_free(&req);
    const char *j = (char *)res.p;
    char *status = j ? json_str(j, "playabilityStatus.status") : NULL;
    if (st != 200 || !status) { answer_error(out, "ERROR", st ? "The service refused the request." : "No connection."); free(status); buf_free(&res); return; }
    if (strcmp(status, "OK")) {
        char *reason = json_str(j, "playabilityStatus.reason");
        answer_error(out, status, reason ? reason : "This video cannot be played.");
        LOG("play: %s: %s", status, reason ? reason : "");
        free(reason); free(status); buf_free(&res); return;
    }
    free(status);
    /* formats */
    const char *arr = json_get(j, "streamingData.adaptiveFormats", NULL), *f;
    int i, heights[16], nh = 0;
    struct { int itag, height, fps, score; char lmt[24]; char *xtags; } bv = {0}, ba = {0};
    for (i = 0; arr; i++) {
        char idx[12]; snprintf(idx, sizeof idx, "%d", i);
        if (!(f = json_get(arr, idx, NULL))) break;
        char *mime = json_str(f, "mimeType"), *lmt = json_str(f, "lastModified"), *xt = json_str(f, "xtags");
        int itag = (int)json_num(f, "itag", 0), h = (int)json_num(f, "height", 0), fps = (int)json_num(f, "fps", 0), score = 0;
        if (mime && lmt && strstr(mime, "avc1") && h) {
            int k, seen = 0;
            for (k = 0; k < nh; k++) if (heights[k] == h) seen = 1;
            if (!seen && nh < 16) heights[nh++] = h;
            /* best: the tallest that fits; 30 fps before 60; if nothing fits, the smallest */
            score = h <= max_height ? 100000 + h * 10 + (fps <= 30 ? 5 : 0) : 50000 - h * 10 + (fps <= 30 ? 5 : 0);
            if (score > bv.score) { bv.itag = itag; bv.height = h; bv.fps = fps; bv.score = score; snprintf(bv.lmt, sizeof bv.lmt, "%s", lmt); free(bv.xtags); bv.xtags = xt; xt = NULL; }
        } else if (mime && lmt && strstr(mime, "mp4a")) {
            size_t l; const char *def = json_get(f, "audioTrack.audioIsDefault", &l), *drc = json_get(f, "isDrc", &l);
            int has_track = json_get(f, "audioTrack", NULL) != NULL;
            long long br = json_num(f, "bitrate", 0);
            score = 1 + (!has_track || (def && *def == 't') ? 1000 : 0) + (drc && *drc == 't' ? 0 : 500) + (br > 200000 ? 50 : br > 100000 ? 100 : 20);
            if (score > ba.score) { ba.itag = itag; ba.score = score; snprintf(ba.lmt, sizeof ba.lmt, "%s", lmt); free(ba.xtags); ba.xtags = xt; xt = NULL; }
        }
        free(mime); free(lmt); free(xt);
    }
    char *sabr = json_str(j, "streamingData.serverAbrStreamingUrl");
    char *ust = json_str(j, "playerConfig.mediaCommonConfig.mediaUstreamerRequestConfig.videoPlaybackUstreamerConfig");
    char *hls = json_str(j, "streamingData.hlsManifestUrl");
    long long dur = json_num(j, "videoDetails.lengthSeconds", 0);
    Session *s = NULL;
    if (bv.itag && ba.itag && sabr && ust) {
        char *n = url_param(sabr, "n"), solved[80] = "";
        if (n && yt_solve_n(n, solved, sizeof solved)) { answer_error(out, "ERROR", "Could not prepare the stream address."); free(n); goto done; }
        s = calloc(1, sizeof *s);
        pthread_mutex_init(&s->mu, NULL); pthread_cond_init(&s->cv, NULL);
        s->url = n ? url_with_param(sabr, "n", solved) : strdup(sabr);
        free(n);
        b64_decode(ust, strlen(ust), &s->ustreamer);
        yt_client_version(s->version, sizeof s->version);
        s->v.itag = bv.itag; snprintf(s->v.lmt, sizeof s->v.lmt, "%s", bv.lmt); s->v.xtags = bv.xtags; bv.xtags = NULL;
        s->a.itag = ba.itag; snprintf(s->a.lmt, sizeof s->a.lmt, "%s", ba.lmt); s->a.xtags = ba.xtags; ba.xtags = NULL;
        s->dl = dl;
        s->height = bv.height; s->want = -1; s->run_idx = -1; s->restart = -1; s->cache_idx = -1;
        s->start_ms = (int64_t)start_s * 1000;
        pthread_mutex_lock(&g_mu);
        s->id = g_next_id++; s->refs = 1;
        for (i = 0; i < 8 && g_ses[i]; i++) {}
        if (i < 8) g_ses[i] = s;
        pthread_mutex_unlock(&g_mu);
        if (i == 8 || spawn_thread(run, s)) { answer_error(out, "ERROR", "Too many sessions."); if (i < 8) { pthread_mutex_lock(&g_mu); g_ses[i] = NULL; pthread_mutex_unlock(&g_mu); } session_free(s); s = NULL; goto done; }
        /* the playlist needs the video's segment index */
        int64_t until = now_ms() + 30000;
        pthread_mutex_lock(&s->mu);
        while (!(s->v.have_init && s->a.have_init) && !s->err && now_ms() < until) wait_ms(s, 500);
        int ok = s->v.have_init && s->a.have_init && !s->err, start_idx = s->run_idx, pw = s->v.t.width, ph = s->v.t.height;
        char em[120]; snprintf(em, sizeof em, "%s", s->err ? s->errmsg : "The stream did not start.");
        if (!ok) { s->stop = 1; pthread_cond_broadcast(&s->cv); }
        pthread_mutex_unlock(&s->mu);
        if (!ok) { answer_error(out, "ERROR", em); goto done; }
        buf_fmt(out, "{\"status\":\"OK\",\"id\":%d,\"hls\":\"%s/hls/%d/v.m3u8\",\"state\":\"%s/hls/%d/state\",\"startIndex\":%d,\"width\":%d,\"height_px\":%d,\"live\":false", s->id, base_url, s->id, base_url, s->id, start_idx, pw, ph);
        LOG("play %d: %s itag %d (%dp%d) + %d, ready in %lld ms", s->id, video_id, s->v.itag, bv.height, bv.fps, s->a.itag, (long long)(now_ms() - t0));
    } else if (hls) {
        buf_str(out, "{\"status\":\"OK\",\"id\":0,\"hls\":"); json_escape(out, hls); buf_str(out, ",\"live\":true");
    } else { answer_error(out, "ERROR", "No stream this phone can play (H.264) was offered for this video."); goto done; }
    buf_fmt(out, ",\"duration\":%lld,\"height\":%d,\"fps\":%d,\"heights\":[", dur, bv.height, bv.fps);
    for (i = 0; i < nh; i++) buf_fmt(out, "%s%d", i ? "," : "", heights[i]);
    buf_str(out, "]");
    raw_field(out, "videoDetails", j, "videoDetails");
    raw_field(out, "captions", j, "captions.playerCaptionsTracklistRenderer.captionTracks");
    raw_field(out, "tracking", j, "playbackTracking");
    buf_str(out, "}");
done:
    if (s) { pthread_mutex_lock(&g_mu); s->refs--; pthread_mutex_unlock(&g_mu); }
    free(sabr); free(ust); free(hls); free(bv.xtags); free(ba.xtags); buf_free(&res);
}
