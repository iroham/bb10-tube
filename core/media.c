/* media.c: YouTube delivers H.264 and AAC as two fragmented MP4 streams; BB10's player takes HLS with MPEG-TS
 * segments. This file reads the MP4 side (init segment: codec setup + segment index; media fragment: sample table)
 * and writes the TS side. Nothing is re-encoded: samples are copied, H.264 NAL length prefixes become start codes,
 * AAC frames get ADTS headers. */
#include "core.h"
#include <stdlib.h>
#include <string.h>

static uint32_t r16(const uint8_t *p) { return (uint32_t)p[0] << 8 | p[1]; }
static uint32_t r32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static uint64_t r64(const uint8_t *p) { return (uint64_t)r32(p) << 32 | r32(p + 4); }

/* next box at p (n bytes left): returns total box size (0 = malformed), sets body/len */
static size_t box_at(const uint8_t *p, size_t n, const uint8_t **body, size_t *len) {
    if (n < 8) return 0;
    uint64_t size = r32(p);
    size_t hdr = 8;
    if (size == 1) { if (n < 16) return 0; size = r64(p + 8); hdr = 16; }
    else if (size == 0) size = n;
    if (size < hdr || size > n) return 0;
    *body = p + hdr; *len = (size_t)size - hdr;
    return (size_t)size;
}
static const uint8_t *find_box(const uint8_t *p, size_t n, const char *type, size_t *len) {
    while (n >= 8) {
        const uint8_t *body; size_t bl, sz = box_at(p, n, &body, &bl);
        if (!sz) return NULL;
        if (!memcmp(p + 4, type, 4)) { *len = bl; return body; }
        p += sz; n -= sz;
    }
    return NULL;
}

void track_free(Track *t) { free(t->ps); free(t->seg_dur); free(t->seg_start); memset(t, 0, sizeof *t); }

static void esds_config(Track *t, const uint8_t *p, size_t n) {
    /* descriptors: tag, length (7 bits per byte, high bit = more), body. 3 = ES, 4 = decoder config, 5 = specific */
    size_t i = 4;
    while (i + 2 <= n) {
        int tag = p[i++]; size_t l = 0; int k = 0;
        while (i < n && k++ < 4) { int c = p[i++]; l = l << 7 | (c & 0x7f); if (!(c & 0x80)) break; }
        if (tag == 3) { if (i + 3 > n) return; int fl = p[i + 2]; i += 3; if (fl & 0x80) i += 2; if (fl & 0x40) { if (i < n) i += 1 + p[i]; } if (fl & 0x20) i += 2; }
        else if (tag == 4) i += 13;
        else if (tag == 5) {
            if (i + 2 > n) return;
            t->aot = p[i] >> 3;
            t->freq_idx = (p[i] & 7) << 1 | p[i + 1] >> 7;
            t->chans = (p[i + 1] >> 3) & 15;
            return;
        } else i += l;
    }
}

