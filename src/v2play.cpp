// v2play -- native real-time player + visualizer for the portable V2 synth.
//
//   v2play <file.v2m> [--era N]
//
// Audio: sokol_audio (ALSA / CoreAudio / WASAPI) pulls float32 stereo straight
// from v2redux::Player::render() on its own thread. Graphics: sokol_gfx +
// sokol_gl draw an oscilloscope (time domain), a log-frequency spectrum
// analyzer, and L/R VU bars from a snapshot of the same audio.
//
// The synth core keeps its strict determinism contract; everything in THIS
// file (FFT, scope trigger, drawing) is display-only and deliberately outside
// that contract -- it never feeds back into the rendered signal.
//
// Keys:  space = pause   r = restart   l = loop toggle   q/esc = quit
//        left/right = seek +-5s   up/down = seek +-30s

#include "v2redux.h"

#include "third_party/sokol/sokol_config.h"
#include "third_party/sokol/sokol_log.h"
#include "third_party/sokol/sokol_gfx.h"
#include "third_party/sokol/sokol_app.h"
#include "third_party/sokol/sokol_glue.h"
#include "third_party/sokol/sokol_gl.h"
#include "third_party/sokol/sokol_audio.h"

#include <atomic>
#include <mutex>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Optional compile-time-embedded song (CMake -DV2_EMBED_SONG=<file.v2m>): the
// generated translation unit defines these with C linkage. When present and no
// file argument is given, v2play plays this song -- a self-contained demo exe.
#ifdef V2PLAY_EMBEDDED
extern "C" const unsigned char v2play_song[];
extern "C" const unsigned long v2play_song_len;
#ifndef V2PLAY_EMBED_NAME
#define V2PLAY_EMBED_NAME "embedded song"
#endif
#endif

// ---------------------------------------------------------------------------
// shared audio<->gui state
// ---------------------------------------------------------------------------

static const int   SAMPLE_RATE = 44100;
static const int   RING        = 1 << 15;   // 32768 mono samples
static const int   RING_MASK   = RING - 1;
static const int   SNAP        = 8192;      // window pulled per frame
static const int   FFT_N       = 8192;      // <= SNAP (~186ms -> ~5.4Hz bins)

static v2redux::Player g_player;
static bool               g_opened = false;

static std::mutex         g_mtx;            // guards the ring + block peaks
static float              g_ring[RING];
static uint32_t           g_widx = 0;
static float              g_blockPeakL = 0.0f, g_blockPeakR = 0.0f;

static std::atomic<bool>  g_paused{false};
static std::atomic<bool>  g_restart{false};
static std::atomic<bool>  g_loop{true};

// playhead + seeking. The audio thread owns g_posFrames and publishes the
// position in ms; the GUI thread reads g_playMs and posts a target into
// g_seekMs (-1 = none) which the audio thread consumes via play(targetMs).
static uint64_t           g_posFrames = 0;       // audio-thread owned
static std::atomic<long long> g_playMs{0};       // published position (ms)
static std::atomic<long long> g_seekMs{-1};      // requested seek target (ms)

// ---------------------------------------------------------------------------
// audio thread: render + mirror a mono sum into the ring
// ---------------------------------------------------------------------------

static void audio_cb(float *buffer, int nframes, int nchan)
{
  if (!g_opened) {
    memset(buffer, 0, sizeof(float) * (size_t)nframes * nchan);
    return;
  }

  // position changes take effect even while paused (reposition, stay paused)
  if (g_restart.exchange(false)) {
    g_player.play(0); g_posFrames = 0; g_playMs.store(0);
  }
  long long seek = g_seekMs.exchange(-1);
  if (seek >= 0) {
    g_player.play((uint32_t)seek);
    g_posFrames = (uint64_t)seek * SAMPLE_RATE / 1000;
    g_playMs.store(seek);
  }

  if (g_paused.load(std::memory_order_relaxed)) {
    memset(buffer, 0, sizeof(float) * (size_t)nframes * nchan);
    return;
  }

  // device buffer is interleaved stereo float32 -- exactly render()'s format
  g_player.render(buffer, (uint32_t)nframes);

  if (g_loop.load(std::memory_order_relaxed) && !g_player.isPlaying()) {
    g_player.play(0); g_posFrames = 0; // loop the demo (cuts the ring-out tail)
  }
  g_posFrames += (uint64_t)nframes;
  g_playMs.store((long long)(g_posFrames * 1000 / SAMPLE_RATE));

  float pL = 0.0f, pR = 0.0f;
  std::lock_guard<std::mutex> lock(g_mtx);
  for (int i = 0; i < nframes; i++) {
    float l = buffer[2 * i], r = buffer[2 * i + 1];
    g_ring[g_widx & RING_MASK] = 0.5f * (l + r);
    g_widx++;
    float al = fabsf(l), ar = fabsf(r);
    if (al > pL) pL = al;
    if (ar > pR) pR = ar;
  }
  g_blockPeakL = pL;
  g_blockPeakR = pR;
}

