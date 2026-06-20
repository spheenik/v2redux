// forcecheck -- research-knob verification (task 5.2):
//   1. a v6 file opened with forceBehaviorVersion=0 plays with v0 engine
//      semantics (output differs from the normal v6 render; deterministic)
//   2. out-of-range forceBehaviorVersion requests error (UnsupportedVersion)
//   3. force == detected version is a no-op (bit-identical)
//
// Usage: forcecheck <any-v6.v2m>

#include "../src/v2redux.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using namespace v2redux;

static int fails = 0;
#define CHECK(cond, ...) do { \
    if (!(cond)) { fails++; printf("FAIL: " __VA_ARGS__); printf("\n"); } \
  } while (0)

static const uint32_t kFrames = 44100 * 5; // 5 seconds

static float *renderOpened(Player &pl)
{
  float *buf = (float *)malloc(sizeof(float) * 2 * kFrames);
  pl.play(0);
  pl.render(buf, kFrames);
  return buf;
}

int main(int argc, char **argv)
{
  if (argc != 2) { printf("usage: %s <v6.v2m>\n", argv[0]); return 2; }

  FILE *f = fopen(argv[1], "rb");
  if (!f) { printf("cannot open %s\n", argv[1]); return 2; }
  fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
  unsigned char *data = (unsigned char *)malloc(len);
  if (fread(data, 1, len, f) != (size_t)len) return 2;
  fclose(f);

  // normal open: detected version drives behavior
  Player p1;
  CHECK(p1.open(data, len) == Result::OK, "normal open failed");
  CHECK(p1.fileVersion() == 6, "detected version %d != 6", p1.fileVersion());
  float *ref = renderOpened(p1);

  // force == detected: must be bit-identical
  Player p2;
  CHECK(p2.open(data, len, 6) == Result::OK, "force=6 open failed");
  float *same = renderOpened(p2);
  CHECK(memcmp(ref, same, sizeof(float) * 2 * kFrames) == 0,
        "force=6 render differs from normal");

  // force = 0 on a v6 file: v0 engine semantics -- must differ (the era
  // gates flip: env curves, frame size, osc algorithm, noise LCG, ...)
#if V2_VER_MIN <= 0
  Player p3;
  CHECK(p3.open(data, len, 0) == Result::OK, "force=0 open failed");
  CHECK(p3.fileVersion() == 6, "force=0 must not change detected version");
  float *v0 = renderOpened(p3);
  CHECK(memcmp(ref, v0, sizeof(float) * 2 * kFrames) != 0,
        "force=0 render identical to v6 -- era gates not applied?");
  // determinism: forcing again gives the same bits
  Player p4;
  CHECK(p4.open(data, len, 0) == Result::OK, "force=0 reopen failed");
  float *v0b = renderOpened(p4);
  CHECK(memcmp(v0, v0b, sizeof(float) * 2 * kFrames) == 0,
        "force=0 render not deterministic");
  free(v0); free(v0b);
#endif

  // out-of-range requests error and never open
  Player p5;
  CHECK(p5.open(data, len, V2_VER_MAX + 1) == Result::UnsupportedVersion,
        "force=%d (beyond max) did not error", V2_VER_MAX + 1);
  CHECK(p5.open(data, len, 99) == Result::UnsupportedVersion,
        "force=99 did not error");

  free(ref); free(same); free(data);
  if (fails) { printf("forcecheck: %d FAILURES\n", fails); return 1; }
  printf("PASS: forceBehaviorVersion works both ways\n");
  return 0;
}
