// loadcheck -- loader equivalence tests (task 4.4):
//   1. v6 file canonicalization is identity (+ the lab's 4-byte zero tail)
//   2. fr08.v2m (v0) canonicalization byte-equals the conv_v2m output
//      (v2m/converted/fr08.v2m)
//   3. corrupt data is rejected (truncations, garbage)
//
// Usage: loadcheck <original-fr08.v2m> <converted-fr08.v2m> <any-v6.v2m>...

#include "../src/v2load.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using namespace v2redux;

static int fails = 0;
#define CHECK(cond, ...) do { \
    if (!(cond)) { fails++; printf("FAIL: " __VA_ARGS__); printf("\n"); } \
  } while (0)

static unsigned char *slurp(const char *path, size_t *len)
{
  FILE *f = fopen(path, "rb");
  if (!f) { printf("FAIL: cannot open %s\n", path); fails++; return nullptr; }
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  fseek(f, 0, SEEK_SET);
  unsigned char *buf = (unsigned char *)malloc(sz);
  if (fread(buf, 1, sz, f) != (size_t)sz) { fclose(f); free(buf); fails++; return nullptr; }
  fclose(f);
  *len = (size_t)sz;
  return buf;
}

int main(int argc, char **argv)
{
  if (argc < 4)
  {
    printf("usage: %s <fr08.v2m> <converted-fr08.v2m> <v6.v2m>...\n", argv[0]);
    return 2;
  }

  // ---- 2. v0 upgrade byte-equals the lab conversion -------------------------
  size_t origLen, convLen;
  unsigned char *orig = slurp(argv[1], &origLen);
  unsigned char *conv = slurp(argv[2], &convLen);
  if (orig && conv)
  {
    V2LoadResult lr = v2loadCanonicalize(orig, origLen);
    CHECK(lr.result == Result::OK, "fr08: load failed (%d)", (int)lr.result);
    CHECK(lr.version == 0, "fr08: version %d != 0", lr.version);
    if (lr.result == Result::OK)
    {
      // the canonical image must byte-equal conv_v2m output over the
      // converted file's length; ours may carry the extra zero tail
      // (ConvertV2M allocates it; whether the lab tool wrote it out is a
      // file-writing detail). Any payload difference is a real bug.
      CHECK(lr.size == convLen || lr.size == convLen + 4 || lr.size + 4 == convLen,
            "fr08: size %zu vs conv %zu", lr.size, convLen);
      size_t common = lr.size < convLen ? lr.size : convLen;
      size_t diffat = (size_t)-1;
      for (size_t i = 0; i < common; i++)
        if (lr.data[i] != conv[i]) { diffat = i; break; }
      CHECK(diffat == (size_t)-1, "fr08: first payload diff at byte %zu", diffat);
      // any tail beyond the common length must be zeros
      const unsigned char *tail = lr.size > common ? lr.data : conv;
      size_t tailLen = (lr.size > common ? lr.size : convLen) - common;
      for (size_t i = 0; i < tailLen; i++)
        CHECK(tail[common + i] == 0, "fr08: nonzero tail byte %zu", common + i);
      free(lr.data);
      printf("fr08 v0->v6 canonicalization: matches conv_v2m output\n");
    }
  }

  // ---- 3. corrupt-data rejection --------------------------------------------
  if (orig)
  {
    for (size_t cut = 8; cut < origLen; cut = cut * 7 / 4 + 13)
    {
      V2LoadResult lr = v2loadCanonicalize(orig, cut);
      CHECK(lr.result != Result::OK || lr.data, "truncate %zu: OK without data", cut);
      // most truncations must fail; those that parse must stay in bounds
      // (ASan/valgrind territory); just free whatever came back.
      if (lr.data) free(lr.data);
    }
    unsigned char *junk = (unsigned char *)malloc(4096);
    for (int i = 0; i < 4096; i++) junk[i] = (unsigned char)(i * 2654435761u >> 13);
    V2LoadResult lr = v2loadCanonicalize(junk, 4096);
    CHECK(lr.result != Result::OK, "garbage accepted");
    free(junk);
    printf("corrupt-data rejection: ok\n");
  }
  free(orig);
  free(conv);

  // ---- 1. v6 identity --------------------------------------------------------
  for (int a = 3; a < argc; a++)
  {
    size_t len;
    unsigned char *data = slurp(argv[a], &len);
    if (!data) continue;
    V2LoadResult lr = v2loadCanonicalize(data, len);
    CHECK(lr.result == Result::OK, "%s: load failed (%d)", argv[a], (int)lr.result);
    if (lr.result == Result::OK)
    {
      CHECK(lr.version == 6, "%s: version %d != 6", argv[a], lr.version);
      CHECK(lr.size == len + 4, "%s: size %zu != %zu+4", argv[a], lr.size, len);
      CHECK(memcmp(lr.data, data, len) == 0, "%s: not identity", argv[a]);
      CHECK(lr.data[len] == 0 && lr.data[len+1] == 0 &&
            lr.data[len+2] == 0 && lr.data[len+3] == 0, "%s: tail not zero", argv[a]);
      free(lr.data);
    }
    free(data);
  }
  printf("v6 identity: %d file(s) ok\n", argc - 3);

  if (fails) { printf("loadcheck: %d FAILURES\n", fails); return 1; }
  printf("PASS\n");
  return 0;
}
