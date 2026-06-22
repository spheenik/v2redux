// portrender -- render a v2m through the v2redux port at a named era profile,
// to raw interleaved stereo float32 (the lab's .f32 format), for oracle A/B.
//
//   portrender <file.v2m> <out.f32> <seconds> [era]
//
// era (default auto): auto | fr08 | flybye | fr014 | fr019 | candytron |
//                     kkrieger2004   -- see eras:: in v2eras.h. The named
// profiles are needed where the format version under-resolves the build
// (kkrieger2004 = v5 file, near-v6 FM-float engine).
#include "v2redux.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using namespace v2redux;

static Era pickEra(const char *name)
{
  if (!strcmp(name, "fr08"))         return eras::fr08;
  if (!strcmp(name, "flybye"))       return eras::flybye;
  if (!strcmp(name, "fr014"))        return eras::fr014;
  if (!strcmp(name, "fr019"))        return eras::fr019;
  if (!strcmp(name, "candytron"))    return eras::candytron;
  if (!strcmp(name, "kkrieger2004")) return eras::kkrieger2004;
  return Era::Auto();
}

int main(int argc, char **argv)
{
  if (argc < 4) {
    fprintf(stderr, "usage: portrender <file.v2m> <out.f32> <seconds> [era]\n");
    return 1;
  }
  const char *in = argv[1], *out = argv[2];
  long total = (long)(atof(argv[3]) * 44100.0);
  Era era = (argc > 4) ? pickEra(argv[4]) : Era::Auto();

  FILE *f = fopen(in, "rb");
  if (!f) { fprintf(stderr, "cannot open %s\n", in); return 1; }
  fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
  void *data = malloc((size_t)len);
  if (!data || fread(data, 1, (size_t)len, f) != (size_t)len) {
    fprintf(stderr, "short read\n"); return 2;
  }
  fclose(f);

  Player p;
  Result r = p.open(data, (size_t)len, era);
  free(data);
  if (r != Result::OK) {
    fprintf(stderr, "%s: open failed (result %d, v%d)\n", in, (int)r, p.fileVersion());
    return 3;
  }
  p.play(0);

  FILE *o = fopen(out, "wb");
  if (!o) { fprintf(stderr, "cannot write %s\n", out); return 1; }
  static float buf[2 * 4096];
  for (long done = 0; done < total; ) {
    long n = total - done < 4096 ? total - done : 4096;
    p.render(buf, (uint32_t)n);
    fwrite(buf, 2 * sizeof(float), (size_t)n, o);
    done += n;
  }
  fclose(o);
  return 0;
}
