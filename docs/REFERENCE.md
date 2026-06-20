# V2 Redux — technical reference

The detailed companion to the [README](../README.md): the C++ API, the version
& era support model, the determinism contract, the measured fidelity (ε), and
the test suite. For the *why* and the per-song forensic detail, see
[`ACCURACY.md`](ACCURACY.md).

## API

The public interface is `v2redux.h` (namespace `v2redux`). One song per
`Player`; instances are independent and may run concurrently.

```cpp
#include "v2redux.h"
using namespace v2redux;

Player p;
Result r = p.open(data, length);          // detect version, canonicalize, prepare
if (r != Result::OK) { /* BadFile | UnsupportedVersion | FpEnvBroken */ }
int v = p.fileVersion();                   // detected format version 0..6

p.setSeed(0);                              // deterministic default (replaces rdtsc)
p.play();                                  // from the start (full synth reset)

float buf[2 * 4096];
while (p.isPlaying()) {
    p.render(buf, 4096);                   // interleaved stereo f32 @ 44100 Hz
    // ... consume buf ...                 // chunk size is irrelevant to the bits
}
```

Key contracts:
- `render()` is **chunk-size invariant** — any partition of N frames yields
  identical bits — and never allocates.
- `setSeed()` replaces the historical `rdtsc` noise / S&H / distortion seeding;
  `0` is the reference seed (matches the pinned-rdtsc oracle convention).
- `open(data, len, era)` — the optional third argument is the **engine
  identity** (`Era`, see `v2eras.h`). The format version is a lossy proxy for
  the build that rendered a song (the score, not the orchestra); a few
  behaviours flip mid-version on a build-date timeline the format can't see.
  - `Era::Auto()` (default) — use the detected version. Correct for every clean
    case (byte-identical to before this knob existed).
  - `eras::xxx` — a named build profile when the format can't express the build,
    e.g. `eras::kkrieger2004` (a format-v5 file rendered by a near-v6 engine:
    polynomial sine/FM + fastatan, but still v5 DC-offset and voice pool). Each
    catalog entry is one disassembled binary.
  - `Era::v(n)` / `.with(DELTA_X, Era::New)` — a raw version baseline or a
    refined research override. The resolved base must lie in the compiled range.
  - A back-compat `open(data, len, int)` overload remains (`-1` == Auto,
    `0..6` == `Era::v(n)`).

### `v2dump` CLI

```sh
v2dump <in.v2m> <out.{wav,f32}> [secs] [chunk]
```

`.wav` is an IEEE-float32 RIFF whose payload is bit-identical to the raw `.f32`
compare format. With `secs` omitted (or `auto`) it renders the whole song plus
the reverb/delay tail rung out to ~−90 dBFS and trimmed; a fixed `secs` is the
deterministic A/B mode. (MP3 is out of scope — pipe to an external encoder.)

## Version & era support

The loader detects the format version by structural fingerprint, parses that
version's layout, and canonicalises value-preservingly to the v6 *layout* as
its internal representation — while the engine switches behaviour on the
**detected source version**, not v6. Era thresholds live in `v2eras.h`.

Every era below was validated by carving the song from its demo binary and
rendering it through *that binary's own engine* (the "oracle"), then matching
the player sample-for-sample. There are **no converted files** anywhere in the
validation — ground truth is always the genuine embedded song at its native era.

| version | reference binary | fidelity |
| --- | --- | --- |
| **v0** | fr-08 *.the .product* (2000) | **whole-song bit-exact** (663 s, max\|d\| = 0) |
| **v1** | *flybye* (2001) | **whole-song bit-exact** |
| v2 | — | no period binary exists anywhere; renders deterministically at native-era behaviour, but era-correctness is **unprovable** until a v2 binary surfaces |
| **v3** | fr-014 *mark & sweep* (2001) | oracle-proven, ε-floor (whole-song corr 0.99989) |
| **v4** | fr-019 *poem to a horse* (2002) | oracle-proven, ε-floor (0 of 341 s above 3 %, rms 0.12 %) |
| **v5** | *candytron* (2003) + fr-024 / fr-029 / *.kkrieger* | candytron per-channel DSP / voice / player / voice-steal **bit-exact**; fr-024/fr-029/.kkrieger ε-floor |
| **v6** | *synth.asm* (2004) | **bit-exact** (max\|d\| = 0) |

`v2` is the only gap, and an external one: no v2-format demo binary is known to
exist to serve as an oracle. See [`ACCURACY.md`](ACCURACY.md) §3–§4.

### Era model

Most behaviours key off the format version directly. A few flip on the engine
*build date* within a single format version — the format can't express which
build wrote a file, so the caller supplies it via `Era`:

- **`Era::Auto()`** (default) resolves to the detected version and is correct
  for every clean case.
- **`eras::` catalog** entries pin a known straddler. The shipped one is
  `eras::kkrieger2004`: the *.kkrieger* beta is a format-v5 file whose 2004
  engine already used the polynomial sine/FM and fastatan of the later builds,
  while keeping v5's DC-offset and 32-voice pool — a mixed intermediate build
  the version number alone cannot name.
