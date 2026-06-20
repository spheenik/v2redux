# Float arithmetic conventions (portable-determinism)

The original V2 synths run the x87 with **PC=24** (the player sets the control
word before every render): every ordinary arithmetic op rounds its significand
to 24 bits. An IEEE-754 binary32 (`float`) op on SSE/NEON performs the *same*
rounding. The portable engine exploits this: **plain `float` arithmetic is
bit-exact to the original, op for op** — as long as nothing adds or removes
precision behind our back. These are the rules that keep it that way.

## Build flags (enforced by CMakeLists.txt)

- `-ffp-contract=off` — no FMA contraction. An FMA skips one rounding step and
  produces different bits.
- **Never** `-ffast-math` / `-Ofast` / `-funsafe-math-optimizations` — they
  permit reassociation AND link crtfastmath, which enables FTZ/DAZ and breaks
  the v0 subnormal-envelope semantics (`Player::open()` self-checks this).
- Output must be identical at every optimization level (test 8.3 verifies).

## Code rules

1. **`float` everywhere in the audio path.** Doubles are not "safer" — they
   are *wrong* (more precision than the original ⇒ different bits). The only
   doubles allowed are inside `v2math.h` kernels (mirroring the x87's
   extended-precision transcendental internals) and the documented
   `trisaw_flt` cancellation workaround inherited from the lab port.
2. **Literal suffixes.** `x * 0.5` silently promotes to double; write
   `x * 0.5f`. Every numeric literal touching a float must carry `f`.
3. **No approximating libm.** `exp/log/pow/sin/cos/atan(f)` are forbidden in
   engine code — use the `vm::` kernels. Allowed libm (exact or
   correctly-rounded **by definition**, hence deterministic): `ldexp(f)`,
   `frexp`, `trunc(f)`, `floor(f)`, `fmod(f)`, `sqrt(f)`, `fabs(f)`,
   `rint(f)`.
4. **Integer conversion = `vm::fistp`.** Plain `(int)` casts truncate; the
   asm's fistp rounds to nearest-even and stores 0x80000000 out of range.
   Never use bare `lrintf` (UB out of range).
5. **No reductions/reordering invitations.** Accumulate in the same order the
   asm does; don't "clean up" summation loops — order is semantics here.
6. **Constants from the asm verbatim.** When the asm loads a `dd`/`dq`
   constant, replicate its exact value and width (`vm::kFci12` is the
   canonical example: a 24-bit 0.083333333333f that is deliberately NOT 1/12;
   the fastsin/fastatan coefficients are deliberately doubles).

## Verification

`test/mathcheck` compares every `vm::` kernel against the genuine x87
sequences at PC=24 (x86 hosts only; the portable kernels are fixed C++, so an
x86 pass certifies all hosts). Status: **0 mismatches in 25M+ points** on all
deterministic kernels, including the fistp-tie-sensitive `oscfreqi`.
