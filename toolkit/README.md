# era-extraction toolkit

Shared offline tooling for the period-binary hunt: turn a packed/unpacked period
farbrausch binary into its carved v2m song(s), its era-delta assay, and a reference
oracle render. One canonical copy of each stage, replacing the forked scripts that
used to live in `fr08-extraction/`, `flybye-extraction/`, and `candytron-extraction/`.

## Modules

| file | purpose |
| --- | --- |
| `packers.py` | `detect_packer(exe)` (PE section signature) + `unpack(exe)` → flat image |
| `carve.py`   | `find_v2ms(image)` → embedded v2m spans (the canonical `findv2m3` parser) |
| `eras.py`    | `assay(image)` → era-delta constants/opcodes present in a synth image |
| `disasm.py`  | `disasm(image, va, n)` → decoded instructions (image base `0x400000`) |
| `tap.py`     | `read(image, va, type, n)` → typed data at a VA (static image peek; data-side of disasm) |
| `bufcmp.py`  | `compare(a, b, eps)` → oracle contract: `max|d|`/rms/divergence (numpy-accelerated) |
| `era`        | CLI front end: `era {detect|unpack|carve|assay|disasm|tap|compare} <file>` |
| `oracle.h`   | shared C oracle scaffold (mmap@`0x400000` + fault reporter + rdtsc-pin + f32) |

## CLI

```
./era detect  <exe>                 # → aplib | kkrunchy | ruletool | none
./era unpack  <exe> <out.bin>       # depack to a flat image
./era carve   <image.bin>           # list embedded v2m spans
./era carve   <image.bin> --extract i --out song.v2m
./era assay   <image.bin>           # era-delta const/opcode report
./era disasm  <image.bin> <va> [n]  # disassemble n insns at a VA
./era tap     <image.bin> <va> [type] [n]  # read typed data at a VA (u32/i32/f32/f64/u16/u8/hex)
./era compare <a.f32> <b.f32> [eps] # oracle contract: max|d|, rms, first divergence
```

### `era tap` (static) vs `oracle.h` taps (live)

`era tap` is a **static** peek at the unpacked image file — constants, header
fields, "what value sits at this VA". It is the data-side sibling of `era disasm`.
It **cannot** read runtime signal-chain data (osc/flt/dist buffers, voice/filter
workspace) — that is computed inside the running `-m32` oracle and exists only
during render, so it must be tapped there with the `oracle.h` helpers
(`oracle_buf`/`oracle_field_f32`/`oracle_tap_*`). A pointer slot the binary fills at
startup reads as 0 statically.

## Packer routes (`unpack`)

- **aplib** (`rygs and`/`packer.` stub) and **kkrunchy** (single section): both are
  unpacked by running the depacker stub under Unicorn — map the image, emulate from
  the PE entry, let the stub decompress in place, stop when it jumps to the
  unresolved OEP (`UC_ERR_INSN_INVALID`), dump `0x400000`. The aPLib stub is pure
  integer; the flybye image round-trips byte-identically.
- **none** (conventional `.text/.rdata/...`): not packed — `unpack` returns the file
  image unchanged and `carve` operates on it directly (e.g. zeitmaschine).
- **ruletool** (`ruletool`/`resultat`, fr011): detected but **not** unpacked — raises
  a clear error. Reverse-engineering this earlier packer is out of scope.

## Oracle scaffold (`oracle.h`)

`oracle.h` is **native** (`-m32` on the host CPU), never Unicorn. The V2 audio path
is x87-transcendental-heavy (`fsin`/`fpatan`/`f2xm1`); the host hardware x87
reproduces the period results bit-for-bit (`max|d| = 0`), whereas QEMU/Unicorn rounds
the bottom ~11 mantissa bits (libm-at-double) — tested: `fsin` ~679 ULP, `f2xm1`
~1936, `fpatan` ~837. Unicorn is for unpacking only. See the change `design.md` (D3).

A harness `#include "oracle.h"`, supplies its own `{IMG_SIZE, entry/init/render VAs,
rdtsc sites}` and driving strategy (in-image player or ported player), and gets the
common scaffold helpers. Build: `gcc -m32 -no-pie -O0 harness.c -o harness`.

**Tapping a loaded oracle.** `oracle.h` also provides the shared mechanism for
pulling intermediate data out of the *running* image at chosen positions — the
per-binary VAs/offsets stay in the harness, the read/sink plumbing is shared:

- typed live reads: `oracle_u32(va)` / `oracle_i32` / `oracle_f32_at` / `oracle_ptr`,
  buffer pointers `oracle_buf(va)` (buffer *at* va) and `oracle_bufptr(va)` (buffer
  *pointed to by* va), and pointer-indirect field reads `oracle_field_u32(base, off)`
  / `oracle_field_f32` (deref a workspace pointer, then read a field; 0 if null).
- env-gated tap sinks: `oracle_tap t = oracle_tap_open("C1_OSC", "ch7.osc")` is a
  no-op unless `C1_OSC` is set; then `oracle_tap_f32(&t, oracle_buf(VA), n)` /
  `oracle_tap_va(&t, VA, bytes)` / `oracle_tap_bytes` stream to it; `oracle_tap_close`.

This replaces each harness's own `rd32`, scattered `*(float*)(uintptr_t)va` casts,
and `if(getenv("X")){fopen;…fwrite;…}` boilerplate (the `osc`/`flt`/`dist`/`premix`
signal-chain dumps and the `VTAP`/`RTAP`/`STAP` state probes).

**Signal-chain tap table.** Dumping several DSP points each render chunk is the same
four steps per tap — open, reset, dump, close — differing only by `{name, mono/stereo,
accumulator}`; only the *accumulate point* (where the live buffer feeds the
accumulator) is per-binary, since it sits in the render control flow. Declare a table
of `oracle_chan_tap` and the lifecycle is one call each: `oracle_taps_open(T, n,
getenv("PFX"))` (no-op if unset), `oracle_taps_reset` / `oracle_tap_add(&T[i], buf, n)`
/ `oracle_taps_dump` per chunk, `oracle_taps_close`. This collapses the ~9×
fopen/memset/fwrite/fclose lines (e.g. in `c1_solo_probe.c`) to a table + four calls.

## Dependencies

`python-unicorn` (2.1.4) and `python-capstone` (5.0.7) — on Arch: `pacman -S
python-unicorn python-capstone`. Only `unpack`/`disasm` need them; `carve`/`assay`
are pure Python.
