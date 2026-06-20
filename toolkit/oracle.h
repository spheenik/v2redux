// oracle.h -- shared C scaffold for period-binary oracle harnesses.
//
// A harness #includes this, supplies its own per-binary config (IMG_SIZE, entry/
// init/render VAs, rdtsc sites) and its own driving strategy (call the binary's
// in-image player like c1_flybye, OR a ported source player + synth-only calls like
// c2_oracle), and gets the process scaffold common to every harness:
//
//   oracle_map_image(path, size)        map the unpacked image at 0x400000, load it
//   oracle_install_faults()             SIGSEGV/SIGBUS/SIGILL reporter (eip + bytes)
//   oracle_pin_rdtsc(sites, n)          patch each `0f31` rdtsc -> `31c0` (seed 0)
//   oracle_write_f32(path, buf, frames) write interleaved stereo float32
//   ORACLE_F32_OPEN / ORACLE_F32_CHUNK  streaming variant for chunked render loops
//
// NATIVE BY DESIGN -- never run under Unicorn. The V2 audio path is x87
// transcendental-heavy (fsin/fpatan/f2xm1); the host hardware x87 reproduces the
// period Pentium's 80-bit results bit-for-bit (max|d|=0), whereas QEMU/Unicorn
// rounds the bottom ~11 mantissa bits (libm-at-double). See change design.md (D3).
//
// Build a harness:  gcc -m32 -no-pie -O0 harness.c -o harness

#ifndef ORACLE_H
#define ORACLE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/mman.h>
#define __USE_GNU 1
#include <signal.h>
#include <ucontext.h>
#include <unistd.h>

#ifndef ORACLE_IMG_BASE
#define ORACLE_IMG_BASE 0x400000u
#endif

// --- fault reporter: report eip + the faulting bytes, then exit -------------
// The image range is reported if the harness defines ORACLE_IMG_SIZE.
static void oracle_segv(int sig, siginfo_t *si, void *uc_)
{
    ucontext_t *uc = (ucontext_t *)uc_;
    unsigned eip = uc->uc_mcontext.gregs[REG_EIP];
    unsigned esp = uc->uc_mcontext.gregs[REG_ESP];
    fprintf(stderr, "[oracle] SIG%d fault=%p eip=0x%08x esp=0x%08x\n",
            sig, si->si_addr, eip, esp);
#ifdef ORACLE_IMG_SIZE
    if (eip >= ORACLE_IMG_BASE && eip < ORACLE_IMG_BASE + (unsigned)ORACLE_IMG_SIZE) {
        unsigned char *p = (unsigned char *)(uintptr_t)eip;
        fprintf(stderr, "[oracle]   bytes: %02x %02x %02x %02x %02x %02x\n",
                p[0], p[1], p[2], p[3], p[4], p[5]);
    }
#endif
    _exit(42);
}

static void oracle_install_faults(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = oracle_segv;
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);
    sigaction(SIGILL, &sa, NULL);
}

// --- map the unpacked image at its native base and load it ------------------
// Returns the number of bytes read. Exits on mmap failure.
static size_t oracle_map_image(const char *path, size_t img_size)
{
    void *p = mmap((void *)(uintptr_t)ORACLE_IMG_BASE, img_size,
                   PROT_READ | PROT_WRITE | PROT_EXEC,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (p != (void *)(uintptr_t)ORACLE_IMG_BASE) {
        fprintf(stderr, "[oracle] mmap @0x%x failed (got %p)\n", ORACLE_IMG_BASE, p);
        _exit(1);
    }
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "[oracle] cannot open %s\n", path);
        _exit(1);
    }
    size_t n = fread((void *)(uintptr_t)ORACLE_IMG_BASE, 1, img_size, f);
    fclose(f);
    fprintf(stderr, "[oracle] mapped %zu bytes from %s at 0x%x\n", n, path, ORACLE_IMG_BASE);
    return n;
}