// ---------------------------------------------------------------------------
// tiny display-only radix-2 FFT (in place)
// ---------------------------------------------------------------------------

static void fft(float *re, float *im, int n)
{
  for (int i = 1, j = 0; i < n; i++) {
    int bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) {
      float tr = re[i]; re[i] = re[j]; re[j] = tr;
      float ti = im[i]; im[i] = im[j]; im[j] = ti;
    }
  }
  for (int len = 2; len <= n; len <<= 1) {
    float ang = -2.0f * 3.14159265358979f / (float)len;
    float wr = cosf(ang), wi = sinf(ang);
    for (int i = 0; i < n; i += len) {
      float cwr = 1.0f, cwi = 0.0f;
      for (int k = 0; k < len / 2; k++) {
        int a = i + k, b = a + len / 2;
        float vr = re[b] * cwr - im[b] * cwi;
        float vi = re[b] * cwi + im[b] * cwr;
        float ur = re[a], ui = im[a];
        re[a] = ur + vr; im[a] = ui + vi;
        re[b] = ur - vr; im[b] = ui - vi;
        float n2 = cwr * wr - cwi * wi;
        cwi = cwr * wi + cwi * wr; cwr = n2;
      }
    }
  }
}

// ---------------------------------------------------------------------------
// drawing helpers (NDC, sokol-gl default identity matrices)
// ---------------------------------------------------------------------------

// region: oscilloscope on the top half, spectrum on the bottom half
static const float SCOPE_Y0 = 0.05f, SCOPE_Y1 = 0.95f;   // -> upper band below
static const float SCOPE_CY = 0.50f, SCOPE_H = 0.42f;    // center / half-height
static const float SPEC_Y0  = -0.95f, SPEC_H = 0.85f;    // bars grow up from y0

static void draw_grid()
{
  sgl_begin_lines();
  sgl_c4f(0.16f, 0.18f, 0.20f, 1.0f);
  // scope zero line
  sgl_v2f(-0.97f, SCOPE_CY); sgl_v2f(0.97f, SCOPE_CY);
  // divider
  sgl_v2f(-0.97f, 0.0f);     sgl_v2f(0.97f, 0.0f);
  // spectrum baseline
  sgl_v2f(-0.97f, SPEC_Y0);  sgl_v2f(0.97f, SPEC_Y0);
  sgl_end();
  (void)SCOPE_Y0; (void)SCOPE_Y1;
}

static void draw_scope(const float *snap)
{
  // draw from the TAIL so the scope shows the most recent audio (the FFT uses
  // the whole, longer window; the scope must not inherit its latency).
  const int N = 1024;                 // samples drawn
  const int searchLo = SNAP - N - 2048;

  // hysteresis trigger: arm on a dip below -h, then fire on the next rising
  // zero crossing. Rejects near-zero noise wiggles so the trace stands still.
  float peak = 1e-6f;
  for (int i = searchLo; i < SNAP; i++) {
    float a = fabsf(snap[i]); if (a > peak) peak = a;
  }
  float h = 0.10f * peak;
  static int lastStart = searchLo;
  int start = -1;
  bool armed = false;
  for (int i = searchLo + 1; i < SNAP - N; i++) {
    if (snap[i] < -h) armed = true;
    if (armed && snap[i - 1] <= 0.0f && snap[i] > 0.0f) { start = i; break; }
  }
  if (start < 0) start = lastStart;   // silence/no edge: hold the last position
  else lastStart = start;
  if (start < searchLo) start = searchLo;
  if (start > SNAP - N) start = SNAP - N;

  sgl_begin_line_strip();
  sgl_c4f(0.25f, 1.0f, 0.45f, 1.0f);
  for (int i = 0; i < N; i++) {
    float s = snap[start + i];
    float x = -0.97f + 1.94f * ((float)i / (float)(N - 1));
    float y = SCOPE_CY + s * SCOPE_H;
    if (y > SCOPE_CY + SCOPE_H) y = SCOPE_CY + SCOPE_H;
    if (y < SCOPE_CY - SCOPE_H) y = SCOPE_CY - SCOPE_H;
    sgl_v2f(x, y);
  }
  sgl_end();
}

