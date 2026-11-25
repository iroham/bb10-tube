/* util.c: buffers, logging, files, URL/base64 coding and a small JSON reader for the core. */
#include "core.h"
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

void buf_put(Buf *b, const void *p, size_t n) {
    if (b->n + n + 1 > b->cap) {
        size_t c = b->cap ? b->cap : 256;
        while (c < b->n + n + 1) c *= 2;
        b->p = realloc(b->p, c); b->cap = c;
        if (!b->p) abort();
    }
    if (n) memcpy(b->p + b->n, p, n);
    b->n += n;
}
void buf_str(Buf *b, const char *s) { buf_put(b, s, strlen(s)); }
void buf_byte(Buf *b, int c) { uint8_t x = (uint8_t)c; buf_put(b, &x, 1); }
void buf_zero(Buf *b) { buf_put(b, "", 0); b->p[b->n] = 0; }
void buf_free(Buf *b) { free(b->p); b->p = NULL; b->n = b->cap = 0; }
void buf_fmt(Buf *b, const char *fmt, ...) {
    char tmp[1024]; va_list ap;
    va_start(ap, fmt); int n = vsnprintf(tmp, sizeof tmp, fmt, ap); va_end(ap);
    if (n < 0) return;
    if ((size_t)n < sizeof tmp) { buf_put(b, tmp, (size_t)n); return; }
    char *big = malloc((size_t)n + 1);
    va_start(ap, fmt); vsnprintf(big, (size_t)n + 1, fmt, ap); va_end(ap);
    buf_put(b, big, (size_t)n); free(big);
}

int64_t now_ms(void) { struct timeval tv; gettimeofday(&tv, NULL); return (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000; }

void logf_(const char *fmt, ...) {
    char line[1024]; va_list ap;
    int64_t t = now_ms();
    int n = snprintf(line, sizeof line, "%lld.%03d ", (long long)(t / 1000 % 100000), (int)(t % 1000));
    va_start(ap, fmt); vsnprintf(line + n, sizeof line - (size_t)n - 1, fmt, ap); va_end(ap);
    size_t l = strlen(line); line[l++] = '\n';
    if (write(2, line, l) < 0) {}
}

char *read_file(const char *path, size_t *len) {
    int f = open(path, O_RDONLY);
    if (f < 0) return NULL;
    struct stat st;
    if (fstat(f, &st)) { close(f); return NULL; }
    char *p = malloc((size_t)st.st_size + 1); size_t got = 0;
    while (p && got < (size_t)st.st_size) { ssize_t r = read(f, p + got, (size_t)st.st_size - got); if (r <= 0) break; got += (size_t)r; }
    close(f);
    if (!p) return NULL;
    p[got] = 0; if (len) *len = got;
    return p;
}
int write_file(const char *path, const void *p, size_t n, int mode) {
    char tmp[1024];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    int f = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, mode);
    if (f < 0) return -1;
    const char *q = p; size_t left = n;
    while (left) { ssize_t w = write(f, q, left); if (w <= 0) { close(f); unlink(tmp); return -1; } q += w; left -= (size_t)w; }
    close(f);
    return rename(tmp, path);
}

void url_encode(Buf *out, const char *s, size_t n) {
    static const char hex[] = "0123456789ABCDEF"; size_t i;
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') buf_byte(out, c);
        else { buf_byte(out, '%'); buf_byte(out, hex[c >> 4]); buf_byte(out, hex[c & 15]); }
    }
}
static int hexv(int c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }
size_t url_decode(char *s) {
    char *o = s, *p = s;
    while (*p) {
        if (*p == '%' && hexv(p[1]) >= 0 && hexv(p[2]) >= 0) { *o++ = (char)(hexv(p[1]) << 4 | hexv(p[2])); p += 3; }
        else if (*p == '+') { *o++ = ' '; p++; }
        else *o++ = *p++;
    }
    *o = 0;
    return (size_t)(o - s);
}
int b64_decode(const char *s, size_t n, Buf *out) {
    uint32_t acc = 0; int bits = 0; size_t i;
    for (i = 0; i < n; i++) {
        int c = (unsigned char)s[i], v;
        if (c >= 'A' && c <= 'Z') v = c - 'A'; else if (c >= 'a' && c <= 'z') v = c - 'a' + 26; else if (c >= '0' && c <= '9') v = c - '0' + 52;
        else if (c == '+' || c == '-') v = 62; else if (c == '/' || c == '_') v = 63; else if (c == '=' || c == '\n' || c == '\r') continue; else return -1;
        acc = acc << 6 | (uint32_t)v; bits += 6;
        if (bits >= 8) { bits -= 8; buf_byte(out, (int)(acc >> bits & 0xff)); }
    }
    return 0;
}

