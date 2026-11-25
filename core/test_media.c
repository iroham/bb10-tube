/* test_media: Mac-side check of media.c. Takes the two fragmented MP4 files the SABR prototype wrote (init + a run
 * of fragments each) and writes HLS: <out>/v.m3u8 + <out>/NNN.ts, one segment per video fragment.
 *   test_media <video.mp4> <audio.mp4> <out dir> */
#include "core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t r32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
/* offset of the first moof */
static size_t first_moof(const uint8_t *p, size_t n) {
    size_t at = 0;
    while (at + 8 <= n) { if (!memcmp(p + at + 4, "moof", 4)) return at; at += r32(p + at); }
    return n;
}
/* end of the fragment (moof + mdat) starting at at */
static size_t frag_end(const uint8_t *p, size_t n, size_t at) {
    at += r32(p + at);
    while (at + 8 <= n && memcmp(p + at + 4, "moof", 4)) at += r32(p + at);
    return at > n ? n : at;
}

int main(int argc, char **argv) {
    if (argc < 4) return 2;
    size_t vn, an;
    uint8_t *v = (uint8_t *)read_file(argv[1], &vn), *a = (uint8_t *)read_file(argv[2], &an);
    if (!v || !a) { fprintf(stderr, "cannot read input\n"); return 1; }
    Track vt, at;
    if (mp4_init(&vt, v, first_moof(v, vn))) { fprintf(stderr, "video init failed\n"); return 1; }
    if (mp4_init(&at, a, first_moof(a, an))) { fprintf(stderr, "audio init failed\n"); return 1; }
    printf("video %dx%d timescale %u elst %lld nal %d ps %d bytes, sidx %d segments\n", vt.width, vt.height, vt.timescale, (long long)vt.elst, vt.nal_len, vt.ps_len, vt.nseg);
    printf("audio aot %d freq idx %d ch %d timescale %u elst %lld, sidx %d segments\n", at.aot, at.freq_idx, at.chans, at.timescale, (long long)at.elst, at.nseg);
    /* all audio frames */
    AFrame *af = NULL; int na = 0;
    size_t o = first_moof(a, an);
    while (o < an) {
        size_t e = frag_end(a, an, o); Sample *s; int n, i;
        if (mp4_fragment(&at, a + o, e - o, &s, &n)) { fprintf(stderr, "audio fragment at %zu failed\n", o); break; }
        af = realloc(af, sizeof *af * (size_t)(na + n));
        for (i = 0; i < n; i++) { af[na].pts90 = s[i].pts * 90000 / (int64_t)at.timescale; af[na].p = a + o + s[i].off; af[na].size = s[i].size; na++; }
        free(s); o = e;
    }
    printf("audio frames %d: %.3f .. %.3f s\n", na, na ? af[0].pts90 / 90000.0 : 0, na ? af[na - 1].pts90 / 90000.0 : 0);
    char path[1024];
    Buf m3u = {0}, body = {0};
    TsState st = {0};
    int seg = 0, ai = 0; double maxd = 0;
    o = first_moof(v, vn);
    while (o < vn) {
        size_t e = frag_end(v, vn, o); Sample *s; int n, i;
        if (mp4_fragment(&vt, v + o, e - o, &s, &n)) { fprintf(stderr, "video fragment at %zu failed\n", o); break; }
        int64_t start = s[0].dts, end = s[n - 1].dts + (n > 1 ? s[n - 1].dts - s[n - 2].dts : 0), minp = s[0].pts;
        for (i = 0; i < n; i++) if (s[i].pts < minp) minp = s[i].pts;
        int64_t e90 = end * 90000 / (int64_t)vt.timescale;
        int a0 = ai;
        while (ai < na && af[ai].pts90 < e90) ai++;
        Buf ts = {0};
        ts_segment(&ts, &st, &vt, v + o, s, n, &at, af + a0, ai - a0);
        snprintf(path, sizeof path, "%s/%03d.ts", argv[3], seg);
        write_file(path, ts.p, ts.n, 0644);
        double d = (double)(end - start) / vt.timescale;
        if (d > maxd) maxd = d;
        buf_fmt(&body, "#EXTINF:%.3f,\n%03d.ts\n", d, seg);
        printf("seg %d: %d frames (key first %d) dts %.3f..%.3f first pts %.3f, %d audio frames, %zu bytes\n", seg, n, s[0].key, (double)start / vt.timescale, (double)end / vt.timescale, (double)minp / vt.timescale, ai - a0, ts.n);
        buf_free(&ts); free(s); o = e; seg++;
    }
    buf_fmt(&m3u, "#EXTM3U\n#EXT-X-VERSION:3\n#EXT-X-TARGETDURATION:%d\n#EXT-X-MEDIA-SEQUENCE:0\n#EXT-X-PLAYLIST-TYPE:VOD\n", (int)(maxd + 0.999));
    buf_put(&m3u, body.p, body.n); buf_str(&m3u, "#EXT-X-ENDLIST\n");
    snprintf(path, sizeof path, "%s/v.m3u8", argv[3]);
    write_file(path, m3u.p, m3u.n, 0644);
    return 0;
}
