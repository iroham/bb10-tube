/* jsrun: QuickJS host for the native app. Evaluates the script files given as arguments in one context, in order,
 * and prints the string value of the last one (or the exception) on stdout. `print(...)` writes a line to stderr.
 * The app runs it as a short-lived helper process for YouTube's player script (signature and "n" transforms):
 * no engine stays in memory between videos. `readFile(path)` returns a file's text (the cached player script). Exit code 0 = value printed, 1 = exception, 2 = usage/IO. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "quickjs.h"

static JSValue js_print(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    for (int i = 0; i < argc; i++) {
        const char *s = JS_ToCString(ctx, argv[i]);
        if (!s) return JS_EXCEPTION;
        fprintf(stderr, "%s%s", i ? " " : "", s);
        JS_FreeCString(ctx, s);
    }
    fputc('\n', stderr);
    return JS_UNDEFINED;
}

static char *slurp(const char *path, size_t *len);
static JSValue js_read_file(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    const char *path = argc > 0 ? JS_ToCString(ctx, argv[0]) : NULL;
    if (!path) return JS_EXCEPTION;
    size_t len = 0;
    char *b = slurp(path, &len);
    JSValue v = b ? JS_NewStringLen(ctx, b, len) : JS_ThrowReferenceError(ctx, "cannot read %s", path);
    JS_FreeCString(ctx, path);
    free(b);
    return v;
}

static char *slurp(const char *path, size_t *len) {
    FILE *f = strcmp(path, "-") ? fopen(path, "rb") : stdin;
    if (!f) return NULL;
    size_t cap = 1 << 16, n = 0, r;
    char *b = malloc(cap + 1);
    while (b && (r = fread(b + n, 1, cap - n, f)) > 0) {
        n += r;
        if (n == cap) { cap *= 2; b = realloc(b, cap + 1); }
    }
    if (f != stdin) fclose(f);
    if (b) b[n] = 0;
    *len = n;
    return b;
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: jsrun [--out=file] script.js [more.js ...] ('-' = stdin)\n"); return 2; }
    /* --out=file: write the result there instead of stdout (QNX's posix_spawn does not redirect for the caller) */
    if (!strncmp(argv[1], "--out=", 6)) {
        if (!freopen(argv[1] + 6, "w", stdout)) { fprintf(stderr, "jsrun: cannot write %s\n", argv[1] + 6); return 2; }
        argv++; argc--;
        if (argc < 2) return 2;
    }
    JSRuntime *rt = JS_NewRuntime();
    /* YouTube's player script is ~2.5 MB of source; the solver parses it into a syntax tree. JSRUN_MEM_MB caps
     * the heap (an exceeded cap shows as a thrown null), JSRUN_STATS=1 prints the peak to stderr. */
    const char *mb = getenv("JSRUN_MEM_MB");
    JS_SetMemoryLimit(rt, (size_t)(mb ? atoi(mb) : 512) << 20);
    JS_SetMaxStackSize(rt, 4u << 20);
    JSContext *ctx = JS_NewContext(rt);
    JSValue g = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, g, "print", JS_NewCFunction(ctx, js_print, "print", 1));
    JS_SetPropertyStr(ctx, g, "readFile", JS_NewCFunction(ctx, js_read_file, "readFile", 1));
    JS_FreeValue(ctx, g);
    int rc = 0;
    for (int i = 1; i < argc; i++) {
        size_t len;
        char *src = slurp(argv[i], &len);
        if (!src) { fprintf(stderr, "jsrun: cannot read %s\n", argv[i]); return 2; }
        JSValue v = JS_Eval(ctx, src, len, argv[i], JS_EVAL_TYPE_GLOBAL);
        free(src);
        if (JS_IsException(v)) {
            JSValue e = JS_GetException(ctx);
            const char *s = JS_ToCString(ctx, e);
            printf("EXCEPTION %s: %s\n", argv[i], s ? s : "?");
            if (s) JS_FreeCString(ctx, s);
            JS_FreeValue(ctx, e);
            rc = 1;
            break;
        }
        if (i == argc - 1) {
            const char *s = JS_ToCString(ctx, v);
            if (s) { puts(s); JS_FreeCString(ctx, s); }
        }
        JS_FreeValue(ctx, v);
    }
    if (getenv("JSRUN_STATS")) {
        JSMemoryUsage u;
        JS_ComputeMemoryUsage(rt, &u);
        fprintf(stderr, "jsrun: heap now %lld KB (limit %lld KB)\n", (long long)u.malloc_size / 1024, (long long)u.malloc_limit / 1024);
    }
    fflush(stdout);
    return rc;   /* the process ends here: no teardown cost */
}
