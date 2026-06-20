// Two concurrent Player instances must not interact (portable-player-api
// spec): interleaved renders of two different songs must equal each song's
// solo render bit-for-bit. Also: render before open outputs silence.

#include "../src/v2redux.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

static std::vector<uint8_t> slurp(const char *p)
{
  FILE *f = fopen(p, "rb");
  if (!f) { fprintf(stderr, "cannot open %s\n", p); exit(1); }
  fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
  std::vector<uint8_t> v((size_t)n);
  if (fread(v.data(), 1, v.size(), f) != v.size()) exit(1);
  fclose(f);
  return v;
}

static std::vector<float> renderSolo(const std::vector<uint8_t> &song, uint32_t frames)
{
  v2redux::Player p;
  if (p.open(song.data(), song.size()) != v2redux::Result::OK) exit(2);
  p.play(0);
  std::vector<float> out(2 * frames);
  p.render(out.data(), frames);
  return out;
}

int main(int argc, char **argv)
{
  const char *fa = argc > 1 ? argv[1] : "corpus/pzero_new.v2m";          // genuine v6
  const char *fb = argc > 2 ? argv[2] : "corpus/v2_zeitmaschine_new.v2m"; // genuine v6
  const uint32_t FRAMES = 44100 * 3, STEP = 777; // odd step crosses frames

  // render-before-open: silence, no crash
  {
    v2redux::Player p;
    std::vector<float> buf(2 * 1024, 1.0f);
    p.render(buf.data(), 1024);
    for (float v : buf)
      if (v != 0.0f) { printf("FAIL: render-before-open not silent\n"); return 1; }
  }

  std::vector<uint8_t> a = slurp(fa), b = slurp(fb);
  std::vector<float> soloA = renderSolo(a, FRAMES), soloB = renderSolo(b, FRAMES);

  // interleaved
  v2redux::Player pa, pb;
  if (pa.open(a.data(), a.size()) != v2redux::Result::OK) return 2;
  if (pb.open(b.data(), b.size()) != v2redux::Result::OK) return 2;
  pa.play(0); pb.play(0);
  std::vector<float> ia(2 * FRAMES), ib(2 * FRAMES);
  for (uint32_t done = 0; done < FRAMES; ) {
    uint32_t n = FRAMES - done < STEP ? FRAMES - done : STEP;
    pa.render(&ia[2 * done], n);
    pb.render(&ib[2 * done], n);
    done += n;
  }

  bool ok = !memcmp(soloA.data(), ia.data(), soloA.size() * 4) &&
            !memcmp(soloB.data(), ib.data(), soloB.size() * 4);
  printf(ok ? "PASS: two instances independent, bit-identical to solo\n"
            : "FAIL: instance cross-talk\n");
  return ok ? 0 : 1;
}