/* ---- JSON ---- */
static const char *ws(const char *p) { while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') p++; return p; }
static const char *skip_str(const char *p) {            /* p at opening quote; returns after closing quote */
    for (p++; *p && *p != '"'; p++) if (*p == '\\' && p[1]) p++;
    return *p ? p + 1 : p;
}
static const char *skip_val(const char *p) {
    p = ws(p);
    if (*p == '"') return skip_str(p);
    if (*p == '{' || *p == '[') {
        int depth = 0;
        for (; *p; p++) {
            if (*p == '"') { p = skip_str(p) - 1; if (!p[1] && *p != '"') return p + 1; }
            else if (*p == '{' || *p == '[') depth++;
            else if (*p == '}' || *p == ']') { if (!--depth) return p + 1; }
        }
        return p;
    }
    while (*p && *p != ',' && *p != '}' && *p != ']' && *p != ' ' && *p != '\n' && *p != '\r' && *p != '\t') p++;
    return p;
}
const char *json_get(const char *json, const char *path, size_t *len) {
    const char *p = ws(json);
    while (*path) {
        const char *dot = strchr(path, '.'); size_t kl = dot ? (size_t)(dot - path) : strlen(path);
        if (*p == '{') {
            p = ws(p + 1);
            int found = 0;
            while (*p == '"') {
                const char *ke = skip_str(p);
                int match = (size_t)(ke - p - 2) == kl && !memcmp(p + 1, path, kl);
                p = ws(ke);
                if (*p != ':') return NULL;
                p = ws(p + 1);
                if (match) { found = 1; break; }
                p = ws(skip_val(p));
                if (*p == ',') p = ws(p + 1);
            }
            if (!found) return NULL;
        } else if (*p == '[') {
            long idx = strtol(path, NULL, 10), i;
            if (path[0] < '0' || path[0] > '9') return NULL;
            p = ws(p + 1);
            for (i = 0; i < idx; i++) {
                if (*p == ']' || !*p) return NULL;
                p = ws(skip_val(p));
                if (*p == ',') p = ws(p + 1);
            }
            if (*p == ']' || !*p) return NULL;
        } else return NULL;
        path += kl; if (*path == '.') path++;
    }
    if (len) *len = (size_t)(skip_val(p) - p);
    return p;
}
static void put_utf8(Buf *b, unsigned c) {
    if (c < 0x80) buf_byte(b, (int)c);
    else if (c < 0x800) { buf_byte(b, (int)(0xc0 | c >> 6)); buf_byte(b, (int)(0x80 | (c & 63))); }
    else if (c < 0x10000) { buf_byte(b, (int)(0xe0 | c >> 12)); buf_byte(b, (int)(0x80 | (c >> 6 & 63))); buf_byte(b, (int)(0x80 | (c & 63))); }
    else { buf_byte(b, (int)(0xf0 | c >> 18)); buf_byte(b, (int)(0x80 | (c >> 12 & 63))); buf_byte(b, (int)(0x80 | (c >> 6 & 63))); buf_byte(b, (int)(0x80 | (c & 63))); }
}
char *json_str(const char *json, const char *path) {
    size_t l; const char *p = json_get(json, path, &l);
    if (!p || *p != '"' || l < 2) return NULL;
    Buf b = {0}; const char *e = p + l - 1;
    for (p++; p < e; p++) {
        if (*p != '\\') { buf_byte(&b, *p); continue; }
        p++;
        switch (*p) {
        case 'n': buf_byte(&b, '\n'); break; case 't': buf_byte(&b, '\t'); break; case 'r': buf_byte(&b, '\r'); break;
        case 'b': buf_byte(&b, '\b'); break; case 'f': buf_byte(&b, '\f'); break;
        case 'u': {
            unsigned c = 0; int k;
            for (k = 1; k <= 4 && hexv(p[k]) >= 0; k++) c = c << 4 | (unsigned)hexv(p[k]);
            p += 4;
            if (c >= 0xd800 && c < 0xdc00 && p[1] == '\\' && p[2] == 'u') {
                unsigned d = 0;
                for (k = 3; k <= 6 && hexv(p[k]) >= 0; k++) d = d << 4 | (unsigned)hexv(p[k]);
                c = 0x10000 + ((c - 0xd800) << 10) + (d - 0xdc00); p += 6;
            }
            put_utf8(&b, c); break;
        }
        default: buf_byte(&b, *p);
        }
    }
    buf_zero(&b);
    return (char *)b.p;
}
long long json_num(const char *json, const char *path, long long dflt) {
    size_t l; const char *p = json_get(json, path, &l);
    if (!p) return dflt;
    if (*p == '"') p++;
    if ((*p < '0' || *p > '9') && *p != '-') return dflt;
    return strtoll(p, NULL, 10);
}
void json_escape(Buf *out, const char *s) {
    buf_byte(out, '"');
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') { buf_byte(out, '\\'); buf_byte(out, c); }
        else if (c == '\n') buf_str(out, "\\n");
        else if (c < 0x20) buf_fmt(out, "\\u%04x", c);
        else buf_byte(out, c);
    }
    buf_byte(out, '"');
}
