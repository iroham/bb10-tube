/* tubed: the native app's core helper. Shared declarations. */
#ifndef TUBE_CORE_H
#define TUBE_CORE_H
#include <stddef.h>
#include <stdint.h>
#include <pthread.h>

/* ---- util.c ---- */
typedef struct { uint8_t *p; size_t n, cap; } Buf;
void buf_put(Buf *b, const void *p, size_t n);
void buf_str(Buf *b, const char *s);
void buf_fmt(Buf *b, const char *fmt, ...);
void buf_byte(Buf *b, int c);
void buf_free(Buf *b);
void buf_zero(Buf *b);                       /* append a NUL that is not counted in n */
void logf_(const char *fmt, ...);
#define LOG(...) logf_(__VA_ARGS__)
int64_t now_ms(void);
char *read_file(const char *path, size_t *len);          /* malloc'd, NUL terminated; NULL if missing */
int write_file(const char *path, const void *p, size_t n, int mode);   /* atomic (tmp + rename) */
void url_encode(Buf *out, const char *s, size_t n);
size_t url_decode(char *s);                               /* in place, returns new length */
int b64_decode(const char *s, size_t n, Buf *out);        /* standard or url-safe alphabet */
/* tiny JSON reader: value at a path like "a.b.3.c"; returns pointer to the value's first char, sets *len */
const char *json_get(const char *json, const char *path, size_t *len);
char *json_str(const char *json, const char *path);       /* malloc'd unescaped string, NULL if absent/not a string */
long long json_num(const char *json, const char *path, long long dflt);
void json_escape(Buf *out, const char *s);                /* appends a quoted JSON string */

/* ---- media.c: fragmented MP4 in, MPEG-TS out ---- */
typedef struct {
    uint32_t timescale;
    int64_t elst;                    /* media time shown at presentation time 0 */
    uint32_t def_dur, def_size, def_flags;
    int is_video;
    uint8_t *ps; int ps_len;         /* video: SPS+PPS with start codes */
    int nal_len;                     /* video: bytes of each NAL length prefix */
    int width, height;
    int aot, freq_idx, chans;        /* audio */
    int nseg;                        /* segment index (sidx) */
    uint32_t *seg_dur;               /* in timescale units */
    uint64_t *seg_start;             /* nseg + 1 entries, cumulative, in timescale units */
} Track;
typedef struct { int64_t dts, pts; uint32_t off, size; int key; } Sample;   /* times in track timescale, elst applied */
typedef struct { int64_t pts90; const uint8_t *p; uint32_t size; } AFrame;
typedef struct { uint8_t cc_pat, cc_pmt, cc_v, cc_a; } TsState;

int mp4_init(Track *t, const uint8_t *p, size_t n);                       /* 0 ok */
void track_free(Track *t);
int mp4_fragment(const Track *t, const uint8_t *p, size_t n, Sample **out, int *count);   /* 0 ok; *out malloc'd */
/* one HLS segment: all video samples of a fragment + the audio frames that belong to it */
void ts_segment(Buf *out, TsState *st, const Track *v, const uint8_t *vdata, const Sample *vs, int nv,
                const Track *a, const AFrame *af, int na);


/* ---- net.c ---- */
typedef size_t (*NetSink)(void *ctx, const uint8_t *p, size_t n);        /* return n to go on, anything else aborts */
typedef struct { long status; Buf body; char err[256]; } NetRes;
void net_init(void);
/* GET, or POST when post != NULL. hdrs: NULL-terminated "Name: value" list (may be NULL). With a sink the body is
 * streamed to it instead of res->body. timeout_s: whole request (0 = only stall detection). Returns 0 when the
 * transfer completed (whatever the HTTP status). */
int net_req(const char *url, const char *const *hdrs, const void *post, size_t postlen, int timeout_s, NetSink sink, void *ctx, NetRes *res);

/* ---- yt.c: account, bootstrap, API ---- */
#define UA_TV "Mozilla/5.0 (SMART-TV; LINUX; Tizen 6.5) AppleWebKit/537.36 (KHTML, like Gecko) 85.0.4183.93/6.5 TV Safari/537.36"
void yt_init(const char *data_dir, const char *js_dir);
const char *yt_data_dir(void);
void yt_status_json(Buf *out);
int yt_auth_start(Buf *out);                 /* JSON {user_code, verification_url, interval, expires_in} or {error} */
int yt_auth_poll(Buf *out);                  /* JSON {state: "pending" | "ok" | "error", error} */
void yt_auth_signout(void);
int yt_signed_in(void);
/* POST /youtubei/v1/<endpoint>; body is a JSON object, the client context is added. Returns HTTP status (0 = network). */
long yt_api(const char *endpoint, const char *body, Buf *out);
long yt_get(const char *url, int auth, Buf *out);                        /* GET with the TV user agent */
int yt_wait_player(int max_s);               /* 0 when signature timestamp + solver cache are ready */
int yt_sts(void);
void yt_client_version(char *out, size_t n);
int yt_solve_n(const char *n, char *out, size_t outsz);                  /* 0 ok */

/* ---- sabr.c: playback sessions ---- */
/* Starts playback of a video: player request, format choice, SABR session. Writes the JSON answer for the UI. */
void play_start(const char *video_id, int max_height, int start_s, const char *base_url, Buf *out);
/* HLS for a session: name is "v.m3u8" or "<index>.ts". Returns HTTP status; fills out and *ctype. */
int play_serve(int session_id, const char *name, Buf *out, const char **ctype);
void play_stop_all(void);
void play_open(const char *video_id, int max_height, int start_s, const char *base_url, Buf *out, int dl);
void play_stop(int id);

/* ---- dl.c: videos kept for later ---- */
void dl_init(void);
void dl_start(const char *id, int height, const char *meta_json, Buf *out);
void dl_list(Buf *out);
void dl_remove(const char *id);
void dl_pause(const char *id);
int dl_file(const char *id, const char *name, Buf *out, const char **ctype);   /* HTTP status */

/* ---- server.c ---- */
/* Starts the loopback server on its own thread; returns the port (0 on failure). token: path prefix. */
int server_start(const char *token);
/* thread with a stack big enough for TLS work (QNX's default is small) */
int spawn_thread(void *(*fn)(void *), void *arg);
#endif