int mp4_init(Track *t, const uint8_t *p, size_t n) {
    memset(t, 0, sizeof *t);
    size_t ml, l, l2;
    const uint8_t *moov = find_box(p, n, "moov", &ml), *b, *c;
    if (!moov) return -1;
    if ((b = find_box(moov, ml, "mvex", &l)) && (c = find_box(b, l, "trex", &l2)) && l2 >= 24) {
        t->def_dur = r32(c + 12); t->def_size = r32(c + 16); t->def_flags = r32(c + 20);
    }
    size_t tl;
    const uint8_t *trak = find_box(moov, ml, "trak", &tl);
    if (!trak) return -1;
    if ((b = find_box(trak, tl, "edts", &l)) && (c = find_box(b, l, "elst", &l2)) && l2 >= 8) {
        uint32_t cnt = r32(c + 4), i; int v1 = c[0] == 1; size_t o = 8, es = v1 ? 20 : 12;
        for (i = 0; i < cnt && o + es <= l2; i++, o += es) {
            int64_t mt = v1 ? (int64_t)r64(c + o + 8) : (int32_t)r32(c + o + 4);
            if (mt >= 0) { t->elst = mt; break; }
        }
    }
    size_t dl;
    const uint8_t *mdia = find_box(trak, tl, "mdia", &dl);
    if (!mdia) return -1;
    if (!(b = find_box(mdia, dl, "mdhd", &l)) || l < 24) return -1;
    t->timescale = b[0] == 1 ? r32(b + 20) : r32(b + 12);
    if (!t->timescale) return -1;
    if (!(b = find_box(mdia, dl, "minf", &l)) || !(b = find_box(b, l, "stbl", &l)) || !(b = find_box(b, l, "stsd", &l)) || l < 16) return -1;
    const uint8_t *e = b + 8; size_t el = l - 8, bl;
    const uint8_t *eb;
    if (!box_at(e, el, &eb, &bl)) return -1;
    if (!memcmp(e + 4, "avc1", 4) || !memcmp(e + 4, "avc3", 4)) {
        if (bl < 78) return -1;
        t->is_video = 1; t->width = (int)r16(eb + 24); t->height = (int)r16(eb + 26);
        const uint8_t *a = find_box(eb + 78, bl - 78, "avcC", &l);
        if (!a || l < 7) return -1;
        t->nal_len = (a[4] & 3) + 1;
        Buf ps = {0}; size_t o = 5; int cnt = a[o++] & 31, pass;
        for (pass = 0; pass < 2; pass++) {
            for (; cnt > 0; cnt--) {
                if (o + 2 > l) { buf_free(&ps); return -1; }
                size_t nl = r16(a + o); o += 2;
                if (o + nl > l) { buf_free(&ps); return -1; }
                buf_put(&ps, "\0\0\0\1", 4); buf_put(&ps, a + o, nl); o += nl;
            }
            if (!pass) { if (o >= l) { buf_free(&ps); return -1; } cnt = a[o++]; }
        }
        t->ps = ps.p; t->ps_len = (int)ps.n;
    } else if (!memcmp(e + 4, "mp4a", 4)) {
        if (bl < 28) return -1;
        const uint8_t *a = find_box(eb + 28, bl - 28, "esds", &l);
        if (!a) return -1;
        esds_config(t, a, l);
        if (!t->aot) return -1;
    } else return -1;
    /* segment index */
    if ((b = find_box(p, n, "sidx", &l)) && l >= 24) {
        int v1 = b[0] == 1; size_t o = v1 ? 28 : 20;
        if (o + 4 <= l) {
            uint32_t cnt = r16(b + o + 2), i; o += 4;
            if (o + 12 * (size_t)cnt <= l && cnt) {
                t->seg_dur = malloc(sizeof(uint32_t) * cnt);
                t->seg_start = malloc(sizeof(uint64_t) * (cnt + 1));
                uint64_t at = 0;
                for (i = 0; i < cnt; i++, o += 12) { t->seg_dur[i] = r32(b + o + 4); t->seg_start[i] = at; at += t->seg_dur[i]; }
                t->seg_start[cnt] = at; t->nseg = (int)cnt;
            }
        }
    }
    return 0;
}

