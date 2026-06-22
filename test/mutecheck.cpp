// throwaway: prove setChannelMask(0) renders silence, and report per-channel
// note-on activity so we can see which channels a song actually uses.
#include "../src/v2redux.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

static unsigned char *slurp(const char *p, size_t *len) {
  FILE *f = fopen(p, "rb"); if (!f) return nullptr;
  fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
  unsigned char *b = (unsigned char *)malloc(n);
  if (fread(b, 1, n, f) != (size_t)n) { fclose(f); free(b); return nullptr; }
  fclose(f); *len = n; return b;
}

static double render_rms(v2redux::Player &p, uint32_t mask, int seconds) {
  p.setChannelMask(mask);
  p.play(0);
  p.setChannelMask(mask);            // re-apply (play() does not clear it, but be sure)
  const uint32_t CH = 4096;
  float buf[CH * 2];
  double sum = 0; uint64_t n = 0;
  uint64_t total = (uint64_t)seconds * 44100;
  for (uint64_t done = 0; done < total; done += CH) {
    p.render(buf, CH);
    for (uint32_t i = 0; i < CH * 2; i++) { sum += (double)buf[i] * buf[i]; n++; }
  }
  return sqrt(sum / n);
}

int main(int argc, char **argv) {
  if (argc < 2) { fprintf(stderr, "usage: mutecheck <file.v2m>\n"); return 2; }
  size_t len; unsigned char *data = slurp(argv[1], &len);
  if (!data) { fprintf(stderr, "cannot read %s\n", argv[1]); return 2; }

  v2redux::Player p;
  if (p.open(data, len) != v2redux::Result::OK) { fprintf(stderr, "open failed\n"); return 2; }

  // which channels does this song actually use? (poll note-on counters over a pass)
  p.setChannelMask(0xffff);
  p.play(0);
  uint32_t non0[16]; p.getChannelNoteOns(non0);
  float buf[4096 * 2];
  for (int s = 0; s < 90; s++) for (int b = 0; b < 11; b++) p.render(buf, 4096); // ~90s
  uint32_t non1[16]; p.getChannelNoteOns(non1);
  printf("channels with note-ons over first ~90s:\n");
  for (int c = 0; c < 16; c++)
    if (non1[c] != non0[c]) printf("  ch %2d : %u note-ons\n", c + 1, non1[c] - non0[c]);

  double rmsAll  = render_rms(p, 0xffff, 30);
  double rmsNone = render_rms(p, 0x0000, 30);
  printf("\nRMS all-audible  (mask 0xffff): %.9f\n", rmsAll);
  printf("RMS all-muted    (mask 0x0000): %.9f   %s\n", rmsNone,
         rmsNone < 1e-7 ? "<- SILENT (gate works)" : "<- STILL AUDIBLE (bug!)");
  free(data);
  return 0;
}