static void draw_spectrum(const float *snap)
{
  static float re[FFT_N], im[FFT_N];
  const float *src = snap + (SNAP - FFT_N);     // most recent FFT_N samples
  for (int i = 0; i < FFT_N; i++) {
    float w = 0.5f - 0.5f * cosf(2.0f * 3.14159265358979f * i / (FFT_N - 1)); // Hann
    re[i] = src[i] * w;
    im[i] = 0.0f;
  }
  fft(re, im, FFT_N);

  const int   NBARS  = 120;
  const float binHz  = (float)SAMPLE_RATE / FFT_N;
  const float fmin   = 35.0f, fmax = 18000.0f;
  const float dbMin  = -70.0f, dbMax = 0.0f;
  const float gain   = 3.5f;                      // visual headroom fudge

  auto magAt = [&](int k) -> float {
    if (k < 1) k = 1;
    if (k > FFT_N / 2) k = FFT_N / 2;
    return sqrtf(re[k] * re[k] + im[k] * im[k]) * (2.0f / FFT_N) * gain;
  };

  sgl_begin_quads();
  for (int b = 0; b < NBARS; b++) {
    float f0 = fmin * powf(fmax / fmin, (float)b / NBARS);
    float f1 = fmin * powf(fmax / fmin, (float)(b + 1) / NBARS);
    int i0 = (int)(f0 / binHz), i1 = (int)(f1 / binHz);
    float mag;
    if (i1 > i0) {
      // band spans >=1 full bin: take the peak across the covered bins
      mag = 0.0f;
      for (int k = (i0 < 1 ? 1 : i0); k <= i1; k++) {
        float m = magAt(k);
        if (m > mag) mag = m;
      }
    } else {
      // sub-bin band (the low end): linearly interpolate at the band center
      float fc = sqrtf(f0 * f1), kf = fc / binHz;
      int k = (int)kf; float fr = kf - k;
      mag = magAt(k) * (1.0f - fr) + magAt(k + 1) * fr;
    }
    float db = 20.0f * log10f(mag + 1e-9f);
    float h  = (db - dbMin) / (dbMax - dbMin);
    if (h < 0.0f) h = 0.0f; if (h > 1.0f) h = 1.0f;

    float x0 = -0.97f + 1.94f * ((float)b / NBARS);
    float x1 = -0.97f + 1.94f * ((float)(b + 1) / NBARS) - 0.004f;
    float y0 = SPEC_Y0;
    float y1 = SPEC_Y0 + h * SPEC_H;
    // green -> yellow -> red by height
    float r = h < 0.5f ? h * 2.0f : 1.0f;
    float g = h < 0.5f ? 1.0f : 1.0f - (h - 0.5f) * 1.6f;
    sgl_c4f(r * 0.9f + 0.05f, g, 0.25f, 1.0f);
    sgl_v2f(x0, y0); sgl_v2f(x1, y0); sgl_v2f(x1, y1); sgl_v2f(x0, y1);
  }
  sgl_end();
}

static void vu_color(float h)
{
  float r = h > 0.6f ? 1.0f : h * 1.4f;
  float g = h > 0.85f ? 1.0f - (h - 0.85f) * 4.0f : 1.0f;
  if (g < 0.0f) g = 0.0f;
  sgl_c4f(r, g, 0.20f, 1.0f);
}

static void draw_vu(float pL, float pR)
{
  // two thin vertical bars hugging the left/right edges, full window height
  auto bar = [](float x0, float x1, float level) {
    float y0 = -0.97f, y1 = -0.97f + 1.94f * level;
    sgl_begin_quads();
    vu_color(level);
    sgl_v2f(x0, y0); sgl_v2f(x1, y0); sgl_v2f(x1, y1); sgl_v2f(x0, y1);
    sgl_end();
  };
  bar(-0.995f, -0.975f, pL);
  bar(0.975f, 0.995f, pR);
}

// ---------------------------------------------------------------------------
// sokol-app lifecycle
// ---------------------------------------------------------------------------

static void init_cb(void)
{
  sg_desc gd = {};
  gd.environment = sglue_environment();
  gd.logger.func = slog_func;
  sg_setup(&gd);

  sgl_desc_t sd = {};
  sd.logger.func = slog_func;
  sgl_setup(&sd);

  saudio_desc ad = {};
  ad.sample_rate   = SAMPLE_RATE;
  ad.num_channels  = 2;
  ad.buffer_frames = 1024;
  ad.stream_cb     = audio_cb;
  ad.logger.func   = slog_func;
  saudio_setup(&ad);

  if (g_opened)
    g_player.play(0);
  if (saudio_sample_rate() != SAMPLE_RATE)
    fprintf(stderr, "warning: audio device runs at %d Hz, song is %d Hz -- "
            "pitch will be off\n", saudio_sample_rate(), SAMPLE_RATE);
}