int mp4_fragment(const Track *t, const uint8_t *p, size_t n, Sample **out, int *count) {
    Sample *s = NULL; int ns = 0, cap = 0;
    size_t at = 0;
    while (at + 8 <= n) {
        const uint8_t *body; size_t bl, sz = box_at(p + at, n - at, &body, &bl);
        if (!sz) break;
        if (!memcmp(p + at + 4, "moof", 4)) {
            size_t moof = at, tl, l;
            const uint8_t *traf = find_box(body, bl, "traf", &tl), *b;
            if (!traf) goto bad;
            uint32_t ddur = t->def_dur, dsize = t->def_size, dflags = t->def_flags;
            uint64_t base = moof; int64_t dts = 0;
            if ((b = find_box(traf, tl, "tfhd", &l)) && l >= 8) {
                uint32_t fl = r32(b) & 0xffffff; size_t o = 8;
                if (fl & 1) { if (o + 8 > l) goto bad; base = r64(b + o); o += 8; }
                if (fl & 2) o += 4;
                if (fl & 8) { if (o + 4 > l) goto bad; ddur = r32(b + o); o += 4; }
                if (fl & 0x10) { if (o + 4 > l) goto bad; dsize = r32(b + o); o += 4; }
                if (fl & 0x20) { if (o + 4 > l) goto bad; dflags = r32(b + o); o += 4; }
            }
            if ((b = find_box(traf, tl, "tfdt", &l)) && l >= 8) dts = b[0] == 1 && l >= 12 ? (int64_t)r64(b + 4) : (int64_t)r32(b + 4);
            /* every trun of the traf */
            const uint8_t *q = traf; size_t qn = tl; uint64_t next_off = base;
            while (qn >= 8) {
                const uint8_t *tb; size_t tbl, tsz = box_at(q, qn, &tb, &tbl);
                if (!tsz) break;
                if (!memcmp(q + 4, "trun", 4) && tbl >= 8) {
                    uint32_t fl = r32(tb) & 0xffffff, cnt = r32(tb + 4), i, first_flags = 0; int v1 = tb[0] == 1; size_t o = 8;
                    uint64_t off = next_off;
                    if (fl & 1) { if (o + 4 > tbl) goto bad; off = base + (int64_t)(int32_t)r32(tb + o); o += 4; }
                    if (fl & 4) { if (o + 4 > tbl) goto bad; first_flags = r32(tb + o); o += 4; }
                    size_t per = ((fl & 0x100) ? 4 : 0) + ((fl & 0x200) ? 4 : 0) + ((fl & 0x400) ? 4 : 0) + ((fl & 0x800) ? 4 : 0);
                    if (per && (tbl - o) / per < cnt) goto bad;
                    if (!per && cnt > 100000) goto bad;
                    for (i = 0; i < cnt; i++) {
                        uint32_t dur = ddur, size = dsize, flags = (i == 0 && (fl & 4)) ? first_flags : dflags; int64_t cto = 0;
                        if (fl & 0x100) { dur = r32(tb + o); o += 4; }
                        if (fl & 0x200) { size = r32(tb + o); o += 4; }
                        if (fl & 0x400) { flags = r32(tb + o); o += 4; }
                        if (fl & 0x800) { cto = v1 ? (int64_t)(int32_t)r32(tb + o) : (int64_t)r32(tb + o); o += 4; }
                        if (off + size > n) goto bad;
                        if (ns == cap) { cap = cap ? cap * 2 : 256; s = realloc(s, sizeof *s * (size_t)cap); }
                        s[ns].dts = dts - t->elst; s[ns].pts = dts + cto - t->elst;
                        s[ns].off = (uint32_t)off; s[ns].size = size; s[ns].key = !(flags & 0x10000);
                        ns++; dts += dur; off += size;
                    }
                    next_off = off;
                }
                q += tsz; qn -= tsz;
            }
        }
        at += sz;
    }
    *out = s; *count = ns;
    return ns ? 0 : -1;
bad:
    free(s); *out = NULL; *count = 0;
    return -1;
}

/* ================= MPEG-TS writer ================= */
#define PID_PMT 0x1000
#define PID_V 0x100
#define PID_A 0x101
#define TS_BASE (10 * 90000LL)          /* keeps early decode times positive */

