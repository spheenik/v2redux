// mathcheck -- verifies the owned transcendentals (v2math.h) against the
// genuine x87 kernels at PC=24, the same precision regime the original synths
// run in (task 1.3 of portable-version-native-player).
//
// The x87 REFERENCE code below is test-only (x86 hosts); the portable library
// itself contains no asm. On non-x86 hosts this test is skipped -- the
// portable functions are fixed C++, so passing on one host proves the values
// for all hosts (that's the point of owning them).
//
// Pass criteria (design D4): float-bit equality except rounding-tie flips,
// which must be rare (tallied and reported; expected ~0 per millions).
// fsin comparison is informational only: the hardware fsin is itself
// CPU-vendor-dependent, so the portable sinf24 is a *reference*, not a clone.

#include "../src/v2math.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <initializer_list>

#if defined(__i386__) || defined(__x86_64__)
#define HAVE_X87 1
#else
#define HAVE_X87 0
#endif

using namespace v2redux;

static uint32_t f2u(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }

// deterministic LCG for test point generation
static uint64_t g_rng = 1;
static uint32_t rnd32() { g_rng = g_rng * 6364136223846793005ull + 1442695040888963407ull; return (uint32_t)(g_rng >> 32); }
static float rndRange(float lo, float hi) { return lo + (hi - lo) * (rnd32() * (1.0f / 4294967296.0f)); }

#if HAVE_X87
static void setPc24()
{
  uint16_t cw;
  __asm__ __volatile__("fnstcw %0" : "=m"(cw));
  cw = (uint16_t)(cw & 0xF0FFu);   // PC=00 -> 24-bit significand, keep RC/masks
  __asm__ __volatile__("fldcw %0" : : "m"(cw));
}

static float x87_pow2(float y)
{
  float r;
  __asm__("fld1\n\t"
          "fld %%st(1)\n\t"
          "fprem\n\t"
          "f2xm1\n\t"
          "faddp %%st,%%st(1)\n\t"
          "fscale\n\t"
          "fstp %%st(1)\n\t"
          : "=t"(r) : "0"(y));
  return r;
}
static float x87_pow(float base, float e)
{
  float r;
  __asm__("fyl2x\n\t"
          "fld1\n\t fld %%st(1)\n\t fprem\n\t f2xm1\n\t"
          "faddp %%st,%%st(1)\n\t fscale\n\t fstp %%st(1)\n\t"
          : "=t"(r) : "0"(base), "u"(e) : "st(1)");
  return r;
}
static const float kFci12ref = 0.083333333333f;
static int32_t x87_oscfreq(float pno, float base)
{
  int32_t r;
  __asm__("fmuls %2\n\t"
          "fld1\n\t"
          "fld %%st(1)\n\t"
          "fprem\n\t"
          "f2xm1\n\t"
          "faddp %%st,%%st(1)\n\t"
          "fscale\n\t"
          "fstp %%st(1)\n\t"
          "fmuls %3\n\t"
          "fistpl %0\n\t"
          : "=m"(r) : "t"(pno), "m"(kFci12ref), "m"(base) : "st");
  return r;
}
static float x87_atan(float x)
{
  float r;
  __asm__("fld1\n\t fpatan\n\t" : "=t"(r) : "0"(x));
  return r;
}
static float x87_odgain2(float p1g, float gain1)
{
  float r;
  __asm__("fld1\n\t fpatan\n\t fdivrp %%st,%%st(1)\n\t"
          : "=t"(r) : "0"(gain1), "u"(p1g));
  return r;
}
static float x87_fsin(float x)
{
  float r;
  __asm__("fsin" : "=t"(r) : "0"(x));
  return r;
}
#endif // HAVE_X87

struct Tally {
  const char *name;
  long n = 0, mism = 0;
  int maxUlp = 0;
  float worstIn0 = 0, worstIn1 = 0;
  void note(float in0, float in1, float ref, float got)
  {
    n++;
    if (f2u(ref) == f2u(got)) return;
    mism++;
    int ulp = (int)llabs((long long)(int32_t)f2u(ref) - (long long)(int32_t)f2u(got));
    if (ulp > maxUlp) { maxUlp = ulp; worstIn0 = in0; worstIn1 = in1; }
  }
  void report() const
  {
    printf("%-12s n=%-9ld mismatch=%-7ld (%.2e)  maxUlp=%d",
           name, n, mism, n ? (double)mism / n : 0.0, maxUlp);
    if (mism) printf("  worst at (%.9g, %.9g)", worstIn0, worstIn1);
    printf("\n");
  }
};