// --- pin rdtsc seed sites to 0: `0f31` -> `31c0` (xor eax,eax) ---------------
static void oracle_pin_rdtsc(const uint32_t *sites, unsigned count)
{
    for (unsigned i = 0; i < count; i++) {
        uint8_t *s = (uint8_t *)(uintptr_t)sites[i];
        if (s[0] == 0x0f && s[1] == 0x31) {
            s[0] = 0x31;
            s[1] = 0xc0;
        } else {
            fprintf(stderr, "[oracle] WARN no rdtsc @0x%x (%02x %02x)\n",
                    sites[i], s[0], s[1]);
        }
    }
    if (count)
        fprintf(stderr, "[oracle] %u rdtsc site(s) pinned -> seed 0\n", count);
}

// --- write interleaved stereo float32 ---------------------------------------
static int oracle_write_f32(const char *path, const float *buf, size_t frames)
{
    FILE *out = fopen(path, "wb");
    if (!out) {
        fprintf(stderr, "[oracle] cannot open %s\n", path);
        return -1;
    }
    size_t w = fwrite(buf, 2 * sizeof(float), frames, out);
    fclose(out);
    fprintf(stderr, "[oracle] wrote %zu frames -> %s\n", w, path);
    return w == frames ? 0 : -1;
}

// streaming variant for chunked render loops:
//   FILE *o = ORACLE_F32_OPEN(path);
//   ... ORACLE_F32_CHUNK(o, buf, chunk); ...
//   fclose(o);
#define ORACLE_F32_OPEN(path)            fopen((path), "wb")
#define ORACLE_F32_CHUNK(o, buf, frames) fwrite((buf), 2 * sizeof(float), (frames), (o))

// ===========================================================================
// Tapping the loaded oracle: read live data at chosen positions in the running
// mapped image and stream it out. The POSITIONS (VAs, field offsets, which
// signal-chain buffer) are per-binary; these helpers are the shared mechanism,
// replacing each harness's own `rd32`, scattered `*(float*)(uintptr_t)va` casts,
// and `if(getenv("X")){fopen;...fwrite;...}` boilerplate.
//
// Example -- tap channel 7's oscillator buffer each render chunk:
//   oracle_tap osc = oracle_tap_open("C1_OSC", "/tmp/ch7.osc");
//   ... per chunk: oracle_tap_f32(&osc, oracle_buf(VA_OSCBUF), n);
//   oracle_tap_close(&osc);
// Example -- a scalar state probe via a workspace pointer (the RTAP idiom):
//   float voicing = oracle_field_f32(0x6a88f8u, 0x54);  // *(ws)+0x54
// ---------------------------------------------------------------------------

// --- typed live reads of the running image at a virtual address -------------
static inline uint32_t oracle_u32(uint32_t va) { return *(volatile uint32_t *)(uintptr_t)va; }
static inline int32_t  oracle_i32(uint32_t va) { return *(volatile int32_t  *)(uintptr_t)va; }
static inline float    oracle_f32_at(uint32_t va) { return *(volatile float *)(uintptr_t)va; }
static inline void    *oracle_ptr(uint32_t va) { return (void *)(uintptr_t)oracle_u32(va); }
// a float buffer located AT va, vs one POINTED-TO BY the slot at va:
static inline const float *oracle_buf(uint32_t va)    { return (const float *)(uintptr_t)va; }
static inline const float *oracle_bufptr(uint32_t va) { return *(const float *const *)(uintptr_t)va; }
// pointer-indirect field read: dereference the slot at base_va, then read off it.
// Returns 0 if the workspace pointer is null (uninitialized) -- matches the taps'
// `if(ws){ ... }` guards.
static inline uint32_t oracle_field_u32(uint32_t base_va, uint32_t off) {
    uint32_t p = oracle_u32(base_va);
    return p ? *(volatile uint32_t *)(uintptr_t)(p + off) : 0u;
}
static inline float oracle_field_f32(uint32_t base_va, uint32_t off) {
    uint32_t p = oracle_u32(base_va);
    return p ? *(volatile float *)(uintptr_t)(p + off) : 0.0f;
}

// --- env-gated tap sink: a no-op unless its env var is set ------------------
typedef struct { FILE *fp; } oracle_tap;