static uint32_t crc32_mpeg(const uint8_t *p, size_t n) {
    uint32_t c = 0xffffffff;
    while (n--) { c ^= (uint32_t)*p++ << 24; int k; for (k = 0; k < 8; k++) c = c & 0x80000000 ? c << 1 ^ 0x04c11db7 : c << 1; }
    return c;
}
static void ts_psi(Buf *out, int pid, uint8_t *cc, const uint8_t *sec, int len) {
    uint8_t pk[188];
    memset(pk, 0xff, sizeof pk);
    pk[0] = 0x47; pk[1] = (uint8_t)(0x40 | pid >> 8); pk[2] = (uint8_t)pid; pk[3] = (uint8_t)(0x10 | (*cc & 15)); *cc = (uint8_t)((*cc + 1) & 15);
    pk[4] = 0;
    memcpy(pk + 5, sec, (size_t)len);
    uint32_t c = crc32_mpeg(sec, (size_t)len);
    pk[5 + len] = (uint8_t)(c >> 24); pk[6 + len] = (uint8_t)(c >> 16); pk[7 + len] = (uint8_t)(c >> 8); pk[8 + len] = (uint8_t)c;
    buf_put(out, pk, 188);
}
static void ts_tables(Buf *out, TsState *st, int has_audio) {
    static const uint8_t pat[] = { 0x00, 0xb0, 0x0d, 0x00, 0x01, 0xc1, 0x00, 0x00, 0x00, 0x01, 0xe0 | (PID_PMT >> 8), PID_PMT & 0xff };
    uint8_t pmt[32]; int n = 0;
    pmt[n++] = 0x02; pmt[n++] = 0xb0; pmt[n++] = (uint8_t)(has_audio ? 23 : 18);
    pmt[n++] = 0x00; pmt[n++] = 0x01; pmt[n++] = 0xc1; pmt[n++] = 0x00; pmt[n++] = 0x00;
    pmt[n++] = 0xe0 | (PID_V >> 8); pmt[n++] = PID_V & 0xff; pmt[n++] = 0xf0; pmt[n++] = 0x00;
    pmt[n++] = 0x1b; pmt[n++] = 0xe0 | (PID_V >> 8); pmt[n++] = PID_V & 0xff; pmt[n++] = 0xf0; pmt[n++] = 0x00;
    if (has_audio) { pmt[n++] = 0x0f; pmt[n++] = 0xe0 | (PID_A >> 8); pmt[n++] = PID_A & 0xff; pmt[n++] = 0xf0; pmt[n++] = 0x00; }
    ts_psi(out, 0, &st->cc_pat, pat, sizeof pat);
    ts_psi(out, PID_PMT, &st->cc_pmt, pmt, n);
}
static void put_ts(uint8_t *p, int prefix, int64_t t) {
    p[0] = (uint8_t)(prefix << 4 | ((t >> 29) & 0x0e) | 1);
    p[1] = (uint8_t)(t >> 22);
    p[2] = (uint8_t)(((t >> 14) & 0xfe) | 1);
    p[3] = (uint8_t)(t >> 7);
    p[4] = (uint8_t)(((t << 1) & 0xfe) | 1);
}
/* split one PES packet (already complete in pes) into TS packets */
static void ts_packets(Buf *out, int pid, uint8_t *cc, const uint8_t *pes, size_t n, int64_t pcr, int key) {
    int first = 1;
    while (n) {
        uint8_t pk[188];
        int af_min = first ? (pcr >= 0 ? 8 : key ? 2 : 0) : 0;
        size_t room = (size_t)(184 - af_min), take = n < room ? n : room;
        int af = 184 - (int)take;
        pk[0] = 0x47; pk[1] = (uint8_t)((first ? 0x40 : 0) | pid >> 8); pk[2] = (uint8_t)pid;
        pk[3] = (uint8_t)((af ? 0x30 : 0x10) | (*cc & 15)); *cc = (uint8_t)((*cc + 1) & 15);
        if (af) {
            pk[4] = (uint8_t)(af - 1);
            if (af > 1) {
                int o = 6;
                pk[5] = (uint8_t)((first && pcr >= 0 ? 0x10 : 0) | (first && key ? 0x40 : 0));
                if (first && pcr >= 0) {
                    pk[6] = (uint8_t)(pcr >> 25); pk[7] = (uint8_t)(pcr >> 17); pk[8] = (uint8_t)(pcr >> 9); pk[9] = (uint8_t)(pcr >> 1);
                    pk[10] = (uint8_t)((pcr & 1) << 7 | 0x7e); pk[11] = 0; o = 12;
                }
                memset(pk + o, 0xff, (size_t)(4 + af - o));
            }
        }
        memcpy(pk + 4 + af, pes, take);
        buf_put(out, pk, 188);
        pes += take; n -= take; first = 0;
    }
}