static void frame_cb(void)
{
  static float snap[SNAP];
  float pkL, pkR;
  {
    std::lock_guard<std::mutex> lock(g_mtx);
    uint32_t w = g_widx;
    for (int i = 0; i < SNAP; i++)
      snap[i] = g_ring[(w - SNAP + i) & RING_MASK];
    pkL = g_blockPeakL;
    pkR = g_blockPeakR;
  }

  // VU ballistics: instant attack, timed release
  static float vuL = 0.0f, vuR = 0.0f;
  float dt = (float)sapp_frame_duration();
  float rel = dt * 1.8f;
  vuL = pkL > vuL ? pkL : vuL - rel; if (vuL < 0.0f) vuL = 0.0f;
  vuR = pkR > vuR ? pkR : vuR - rel; if (vuR < 0.0f) vuR = 0.0f;

  sgl_defaults();
  draw_grid();
  draw_spectrum(snap);
  draw_scope(snap);
  draw_vu(vuL > 1.0f ? 1.0f : vuL, vuR > 1.0f ? 1.0f : vuR);

  sg_pass pass = {};
  pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
  pass.action.colors[0].clear_value = { 0.03f, 0.03f, 0.045f, 1.0f };
  pass.swapchain = sglue_swapchain();
  sg_begin_pass(&pass);
  sgl_draw();
  sg_end_pass();
  sg_commit();
}

// post a relative seek from the current published position (lower-clamped at
// 0; no upper clamp -- the song length isn't exposed by the player API)
static void seek_by(long long deltaMs)
{
  long long t = g_playMs.load() + deltaMs;
  if (t < 0) t = 0;
  g_seekMs.store(t);
  fprintf(stderr, "seek -> %lld:%02lld\n", t / 60000, (t / 1000) % 60);
}

static void event_cb(const sapp_event *e)
{
  if (e->type != SAPP_EVENTTYPE_KEY_DOWN)
    return;
  switch (e->key_code) {
  case SAPP_KEYCODE_ESCAPE:
  case SAPP_KEYCODE_Q: sapp_request_quit(); break;
  case SAPP_KEYCODE_SPACE:
    g_paused.store(!g_paused.load()); break;
  case SAPP_KEYCODE_R: g_restart.store(true); break;
  case SAPP_KEYCODE_L: g_loop.store(!g_loop.load()); break;
  case SAPP_KEYCODE_LEFT:  seek_by(-5000);  break;
  case SAPP_KEYCODE_RIGHT: seek_by(+5000);  break;
  case SAPP_KEYCODE_DOWN:  seek_by(-30000); break;
  case SAPP_KEYCODE_UP:    seek_by(+30000); break;
  default: break;
  }
}

static void cleanup_cb(void)
{
  saudio_shutdown();
  sgl_shutdown();
  sg_shutdown();
}

// ---------------------------------------------------------------------------
// load the song, then hand off to sokol-app
// ---------------------------------------------------------------------------

static bool open_song(const void *data, size_t len, int era, const char *label)
{
  v2redux::Result r = era < 0 ? g_player.open(data, len)
                              : g_player.open(data, len, era);
  if (r != v2redux::Result::OK) {
    fprintf(stderr, "%s: open failed (result %d, detected v%d)\n",
            label, (int)r, g_player.fileVersion());
    return false;
  }
  fprintf(stderr, "%s: format v%d\n", label, g_player.fileVersion());
  return true;
}

static bool load_song_file(const char *path, int era)
{
  FILE *f = fopen(path, "rb");
  if (!f) { fprintf(stderr, "cannot open %s\n", path); return false; }
  fseek(f, 0, SEEK_END);
  long len = ftell(f);
  fseek(f, 0, SEEK_SET);
  void *data = malloc((size_t)len);
  bool ok = data && fread(data, 1, (size_t)len, f) == (size_t)len;
  fclose(f);
  if (!ok) { fprintf(stderr, "short read on %s\n", path); free(data); return false; }
  ok = open_song(data, (size_t)len, era, path);
  free(data);
  return ok;
}

sapp_desc sokol_main(int argc, char *argv[])
{
  const char *path = nullptr;
  int era = -1;
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--era") == 0 && i + 1 < argc)
      era = atoi(argv[++i]);
    else if (!path)
      path = argv[i];
  }
  if (path) {
    g_opened = load_song_file(path, era);
  } else {
#ifdef V2PLAY_EMBEDDED
    g_opened = open_song(v2play_song, (size_t)v2play_song_len, era,
                         V2PLAY_EMBED_NAME);
#else
    fprintf(stderr, "usage: v2play <file.v2m> [--era N]\n"
                    "  (or rebuild with -DV2_EMBED_SONG=<file.v2m> to bake one in)\n");
#endif
  }

  sapp_desc d = {};
  d.init_cb     = init_cb;
  d.frame_cb    = frame_cb;
  d.cleanup_cb  = cleanup_cb;
  d.event_cb    = event_cb;
  d.width       = 960;
  d.height      = 600;
  d.sample_count = 4;   // 4x MSAA: smooths the scope line + spectrum bar edges
  d.window_title = "v2play";
  d.logger.func = slog_func;
  return d;
}