static oracle_tap oracle_tap_open(const char *env_var, const char *path) {
    oracle_tap t;
    t.fp = getenv(env_var) ? fopen(path, "wb") : NULL;
    if (getenv(env_var) && !t.fp)
        fprintf(stderr, "[oracle] tap %s: cannot open %s\n", env_var, path);
    return t;
}
static inline int  oracle_tap_on(const oracle_tap *t)                         { return t->fp != NULL; }
static inline void oracle_tap_bytes(oracle_tap *t, const void *p, size_t n)   { if (t->fp) fwrite(p, 1, n, t->fp); }
static inline void oracle_tap_f32(oracle_tap *t, const float *b, size_t n)    { if (t->fp) fwrite(b, sizeof(float), n, t->fp); }
static inline void oracle_tap_va(oracle_tap *t, uint32_t va, size_t bytes)    { if (t->fp) fwrite((const void *)(uintptr_t)va, 1, bytes, t->fp); }
static inline void oracle_tap_close(oracle_tap *t)                            { if (t->fp) { fclose(t->fp); t->fp = NULL; } }

// --- signal-chain tap TABLE: the open/reset/dump/close lifecycle ------------
// Dumping several DSP-chain points each render chunk (osc/flt/dist/chan/premix)
// is the same four steps per tap -- only the ACCUMULATE point (where the live
// buffer feeds the accumulator) is per-binary, because that sits in the render
// control flow. Declare a table of descriptors; the lifecycle is shared:
//
//   static float a_osc[320], a_flt[320], a_premix[640];
//   static oracle_chan_tap T[] = {
//       { ".osc",    0, a_osc,    NULL },   // 0 = mono, 1 = stereo
//       { ".flt",    0, a_flt,    NULL },
//       { ".premix", 1, a_premix, NULL },
//   };
//   enum { NT = sizeof T / sizeof T[0] };
//   oracle_taps_open(T, NT, getenv("C1_VTRACE"));     // once; no-op if env unset
//   ... per chunk:
//     oracle_taps_reset(T, NT, n);
//     ... at the osc point:  oracle_tap_add(&T[0], oracle_buf(VA_OSC), n);
//     ... at the premix pt:  oracle_tap_add(&T[2], oracle_bufptr(VA_OUTPTR), n);
//     oracle_taps_dump(T, NT, n);
//   oracle_taps_close(T, NT);                          // once
typedef struct {
    const char *suffix;   // file is "<prefix><suffix>", e.g. ".osc"
    int         stereo;   // 0 = 1 float/frame, 1 = 2 floats/frame
    float      *acc;      // accumulator (>= max_frames * (stereo?2:1) floats)
    FILE       *fp;       // opened by oracle_taps_open iff prefix non-NULL
} oracle_chan_tap;

static inline size_t oracle__tap_w(const oracle_chan_tap *t) { return t->stereo ? 2 : 1; }

static void oracle_taps_open(oracle_chan_tap *t, int n, const char *prefix) {
    char p[1024];
    if (!prefix) return;
    for (int i = 0; i < n; i++) {
        snprintf(p, sizeof p, "%s%s", prefix, t[i].suffix);
        t[i].fp = fopen(p, "wb");
        if (!t[i].fp) fprintf(stderr, "[oracle] tap: cannot open %s\n", p);
    }
}
static void oracle_taps_reset(oracle_chan_tap *t, int n, size_t frames) {
    for (int i = 0; i < n; i++)
        if (t[i].fp) memset(t[i].acc, 0, frames * oracle__tap_w(&t[i]) * sizeof(float));
}
// accumulate `frames` worth of `src` into this tap (the per-binary tap point)
static inline void oracle_tap_add(oracle_chan_tap *t, const float *src, size_t frames) {
    if (!t->fp) return;
    size_t k = frames * oracle__tap_w(t);
    for (size_t i = 0; i < k; i++) t->acc[i] += src[i];
}
static void oracle_taps_dump(oracle_chan_tap *t, int n, size_t frames) {
    for (int i = 0; i < n; i++)
        if (t[i].fp) fwrite(t[i].acc, sizeof(float) * oracle__tap_w(&t[i]), frames, t[i].fp);
}
static void oracle_taps_close(oracle_chan_tap *t, int n) {
    for (int i = 0; i < n; i++)
        if (t[i].fp) { fclose(t[i].fp); t[i].fp = NULL; }
}

#endif // ORACLE_H