int main()
{
#if !HAVE_X87
  printf("mathcheck: non-x86 host, x87 reference unavailable -- skipped.\n"
         "(portable functions are fixed C++; verify on an x86 host.)\n");
  return 0;
#else
  setPc24();

  // ---- pow2f: grid + randoms over the synth's exponent range
  Tally tPow2{"pow2f"};
  for (long i = 0; i < 4000000; i++) {
    float y = rndRange(-32.0f, 32.0f);
    tPow2.note(y, 0, x87_pow2(y), vm::pow2f(y));
  }
  for (float y = -20.0f; y <= 20.0f; y += 0.0001f)  // dense deterministic grid
    tPow2.note(y, 0, x87_pow2(y), vm::pow2f(y));
  tPow2.report();

  // ---- oscfreqi: the fistp tie-sensitive path. v0 base const + a modern one.
  Tally tFreq{"oscfreqi"};
  const float bases[2] = { 3185015.0f, 12740060.0f };
  for (int b = 0; b < 2; b++) {
    for (long i = 0; i < 4000000; i++) {
      float pno = rndRange(-128.0f, 128.0f);
      int32_t ref = x87_oscfreq(pno, bases[b]);
      int32_t got = vm::oscfreqi(pno, bases[b]);
      tFreq.n++;
      if (ref != got) {
        tFreq.mism++;
        if (abs(ref - got) > tFreq.maxUlp) {
          tFreq.maxUlp = abs(ref - got);
          tFreq.worstIn0 = pno; tFreq.worstIn1 = bases[b];
        }
      }
    }
    // semitone/cent grid (corpus-realistic pitches)
    for (int cents = -64 * 128; cents <= 64 * 128; cents++) {
      float pno = (float)cents * (1.0f / 128.0f);
      int32_t ref = x87_oscfreq(pno, bases[b]);
      int32_t got = vm::oscfreqi(pno, bases[b]);
      tFreq.n++;
      if (ref != got) tFreq.mism++;
    }
  }
  tFreq.report();

  // ---- powf24 (env susmul / reverb gain decay domain)
  Tally tPow{"powf24"};
  for (long i = 0; i < 2000000; i++) {
    float base = rndRange(1e-4f, 4.0f);
    float e = rndRange(-32.0f, 32.0f);
    tPow.note(base, e, x87_pow(base, e), vm::powf24(base, e));
  }
  tPow.report();

  // ---- atanf24 (v0 waveshaper, per-sample domain)
  Tally tAtan{"atanf24"};
  for (long i = 0; i < 4000000; i++) {
    float x = rndRange(-64.0f, 64.0f);
    tAtan.note(x, 0, x87_atan(x), vm::atanf24(x));
  }
  for (float x = -4.0f; x <= 4.0f; x += 1e-5f)
    tAtan.note(x, 0, x87_atan(x), vm::atanf24(x));
  tAtan.report();

  // ---- odGain2 (overdrive setup, both eras)
  Tally tOd{"odGain2"};
  for (long i = 0; i < 2000000; i++) {
    float p1g = rndRange(0.0f, 1.0f);
    float g1 = rndRange(1e-3f, 64.0f);
    tOd.note(p1g, g1, x87_odgain2(p1g, g1), vm::odGain2(p1g, g1));
  }
  tOd.report();

  // ---- sinf24 vs hardware fsin: INFORMATIONAL (fsin is vendor-dependent)
  Tally tSin{"sinf24*"};
  for (long i = 0; i < 4000000; i++) {
    float x = rndRange(-8.0f, 8.0f);
    tSin.note(x, 0, x87_fsin(x), vm::sinf24(x));
  }
  tSin.report();
  printf("(* sinf24 row is informational: hardware fsin is itself "
         "CPU-vendor-dependent; ~1-ulp diffs expected)\n");

  // pass/fail: the deterministic kernels must be tie-rare (< 1e-5 mismatch)
  bool ok = true;
  for (const Tally *t : { &tPow2, &tFreq, &tPow, &tAtan, &tOd })
    if (t->n && (double)t->mism / t->n > 1e-5) ok = false;
  printf(ok ? "PASS\n" : "FAIL\n");
  return ok ? 0 : 1;
#endif
}