void ts_segment(Buf *out, TsState *st, const Track *v, const uint8_t *vdata, const Sample *vs, int nv,
                const Track *a, const AFrame *af, int na) {
    Buf pes = {0};
    int ai = 0, i;
    ts_tables(out, st, a != NULL);
    for (i = 0; i <= nv; i++) {
        int64_t vdts = i < nv ? vs[i].dts * 90000 / (int64_t)v->timescale : INT64_MAX;
        /* audio that starts before this video frame is decoded; up to 6 AAC frames per PES */
        while (ai < na && af[ai].pts90 <= vdts) {
            int k = 0; size_t total = 0;
            while (ai + k < na && k < 6 && af[ai + k].pts90 <= vdts) { total += 7 + af[ai + k].size; k++; }
            uint8_t h[14];
            h[0] = 0; h[1] = 0; h[2] = 1; h[3] = 0xc0;
            h[4] = (uint8_t)((total + 8) >> 8); h[5] = (uint8_t)(total + 8);
            h[6] = 0x80; h[7] = 0x80; h[8] = 5;
            put_ts(h + 9, 2, af[ai].pts90 + TS_BASE);
            pes.n = 0; buf_put(&pes, h, 14);
            for (; k > 0; k--, ai++) {
                uint32_t fl = af[ai].size + 7; uint8_t ad[7];
                ad[0] = 0xff; ad[1] = 0xf1;
                ad[2] = (uint8_t)(((a->aot - 1) & 3) << 6 | (a->freq_idx & 15) << 2 | (a->chans >> 2 & 1));
                ad[3] = (uint8_t)((a->chans & 3) << 6 | (fl >> 11 & 3));
                ad[4] = (uint8_t)(fl >> 3); ad[5] = (uint8_t)((fl & 7) << 5 | 0x1f); ad[6] = 0xfc;
                buf_put(&pes, ad, 7); buf_put(&pes, af[ai].p, af[ai].size);
            }
            ts_packets(out, PID_A, &st->cc_a, pes.p, pes.n, -1, 0);
        }
        if (i == nv) break;
        int64_t dts = vdts + TS_BASE, pts = vs[i].pts * 90000 / (int64_t)v->timescale + TS_BASE;
        uint8_t h[19];
        h[0] = 0; h[1] = 0; h[2] = 1; h[3] = 0xe0; h[4] = 0; h[5] = 0; h[6] = 0x80;
        pes.n = 0;
        if (pts != dts) { h[7] = 0xc0; h[8] = 10; put_ts(h + 9, 3, pts); put_ts(h + 14, 1, dts); buf_put(&pes, h, 19); }
        else { h[7] = 0x80; h[8] = 5; put_ts(h + 9, 2, pts); buf_put(&pes, h, 14); }
        buf_put(&pes, "\0\0\0\1\x09\xf0", 6);                      /* access unit delimiter */
        if (vs[i].key) buf_put(&pes, v->ps, (size_t)v->ps_len);
        const uint8_t *p = vdata + vs[i].off, *end = p + vs[i].size;
        while (p + v->nal_len <= end) {
            uint32_t nl = 0; int k;
            for (k = 0; k < v->nal_len; k++) nl = nl << 8 | *p++;
            if (nl > (uint32_t)(end - p)) break;
            int type = nl ? p[0] & 31 : 0;
            if (type != 9 && !(vs[i].key && (type == 7 || type == 8))) {      /* our own AUD/SPS/PPS already written */
                buf_put(&pes, "\0\0\0\1", 4); buf_put(&pes, p, nl);
            }
            p += nl;
        }
        ts_packets(out, PID_V, &st->cc_v, pes.p, pes.n, dts - 9000, vs[i].key);
    }
    buf_free(&pes);
}