- Each `eras::` entry is `constexpr` (two 32-bit masks) and folds every era gate
  at compile time.

## Determinism contract

Identical float32 output, bit-for-bit, across compilers, optimisation levels,
and CPU architectures (verified equal on **x86_64 and aarch64**, whole corpus).
This is enforced two ways:

1. **No approximating libm in the audio path.** All transcendentals
   (`sin`/`cos`/`exp`/`log`/`atan`/`pow`) are the engine's own polynomial
   kernels in `v2math.h`, reproducing the original x87 `PC=24` rounding
   op-for-op. The only `<math.h>` used is the IEEE *correctly-rounded / exact*
   subset (`sqrt`, `fabs`, `ldexp`, `frexp`, `fmod`, `trunc`, `lrint`) — bit-
   identical on every conforming architecture by definition.
2. **Build flags** (pinned by `CMakeLists.txt` per compiler — the float
   arithmetic *is* the spec; flags that change rounding change the bits):

   | flag | requirement | why |
   | --- | --- | --- |
   | `-ffast-math` / `/fp:fast` | **never** | enables FTZ/DAZ → flushes subnormals → breaks v0 envelope semantics; also relaxes rounding |
   | `-ffp-contract=off` / `/fp:precise` | **always** | FMA contraction skips a rounding step → different bits |
   | FTZ/DAZ (any source) | **off** | subnormal arithmetic must round normally; `open()` self-checks this and returns `FpEnvBroken` otherwise |
   | SSE2, never x87 | **forced** | 32-bit builds default to x87 80-bit; `/arch:SSE2` (MSVC) / `-msse2 -mfpmath=sse` (GCC/Clang) keep the bits equal to the 64-bit / NEON path |
   | `-O0` vs `-O2` | either | optimisation level must not change output (verified bit-identical) |

## ε — measured fidelity

ε is the residual between the player and the genuine period binary. It is a
fixed, measured quantity — the documented native-polynomial-vs-x87
transcendental-tie class (a 1-ULP `fsin`/`fpatan` tie nudging a keysync=0
oscillator phase late in a song), **not drift**. Several eras have no ε at all.

| reference | window | result |
| --- | --- | --- |
| **v0** fr-08 vs the year-2000 binary | whole 663 s | **bit-exact**, max\|d\| = 0 |
| **v1** *flybye* vs the 2001 binary | whole song | **bit-exact** |
| **v3** fr-014 vs *mark & sweep* | whole song | corr 0.99989 (ε-floor; ch8 noise phase) |
| **v4** fr-019 vs *poem to a horse* | whole 341 s | 0 s above 3 %, rms 0.12 % (ε-floor) |
| **v5** *candytron* | per-channel / voice / player | **bit-exact** (DSP, voice DSP, sequencer, voice-steal) |
| **v5** fr-024 (own engine) | whole song | corr 0.999991, rms\|d\| 0.0010 (ε-floor) |
| **v5** fr-029 (own engine) | whole song | corr 0.999557, rms\|d\| 0.0051 (ε-floor) |
| **v5** *.kkrieger* (`eras::kkrieger2004`) | 30 s mix | corr 0.99999 (ε-floor) |
| **v6** pzero / zeitmaschine vs *synth.asm* | exact | **bit-exact**, max\|d\| = 0 |
| **cross-arch** x86_64 vs aarch64 | whole corpus | **bit-identical** (every file) |

Full per-song derivations, the two documented build-era sub-cases (early-v5
old-core; the brüllwürfel noise-seed), and the open ε threads are in
[`ACCURACY.md`](ACCURACY.md).

## Test suite

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
ctest --test-dir build
```

| test | checks |
| --- | --- |
| `mathcheck` | owned transcendentals vs genuine x87 at PC=24 (0 mismatches over 25 M+ points) |
| `tablecheck` | per-version parameter size/offset tables vs the original `sounddef.h` |
| `twoinstance` | two concurrent `Player`s are independent; render-before-open is silent |
| `forcecheck` | `Era` / `forceBehaviorVersion` resolution both ways + out-of-range rejection |
| `corpus_hashes` | renders the corpus and checks per-file SHA-256 against `test/baselines.sha256` — the **cross-host determinism gate** |

`corpus_hashes` (via `test/check.py`) is a *determinism* gate: the same bytes on
every host. Fidelity is the era oracles above, not this hash. (`check.py
--oracle-dir DIR` adds the ε comparison when reference renders are available.)
`loadcheck` builds but is not in the default run — it compared the loader's
canonicalisation against a converted file, and the converted corpus was removed
by design.

---

Living threshold ledger: [`v2eras.h`](../v2eras.h). The disassembly notes,
carve toolkit, and reference renders behind these claims live in the upstream
research repository — see the note at the top of [`ACCURACY.md`](ACCURACY.md).
