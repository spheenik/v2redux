# Portable player — accuracy & fidelity record

> **Note on references.** This is the working fidelity record of the port. It
> cites the research lab where the oracles were built — disassembly notes,
> carve toolkit, scratch renders (paths like `../v2m/<demo>-extraction/NOTES.md`,
> `toolkit/`). That lab is the upstream development repository and is **not
> shipped in this standalone player repo**; the references are preserved as a
> provenance trail. What *is* here: the player, the era ledger (`v2eras.h`), the
> corpus, and the determinism gate (`test/check.py`) — enough to reproduce every
> hash-level claim. The per-section findings below stand on their own.

This is the fidelity record of the player: how each era was validated against
the genuine engine, and the handful of sub-perceptual residuals that remain.
The player is structurally faithful, deterministic, chunk-size invariant, and
**bit-identical across architectures** (x86_64 ≡ aarch64, whole corpus — §6).

**Validation status by format version** (each carved from its demo binary and
rendered through that binary's *own* engine — never a converted file):

| ver | reference binary | result |
| --- | --- | --- |
| **v0** | fr-08 *.the .product* (2000) | **whole-song bit-exact** (663 s, max\|d\|=0) |
| **v1** | *flybye* (2001) | **whole-song bit-exact** |
| v2 | — | no period binary exists anywhere; deterministic, validation-only (§4) |
| **v3** | fr-014 *mark&sweep* (2001) | oracle-proven, ε-floor (corr 0.99989, §8) |
| **v4** | fr-019 *poem to a horse* (2002) | oracle-proven, ε-floor (0/341 s >3%, §9) |
| **v5** | *candytron* (2003) + fr-024/fr-029/*.kkrieger* | candytron per-stage **bit-exact**; the others ε-floor (§1, §7, §10) |
| **v6** | *synth.asm* (2004) | **bit-exact** (max\|d\|=0) |

What remains below is **not defects** — it is the irreducible
native-polynomial-vs-x87 transcendental-tie class (a 1-ULP `fsin`/`fpatan` tie
nudging a keysync=0 oscillator phase late in a song), sub-perceptual and
inaudible in listening tests. The sections are kept so the work is *resumable*.

The living threshold ledger is `v2eras.h`.

---

## 1. josie v5 music-bed residual (~0.009 rms) — transcendental ε

**Status:** localized, not a structural bug. Documented as ε.

The native v5 render of `josie.v2m` matches the candytron binary oracle
(`c2_josie.f32`) to a music-bed residual of **rms ~0.009** once ch15 is
routed through Ronan (the V2_RONAN=0 stub leaving ch15 raw accounts for the
larger 0.079 figure — that is a *config* artifact, not a mix bug).

Proven bit-exact vs the candytron binary along the way:
- per-channel DSP chain — 13/16 channels solo `d_rms = 0.00000`
- the voice DSP (direct voice-array tap @0x4c4be0)
- the player / sequencer (`v2seq` == genthree `_viruz2.cpp`)
- voice stealing — all 291 note-on allocations identical

The remaining floor is **ch3/ch6 late-song (38–46 s) oscillator-phase razor
ties** amplified by the sum-compressor: keysync=0 → continuous phase, ch6 is
bit-exact until a single 1-ULP tie at 39.5 s and then the phase drifts.
Prime suspect: portable native-sin vs the binary's x87 `fsin` feeding an
LFO/osc.

**To close it (optional):** decode the exact op at ch6's 39.5 s onset via
per-note bisection (the method that drove fr08 v0 to max|d| = 0), then
either localize+fix the 1-ULP op or formally document it as irreducible
native-vs-x87 ε under the fr08 v0 rules. Tap ch6 solo onset @3487340.
Tooling: `c2_oracle_solo` (C2_SOLO/C2_VTAP/C2_CTAP) + `v2dump` V2SEQ_SOLO.

## 2. Ronan phoneme-sequencer one-syllable lag (was the "audibly off")

**Status:** FIXED in 6.1 (USER-CONFIRMED by listening), kept here only as a
pointer for the related fine-timing thread.

Root cause was `reset()` not replicating the lab's `memset(workspace,0)`,
leaving `wait4on` stuck after a mid-song CC4 text-select → one-syllable lag
all song (ch15 speech corr 0.13 → 0.99966). The residual *fine* phoneme
timing (sub-frame note-on/off vs wait-gate ordering) was never chased to the
sample because it is inaudible. If ever revisited: per-tick trace of
spos/scounter/framecount/wait4on after the 7.384 s reset, both sides
(tools C2_RTAP/C2_NTAP committed; ronan process@0x41493c tick@0x4145c4
ws[0x6a88f8]).

## 3. v1–v4 ASSUMED era-table rows

**Status (updated 2026-06-07):** two more period binaries unpacked and
assayed — **flybye (fr-013, format v1)** and **fr-022 ein.schlag (embeds a
v5 export)** — see `../v2m/flybye-extraction/NOTES.md`. The era assay now has
a five-point build timeline: v0 fr08 (2000) · v1 flybye (2001-12) · v5 fr-022
(2002-08) · v5 candytron (2003-08) · v6 synth.asm (2004).

All seven formerly-ASSUMED rows are now resolved (data edits) — the osc-core
trio by constant scan, the four player/render rows by disassembly. **No
EV_ASSUMED row remains at flipsAt 1.** The follow-up render oracle
(`c1_flybye_harness.c`) then upgraded v1 from correct-by-row to **whole-song
bit-exact**, catching one row no static read had found: the **voice-pool
size** (16 in 2000/2001, 32 in both 2002/2003 v5 binaries, 64 in the 2004
asm) — now `DELTA_POLY_16`/`DELTA_POLY_32` + era-bounded allocator scans
(the 66.42 s hunt, flybye-extraction NOTES).

| row | status after flybye/fr-022 |
| --- | --- |
| `DELTA_OSC_BOXFILTER`   | **SUPERSEDED → flipsAt 3 PROVEN (see §9).** flybye(v1) box, fr014(v3)/fr019(v4) OSM — the box→OSM rewrite is v1→v3, not the build-date. flipsAt 5 was wrong. |
| `DELTA_NOISE_LCG_MSVC`  | **fixed → flipsAt 5.** flybye(v1) has MSVC LCG; modern absent. |
| `DELTA_OSC_FREQ_CONST`  | **fixed → flipsAt 5.** flybye(v1) has baked 3185015. |
| `DELTA_PGMCHANGE_V0`    | **fixed → flipsAt 5.** flybye(v1) disasm @0x410455 == fr08: no early-out + ctl7=127. (NEW already at early-v5 fr-022 — flipped earlier than the rest.) |
| `DELTA_TICK_BEFORE_SET` | **fixed → flipsAt 5.** flybye render drv @0x40ff83 == fr08: trailing-edge tick. Build-date flip (OLD in early-v5 fr-022). |
| `DELTA_SUBFRAME_RENDER` | **fixed → flipsAt 5.** same render driver: sub-frame chunks. Build-date flip. |
| `DELTA_RVB_E_FULLPREC`  | **fixed → flipsAt 5.** syReverbSet @0x40f89e == fr08: no SRfclinfreq. Build-date flip (fr-022 OLD, candytron NEW @0x41f27d). |

**The trio is build-date-tied, not format-tied.** fr-022 (v5, 2002-08) and
candytron (v5, 2003-08) are the SAME format version with OPPOSITE DSP cores
(fr-022 = old MSVC LCG + baked freq + box; candytron = modern). The flip
landed in that 12-month gap, and the v2m records only format version — so an
early-v5 file is genuinely under-specified. `flipsAt 5` is the best proxy
(OLD v0..v4, NEW v5..v6): correct for every oracle-bearing file and a no-op
for v0/v5/v6 (check.py still 3/3). The lone unrepresentable case is an
early-v5 old-core file like fr-022 — rendering it as new-core measures **89%
relative-RMS / 0.63 correlation** error (dominated by the tonal core, not
just noise; full breakdown in the extraction NOTES). Getting it right needs a
build-era signal the file lacks (the `v2eras.h` "model change" option) —
deferred until/unless early-v5 files enter the corpus.

**Done since:** the flybye render-harness oracle was built
(`c1_flybye_harness.c`) and the v1 claim is now **bit-exact, whole song**
(rel-RMS 4.0e-5, max|d| 1.5e-4, zero samples >1e-3) after the voice-pool fix.

**Update 2026-06-07 (era-gap sweep — `../v2m/era-gap-sweep-results` in memory,
+ `../v2m/brullwurfel-extraction/`):** 11 more period binaries unpacked/assayed,
giving the FIRST v3 and v4 engines. Three rows moved off the flipsAt-5 proxy:
- `DELTA_PGMCHANGE_V0` → **flipsAt 4 EV_PROVEN** (v3 fr014/fr-022party OLD
  ctl7=127; v4 fr019 NEW same-prog early-out — both sides at the MS2002 party).
- `DELTA_NO_FM_OSC` → **flipsAt 4 EV_PROVEN** (oscjtab mode5 OFF thru v3, fsin
  FM at v4 fr019 @0x4282a1; was the last flipsAt-1 ASSUMED).
- `DELTA_POLY_16` → **flipsAt 3** (pool 32 PROVEN at v3 fr014 @0x410030; the
  16→32 growth is v2-or-v3, no v2 binary, so still ASSUMED for that 1-version gap).
NOISE_LCG/FREQ_CONST + TICK/SUBFRAME/RVB_E STAY flipsAt-5 proxies (OSC_BOXFILTER
left the group — pinned at v3 PROVEN by the v4 oracle, §9) but the sweep
confirmed their build-date flip is the one month **2002-08→09** (old-core thru
fr-027 v5 2002-07, new at fr-028 brullwurfel 2002-09; fr-027 joins fr-022 as an
early-v5 old-core unrepresentable case). check.py still 3/3 (these gates are
no-ops on the v0+v6 corpus).

## 4. v1–v4 originals: structural-only validation (no oracle)

We have genuine period files spanning the gap, in-repo:

| file | era | location |
| --- | --- | --- |
| `tpinv2.v2m` / `tpinv.v2m`        | v1 | RG2/flybye, RG2/ViruzII |
| `whatever07.v2m` / `whateverload2.v2m` | v3 | RG2/einschlag |
| `loading.v2m`                     | v4 | RG2/Viewer |
| `drumtro3.v2m` / `invtro.v2m`     | v5 | RG2/dopplerdefekt, RG2/welcome_to |

These files are detected at their native version, canonicalized to the v6
*layout* (value-preserving), and rendered with their *native-era behavior*
(the era gates fire on the detected v1/v3/v4, not on v6). They render through
the portable and are **deterministic + chunk-invariant** (verified 2026-06-06:
identical bytes across reruns and chunk 4096 vs 333).

**Caveat — this is a liveness check, not a fidelity check.** Determinism +
chunk-invariance only proves the renderer is deterministic; it does NOT prove
the v1–v4 era *behavior* is correct (a renderer that wrongly played everything
at v6 would pass the same test). **v1 is now oracle-proven** (flybye, §3):
the embedded tpinv2 (byte-identical to the repo `tpinv2.v2m`) renders
whole-song bit-exact against the 2001 binary itself.

**Update 2026-06-07:** the era-gap sweep surfaced **v3 (fr014, fr-022party) and
v4 (fr019) period engines** (assayed, §3), and the brullwurfel (fr-028, v5)
**render oracle** is built (`../v2m/brullwurfel-extraction/c3_oracle.c`). So v3/v4
are no longer binary-less, and v5 has a second proven render anchor besides
candytron. **Update 2026-06-08: v3 (fr014, §8) AND v4 (fr019, §9) now have full
render oracles** — v3/v4 osc rendering is render-proven (fr019 whole-mix rms
3.9e-6), and the v4 oracle corrected the BOXFILTER flip point (§9). **v2 still
has no binary at all** — it remains liveness-only. kkrieger6's native-v5 pool
note stands (saturates 32 voices at 102.47 s; josie never does).

**Update 2026-06-22 (the farbomat v2 lead — closed):** the V2 author Tammo
Hinrichs suggested fr-minus-03 *farbomat* as the only plausible carrier of a
**format-version-2** .v2m (v1 = fr-013 Nov 2001, v3 = fr-014 late Dec 2001), but
warned he may have "bumped the version 2× between releases." Carved
(`fr-minus-03-party.zip`, exe 2001-12-28): one valid 9-channel song that the
loader detects as **format version 3** — same as fr-014, both late-Dec-2001. So
the lead is closed: farbomat is now a third genuine **v3** corpus file
(`corpus/embedded/farbomat.v2m`, determinism gate only — no render oracle yet),
and the **v2 format-version slot is still empty**. The POLY 16→32 "v2-or-v3"
ambiguity in §3 is therefore unchanged (it needs a true v2-format file, which
no known release provides).

## 5. CC1 mod-dest remap re-audit (low priority)

Open lead from the 6.2 hunt: re-verify the v5→v6 mod-dest remap puts these on
the right canonical param — mod3 dest67=boost.amount, mod4 dest65=aux2/delay-
send, mod5 dest13=osc2.vol. No observed divergence; flagged for completeness.

## 6. Cross-host / cross-arch determinism (task 8.2) — CLOSED 2026-06-20

**Status: PROVEN cross-arch.** The whole corpus renders **bit-identical on
aarch64** to the checked-in x86_64 `baselines.sha256`. Method: cross-build a
static `v2dump` with `aarch64-linux-gnu-g++` (same `-ffp-contract=off -O2 -std=
c++17` policy) and run `test/check.py --dump <qemu-aarch64 wrapper>`. Result:
14/14 OK, including the transcendental-heavy binaries (kkrieger FM, fr019,
candytron) — same sha256 on both arches. This validates the determinism design
end-to-end: own polynomial transcendentals (`v2math.h`) over plain IEEE doubles,
FMA contraction off, no x87 80-bit.

Caveat on QEMU: user-mode qemu emulates aarch64 *instructions* but links the
host glibc — so it proves the player's own arithmetic is arch-independent, not a
real ARM libm. That residual gap is now also closed at the source: the audio
path no longer calls any *approximating* libm function. The two set-time
`cos(boost)`/`sin(boost)` calls (`v2core.cpp:714-715`) — the last violators of
the `v2math.h` "no approximating libm" policy — were routed through the
deterministic `vm::cosf24`/`vm::sinf24` (new `cosCore`, 2026-06-20). All
remaining `<math.h>` use is the IEEE correctly-rounded / exact subset (`sqrt`,
`fabs`, `ldexp`, `frexp`, `fmod`, `trunc`, `lrint`), which is bit-identical on
every conforming arch by definition. The cosCore change moved **zero** baselines
(byte-identical on x86_64 for every corpus song), so it was free.

## 7. Portable v5 vs the brullwurfel v5 oracle (~0.148 residual)

**Status (2026-06-07): LOCALIZED + PROVEN a real brullwurfel→candytron
modern-core sub-era delta. Documented as an unrepresentable early-modern-core
case (mirrors §3's early-v5 case). The portable is NOT at fault — it is
bit-exact against its candytron reference.**

The brullwurfel (fr-028, 2002-09) render oracle (`../v2m/brullwurfel-extraction/
c3_oracle.c` + NOTES) renders song1 (= the fr08 ".the .product" song re-exported
to v5) through brullwurfel's OWN synth. Against the portable v5 path (same v2m,
60 s): envelope correlation **0.9994**, **identical peak**, but a sample-level
**rms-diff/rms ≈ 0.148** — not bit-exact. The hunt (channel-solo bisection +
three-way oracle + patch dump + image constant-scan) ran it all the way down:

**1. Channel-solo (`C3_SOLO`/`V2SEQ_SOLO`).** The whole 0.148 collapses onto a
single channel, **ch1** (rms-diff 0.01665 of the 0.0167 total; every other
channel is bit-exact at ~2e-9 or silent in the first 60 s). ch1 is silent until
~36 s, then diverges from its very first sample — *not* an accumulating drift.

**2. Three-way oracle (the decisive test).** Rendering song1 ch1 through
candytron's OWN engine too (`c2_oracle_solo` + `/tmp/candytron/unpacked.bin`):

| pair | peak A / B | rms-diff | verdict |
| --- | --- | --- | --- |
| portable vs candytron (2003) | 0.383627 / 0.383627 | 1.2e-09 | **BIT-EXACT** (1 ULP) |
| candytron (2003) vs brullwurfel (2002) | 0.383627 / 0.170289 | 1.67e-02 | the divergence |
| portable vs brullwurfel (2002) | 0.383627 / 0.170289 | 1.67e-02 | same divergence |

So the portable reproduces candytron to the last ULP; **both** the portable and
candytron diverge from brullwurfel identically. The 0.148 is a genuine v5
sub-era delta between the earliest modern core (Sep-2002) and candytron
(Aug-2003), not a portable defect.

**3. Root cause: ch1's full-gain NOISE oscillator.** ch1's patch (pgm1, serial
filters) is `osc0/osc1 = PULSE` at gain 39/47 and **`osc2 = OSC_NOISE` at full
gain 127**, through a high-resonance serial filter (flt0 mode3 cutoff102
reso106). The two builds' ch1 renders are **decorrelated (corr 0.16)** with all
energy in a ~3.5 kHz resonant band — the signature of *differing noise* shaped by
the same resonant filter (the quiet pulses are identical and supply the residual
0.16 correlation). c3 is byte-stable across runs, so the noise is deterministic,
just build-specific.

**4. Mechanism: same LCG, different seed.** Both unpacked images contain the
SAME modern noise LCG (196314165 / 907633515 — brullwurfel @0x4369, candytron
@0x1441b) *and* the MSVC LCG, so `DELTA_NOISE_LCG_MSVC` is correct (both NEW).
The decorrelation is therefore a **different per-voice noise seed** feeding the
same LCG (identical generator + different start = fully decorrelated). At v5 the
seed is `seedMix(userSeed, rdtsc→0, idx)`; brullwurfel's early-modern build seeds
it differently from candytron, and the v2m carries no build-era signal to tell
the two apart.

**Disposition:** unrepresentable early-modern-core sub-era case, same shape as
§3's early-v5-old-core (fr-022/fr-027). The portable models candytron, the later
canonical v5, bit-exact; noise-seed divergence between builds is the irreducible
class already noted for rdtsc seeding. NOT promoted to an era row (no
early-modern file in the corpus to serve, and a noise *seed* — unlike the LCG
constant — is not a behavior the v2m can carry). Re-open only if an early-modern
noise-heavy v2m enters the corpus AND brullwurfel's exact seed init is decoded
(disasm the syOsc noise-mode seed at the brullwurfel noise-gen site).

Tooling added this session: `C3_SOLO` in `c3_oracle.c` (mirrors `C2_SOLO`/
`V2SEQ_SOLO`); `V2_PATCHDUMP=<ch>` in `v2core.cpp` `storeV2Values` (dev-only,
`#ifndef NDEBUG`, dumps a channel's post-mod voice config). check.py 3/3 (genuine-only).

NOTE: the brullwurfel unpacked image + carved song1 live in scratch (re-unpack
`~/downloads/fr-028.zip` via `../v2m/toolkit/era`; song1 is carve index 1). The
candytron image re-unpacks from `~/downloads/fr-030_candytron_final.zip`
(kkrunchy) to `/tmp/candytron/unpacked.bin`.

## 8. v3 (fr014) render oracle — built; ch8 divergence SOLVED

**Status (2026-06-08, SOLVED): the v3 render oracle is built and the ch8
divergence is FIXED. ROOT CAUSE: the CHANNEL mod matrix must skip voice-private
mod sources (>= 8: aenv/env2, lfo1/lfo2, note) at v3/v4 — they have no
channel-level meaning. fr-014 ch8 (pgm7) carries `aenv -> comp.outgain` and
`lfo1 -> chorus.amount` mods; the portable wrongly applied them, pushing the
channel compressor makeup from outgain 98 to 128 (the clamp) for ~3.7x extra
gain (plus the chorus perturbation) = the ~5x ch8 blow-up. FIX:
`DELTA_CHANMOD_NO_VOICE_SRC` (flipsAt 5) — the v3/v4 stores skip src>=8
(`cmp al,8; jae`, fr014 @0x40fe39, fr019 @0x429900); the modern-core v5+ store
applies all sources (bit-exact v5/v6 corpus). RESULT: whole-song v3 oracle corr
0.947 -> 0.990, ch8 peak 3.19 -> amplitude-matched (~0.68 like the binary).
check.py stays 3/3, release render sha unchanged. The remaining ch8 residual
(corr ~0 on a noise channel, peaks matched) is the §7-class noise-seed phase
decorrelation amplified by the chorus feedback comb — NOT a new bug.**

**Overturned suspects (history): noise resonator (719fe10), amplitude-envelope/
curvol (f15a2b7), and "channel comp/chorus makeup era-difference" (the prior §8)
were ALL wrong. The first two were broken-probe artifacts; the third was the
right STAGE (comp/chorus) but the wrong CAUSE — the comp/chorus algorithms and
their parsed params are bit-faithful; only the mod-source filter differed.**

`../v2m/fr014-extraction/c1_fr014_harness.c` is the first **render** oracle for
format v3 (previously assay-only, §3). It drives mark&sweep's (fr-014, 2001-12)
own player on the in-image v3 song (carve 0), rdtsc-pinned, deterministic
(byte-stable). VAs: OpenV2M 0x40d93d, PlayV2M 0x40da6d, RenderProxy 0x40d7f3
(stdcall ret8), synthRender 0x40ff6f (sets x87 **PC=24** @0x40ff71), synthInit
0x40feb2 (SYN@0x50a374 sz 0x1e27b4, 32 voices @0x50c184 stride 0x210),
playing@0x476af0, rdtsc 0x40e4a0/0x40ead9/0x40ec01. Ronan init (call 0x40df02)
nop'd. Build: `gcc -m32 -no-pie -O0 c1_fr014_harness.c -o c1_fr014_harness`.

**Result:** portable-v3 vs the v3 binary is corr 0.947 / median sample ratio 1.0
(most of the mix bit-faithful). The whole divergence is **ch8** (pgm7: three
full-gain NOISE oscs → HP filter flt0 mode3 → LP flt1 mode1 → BITCRUSHER mode3
→ vol). Portable ch8 peak **3.19**, binary **0.63** (~5×). Other channels
bit-exact ~2e-9 or matching ~5e-4.

**WHAT WAS MEASURED (2026-06-08 follow-up — gdb on the binary + new taps).**
The earlier curvol/env story rested on a voice-workspace probe that read the
WRONG voice slot with brullwurfel-borrowed offsets; every number it produced
(curvol≈0.118, "flt0 left open") was an artifact. Re-measured from scratch:

1. **Noise oscillator: BIT-EXACT.** gdb at `renderNoise` entry (0x40e7e1) shows
   the binary's runtime coeffs are `nffrq`, `nfres=1.0`, `gain=0.992`, `seed=0`
   — identical to the portable. Stepping `renderNoise_v0` 21 samples from seed 0
   reproduces the binary's frame-2 state **to the bit** (`seed=3522190791,
   sl=0.371471, sb=-0.645311`). The "noise resonator divergence" (commit 719fe10)
   is FALSE. The decoded recurrence (`bb=sb+sl·f; h=(n−sl)·r−bb; ll=sl+h·f;
   out=(h+bb+ll)·gain`) is `renderNoise_v0` verbatim. x87 PC=24/53/64 all give
   the same peak (13.29) — precision is not it either.
2. **Whole per-voice chain matches.** gdb buffer peaks: OSC sum ≈ **13.3**
   (portable VCETAP 13.29), DIST (post-bitcrusher voice buffer) = **1.969**
   (portable 1.97). The amp-EG `Amplify` (voice-param[37], velocity-modded) reads
   **125** in the binary vs **124** portable — gain matches. flt0 cutoff closes
   to ~93 in BOTH. So osc/filter/bitcrusher/curvol are all faithful.
   (The earlier NOP-based "binary osc = 0.11" was ALSO an artifact — NOPing the
   filter calls corrupted the shared voice buffer. gdb is the reliable probe.)
3. **The divergence is the CHANNEL FX chain.** Per-stage peaks for ch8:
   - portable voice-sum into chanbuf (1 voice): **1.36** — matches the binary.
   - portable after channel COMPRESSOR: **3.21** (~2.4× makeup).
   - portable after channel CHORUS: **9.18** (~2.8×, feedback comb).
   - portable pre-master mix: **5.38**; final **3.19**.
   - binary pre-master channel mix (0x509b30): **1.80**; final **0.628**.
   Empirical isolation (portable, dev gates): baseline 3.19; no-chorus **1.16**;
   no-comp **0.99**; **no-comp + no-chorus 0.49** ≈ binary **0.628**. So the
   portable's channel compressor + chorus over-amplify a signal the v3 binary
   leaves near unity. (corr is ~0 because ch8 is noise — use peak/RMS, not corr.)

**THE FIX (how it was found).** `0x40fda8` is the channel value STORE (loads
patch bytes [0x39..0x51] → the 25-float value array @0x510384+ch*0x64, then runs
the channel mod matrix), tail-calling the channel SET `0x40fc69` then the per-
sample RENDER `0x40fcda` (stage map: COMP 0x40f945, BOOST 0x40f40a, then
DIST 0x40ee75 / CHORUS 0x40f612 swapped by the fxr flag [+0xc]; NO dcf at v3 —
exactly the portable's comp→boost→[dist↔chorus] order). The stage isolation
(FR014_NOCOMP/… vs V2_MIXTAP) localized the over-amplification to the channel
COMP. The portable's comp.set RECEIVED the correct params (mode1/thr9/outg98,
== binary value[16..24]) but RENDERED with outgain 128: a controller mod was
inflating it. V2_CHANPARM (post-mod param dump) + V2_MODREMAP showed the mod
`src=8 (aenv) -> dest comp.outgain` (and `src=10 (lfo1) -> chorus.amount`). The
binary keeps outgain pinned at 98 across the whole song because its channel store
SKIPS src>=8 (`cmp al,8; jae` @0x40fe39). Encoded as `DELTA_CHANMOD_NO_VOICE_SRC`
in storeChanValues. The comp/boost/dist/chorus engines, their param parse, the
v3 channel-param layout, and the aux-param defaulting are all bit-faithful — only
the channel mod-source filter was missing.

**Evidence for the flip (flipsAt 5):** OLD (skip src>=8) PROVEN at v3 (fr014
@0x40fe39) and v4 (fr019 @0x429900, same `cmp al,8; jae` after the dest-range
`cmp al,0x39/0x52`). NEW (apply all) at the modern-core v5+ store (brullwurfel
v5 has no such range/source check) — proven by the bit-exact v5/v6 corpus, which
carries src>=8 channel mods and only matches if they are applied. Build-date
proxy at v5 like the osc/player rows (an early-v5 old-core file would still
skip). The fr014 binary IS ground truth (native v3); matched, not converted-v6.

**Reusable taps (dev-only, `#ifndef NDEBUG`, env-gated, output-neutral; committed):**
- portable `v2core.cpp`: `V2_PATCHDUMP=<ch>` (post-mod voice config + mod matrix),
  `V2_VCETAP` (per-stage osc/flt/dist/dcf peaks), `V2_NSEED` (noise seed +
  nffrq/nfres hex), `V2_VOL` (curvol/flt0.cfreq/flt1.cfreq/env2.out/aenv.out),
  `V2_OSCONSET` (first-16 osc-buffer samples at onset), `V2_MIXTAP` (per-master-
  stage peaks premix/reverb/delay/dcf/lchc/compr + per-channel-stage CHTAP
  dcf1/comp/boost/dist/chorus + `[chanN] voicesum/nvoices` + CHTAP entry/rxaux),
  `V2_COMPDUMP` (channel-comp mode/thresh/ratio/outgain/autogain → invol/outvol),
  `V2_CHANPARM=<ch>` (that channel's POST-MOD param array + decoded comp config,
  on pgm/outgain change — the tap that cracked this), `V2_CHANTRACE` (per-channel
  pre/post-process peak + the channel comp's runtime mode/invol/outvol/net/curgain
  + fxr), `V2_NOCOMP` / `V2_NOCHORUS` (skip a channel-FX stage = stage isolation).
- portable `v2load.cpp`: `V2_MODREMAP` (raw v3 dest → remapped v6 dest per mod;
  `src=N` is the mod source — 0 vel, 1..7 ctl, 8/9 aenv/env2, 10/11 lfo).
- harness `c1_fr014_harness.c`: `FR014_SOLO=<ch>` (binary-side channel solo),
  `FR014_DUMPVOX=<sec>` (one-shot voice dump: per-voice param[37] Amplify + aenv +
  osc nffrq/nfres + `[ch8 chanvals]` post-mod value array + decoded `[ch8 COMP]`),
  `FR014_BUFPEAK` (running max of voicebuf 0x509730 [post-dist] + chanmix 0x509b30
  [pre-master] + onset samples), `FR014_NOCOMP/NOBOOST/NODIST/NOCHORUS` (NOP the
  channel-FX render call(s) = binary stage isolation, == portable V2_NOCOMP/…),
  `FR014_STAGE=osc|flt` / `FR014_NOISERAW` (voice-chain NOPs — UNRELIABLE, corrupt
  the shared buffer; prefer gdb). The TRUSTWORTHY binary probe is **gdb** at VAs.
- Scratch: `era unpack <party.exe> <out.bin>`; fr014 → carve 0 = song0.v2m (v3,
  the divergent song), carve 1 = song1.v2m (v1); fr019 (v4) and fr-028 (v5
  brullwurfel) unpacked for the flip evidence. Portable solo: `V2SEQ_SOLO=8`.

NOTE: every earlier suspect is now disproven — the 719fe10 "color misparse"
(binary nfres=1.0), the f15a2b7 "amplitude-envelope/curvol" (Amplify=125 matches),
and the prior §8 "channel comp/chorus makeup era-difference" (right stage, wrong
cause: the comp/chorus math and parsed params are bit-faithful). The real cause
is the channel mod-source filter (`DELTA_CHANMOD_NO_VOICE_SRC`).

## 9. v4 (fr019) render oracle — built; overturned the BOXFILTER flip point

**Status (2026-06-08, SOLVED): the v4 render oracle is built and it caught a
real era-row error. ROOT CAUSE: the tri/saw/pulse box->analytic-OSM oscillator
rewrite happens at the v1->v3 format step, NOT at the v5 build-date as the
flipsAt-5 `DELTA_OSC_BOXFILTER` proxy assumed. The proxy was set from flybye
(v1, box) + candytron (v5, OSM) and INTERPOLATED across v2-v4 -- but v3/v4 were
never checked. They are OSM. FIX: `DELTA_OSC_BOXFILTER` flipsAt 5 -> 3 (PROVEN),
decoupled from the NOISE_LCG/FREQ_CONST pair it was bundled with; AND the
integral renderTriSaw/renderPulse now advance the phase at freq<<2 when
old(FREQ_CONST) (v3/v4 keep the 4x-oversample freq convention even though the
renderer is the analytic OSM -- fr019 pulse @0x428111 does `shl esi,2`), mirror-
ing renderSin_v0. The box-vs-OSM divergence is then GONE (ch3 pulse corr -1.0 ->
1.0 max|d|=0). fr014 (v3) whole-song corr 0.990 -> 0.99989; flybye (v1)/fr08 (v0)
unchanged (box untouched); check.py 3/3 (genuine-only) (v0/v5/v6 corpus is unaffected by a
v3/v4-only change). BUT the box->OSM fix alone left fr019 audibly wrong from
t=20.3 s -- that turned out to be TWO MORE structural v4 bugs in the FM oscillator
(not a floor; see "FM oscillator" below). With all three fixes, fr019 whole-song
(341 s) is 0 of 341 sec above 3%, overall rms-diff/rms 0.12%, listening A/B
indistinguishable.**

`../v2m/fr019-extraction/c1_fr019_harness.c` is the v4 ORACLE. poemtoahorse
(fr-019, ms2002 2002-03) fuses OpenV2M+PlayV2M into one stdcall @0x4107f0 (parses
header -> globals timediv@0x448b98/maxtime@0x448ba0/gdnum@0x448ba8, builds 16
channel tables @0x448bb4 stride 0x50, calls Reset @0x41003d, sets the playing
BYTE @0x448148 := 1). RenderProxy @0x410716 (ret 8) calls synthRender @0x429a36.
synthInit @0x429979 (SYN @0x4cacf0, 32 voices @0x4ccb00 stride 0x210, `mov
cl,0x20` = v4 POLY 32). rdtsc 0x427ee4/0x42856b/0x428693. No Ronan on the path.
embedded v4 song = carve 0 @0x41a7cd (timediv 480, 14 active ch). VAs signature-
matched against fr014's known V2MPlayer methods (OpenV2M imul-0x2710 prologue,
synthRender's PC=24 `66 25 fff0; 66 0d 3f00`, the byte playing-flag RenderProxy).
Build: `gcc -m32 -no-pie -O0 c1_fr019_harness.c -o c1_fr019_harness`.

**How it was found.** Whole-mix diff DIVERGE (rms 0.037). Channel-solo bisection
(`FR019_SOLO`=binary, `V2SEQ_SOLO`=portable): ch2 (noise) bit-exact, ch3 (3x
pulse) carried the whole residual at **corr exactly -1.0, magnitudes matched** --
a pure sign inversion, localized to OSC_PULSE (ch4 = 1 pulse + 2 tri/saw diverged
partially). The v1 linchpin: flybye (v1) ch7 pulse is `corr +1.0 max|d|=0`
BIT-EXACT vs the current (box) renderer, so the box sign is right at v1 -- a real
era difference, not a global bug. The asm settled it: fr014(v3) pulse @0x40e6cf /
tri/saw @0x40e595 and fr019(v4) @0x428111 / @0x427fd9 are the analytic OSM
(utof23 + `fdivr` gain/f + the osm state-machine jump), byte-identical to each
other, while flybye(v1) tri/saw @0x40e70a is the 4x box (`mov cl,4; fldz`). The
-1.0 was box-vs-OSM (opposite polarity convention); the residual 4% magnitude
after fixing the flip was the freq<<2 phase-advance (the OSM at v3/v4 still uses
the old 4x freq). fr08(v0) has 4 pulse channels and is whole-song bit-exact too,
so v0+v1 = box, v3+v4 = OSM: the flip is v1->v3 (v2 has no binary; assumed box).

**v4 was the goal era for the FM-osc path** (`DELTA_NO_FM_OSC` NEW side):
poemtoahorse uses mode5 FM on ch11/ch12 (one ring-modulated). The first-15s
bit-exactness render-proves the FM path that was previously assay-only (fsin
@0x4282a1) -- though the FM channels also carry a slice of the whole-song
residual below. oscjtab confirms it: fr014(v3) mode5 -> off @0x40e6ad, fr019(v4)
mode5 -> FM @0x4282a1.

**FM oscillator -- TWO more structural v4 bugs (2026-06-09, FIXED).** After the
box->OSM fix, fr019 was still audibly wrong (diff ~80% of signal) from t=20.32 s
-- which is exactly when ch11's first FM note enters (the first ~20 s has no FM,
which is why earlier windows looked clean and the original "FM render-proved"
claim was premature). The trigger channel is ch11 (osc1 mode5 FM), found by
whole-mix-aligned onset detection (NOT the binary solo, whose re-Reset shifts
note timing per-channel -- a real gotcha; the whole-mix diff is the trustworthy
signal). Both bugs are in `renderFMSin_v5`, both asm-confirmed from the v4 FM
renderer @0x4282a1, both the v4-vs-v5 FM scheme:
  1. **Carrier freq<<2.** v4 advances the FM carrier at freq<<2 (`shl edx,2; add
     eax,edx` @0x4282a7), like the other oscillators at FREQ_CONST-old;
     renderFMSin_v5 advanced 1x, so the carrier ran at 1/4 rate -- spectrum at
     894 Hz instead of 996. Fixed -> sidebands match, whole-song bad-seconds
     40 -> 10 (60 s).
  2. **Modulation depth 4.0** (NOT freq-gated -- see below). v4 scales the
     modulator by 4.0 (`fmul [0x427e7c]`), twice the 2.0 fcfmmax. With 2.0 the
     timbre is wrong (ch11 magnitude-spectrum corr 0.87, rel-dist 0.57); with 4.0
     it matches (0.998). Fixed -> whole-song 10 -> 0 bad-seconds.
The carrier freq<<2 is gated `old(DELTA_OSC_FREQ_CONST)` (v4 only -- candytron's
FM @0x41e060 has NO `shl edx,2`). But the **depth 4.0 is NOT gated**: the v5
candytron FM uses 4.0 too (@0x41dbd0, verified 2026-06-10) -- the integer FM
scheme (v4 AND v5) is 4.0; only the SEPARATE v6 float scheme (renderFMSin, the
2004 synth.asm line-85 `fcfmmax 2.0`) is 2.0. So `renderFMSin_v5` hardcodes 4.0
for v4+v5; the 2.0 it used before was a latent v5 bug (the v6 value reused).
Confirmed against the candytron oracle: candytron's own v5 FM channel (ch6) now
matches the portable to magnitude-spectrum corr 1.0000 (rel-dist 0.0). v6
unchanged, check.py 3/3 (genuine-only) (the v5 FM path is not in that corpus -- its v5 songs
are v6-converted).
RESULT: fr019 whole-song (341 s) 0/341 sec above 3%, overall rms-diff/rms 0.12%,
listening A/B indistinguishable.

**Method lesson (this section was WRONG twice before this rewrite):** "whole-mix
MATCH 3.9e-6" was a 10 s window; "ch6 pulse / §1 resonance-amplified ULP floor"
was a mis-attribution off the timing-skewed binary solo. Each "sub-perceptual
floor" call turned out to be a structural bug found by pushing harder. Judge a
DEPTH/timbre change by the MAGNITUDE spectrum, not waveform correlation (which is
phase-dominated): waveform corr stayed -0.69 while the depth fix took the
magnitude match 0.87 -> 0.998. The remaining 0.12% is a genuine carrier-phase/
keysync offset (waveform -0.69, magnitude 0.998) + transcendental ULP -- below
the perceptual floor (confirmed by the whole-song measure AND listening, not an
assumption). The exact phase op is the only open item; not chased (inaudible).
RULED OUT for the big divergence: rdtsc clock (pinned), voice-steal, x87-vs-SSE
(faithful `-m32 -mpc32` build BYTE-IDENTICAL) -- it was plain wrong constants.

**Reusable taps (committed):** `FR019_SOLO=<ch>` (binary channel solo via
notenum-zeroing @0x448bac+ch*0x50 + re-Reset), `FR019_BUFPEAK` (output peak).
Scratch: `era unpack ~/downloads/fr019_party.zip's fr-019-party-b.exe
/tmp/fr019_unpacked.bin`; carve 0 = the v4 song. fr-022 party (also ms2002, v3,
has pulse) and the fr019 pulse/oscjtab disasm are the cross-checks.

## 10. v5 corpus verification — fr-024 & fr-029 PROVEN, kkrieger blocked

**Status (2026-06-10): two of the three previously-unverified v5 songs are now
oracle-proven faithful; the third is blocked on its packer.** This closes the
"determinism-only" gap for the v5 corpus that §4 flagged — the portable v5 model
was proven against candytron only; now fr-024 and fr-029 are proven against
*their own* engines too.

Method: a generic v5 own-engine oracle (`../v2m/toolkit/v5_oracle.c`) — the same
source-ported `_viruz2.cpp` player as the candytron c2, parametrised by `-D` flags
for the 4 synth-entry VAs + rdtsc sites of each binary. Entry VAs were located by
address-free code fingerprints against candytron's (the v5 synth source is
byte-identical across releases). Full per-song writeups + reproduce commands in
`../v2m/{fr024,fr029,kkrieger}-extraction/NOTES.md`.

| song | era | whole-song corr | rms\|d\| | peak orc/port | disposition |
| --- | --- | --- | --- | --- | --- |
| fr-024 | v5 | **0.999991** | 0.0010 | 1.140 / 1.130 | PROVEN — ε-floor (bit-exact first 60s; late-song razor-tie max\|d\| 0.026 @182s) |
| fr-029 | v5 | **0.999557** | 0.0051 | 1.162 / 1.160 | PROVEN — ε-floor (late-song 0.009 band == candytron's; max\|d\| 0.35 @145s, 1-sample tie) |
| kkrieger | v5 | **0.99999** (`eras::kkrieger2004`) / 0.46 (default Auto/v5 int FM) | 0.0013 | 0.239 / 0.241 | PROVEN ε-floor via the **Era profile** `eras::kkrieger2004` (base v5 + NATIVE_FSIN/FPATAN→New). Default Auto stays int-FM (faithful to fr029); caller names the profile (§ below) |

Both fr-024/fr-029 are the same irreducible native-vs-x87 / continuous-osc-phase
razor-tie class already documented for candytron (§1) and fr019 (§9): bit-exact
or sub-µ early, a 1-ULP transcendental tie nudges a keysync=0 osc phase late in
the song, per-second corr stays ≥0.998 throughout. Sub-perceptual. Both songs are
spsize=0 (no ronan), so no speech path was exercised. `era assay` of both matches
candytron's modern-core v5 signature exactly (modern noise LCG; no baked oscfreq,
no v6 oscseeds/fcdcoffset).

**kkrieger unpack SOLVED (2026-06-10); engine runs; all channels render.**
kkrieger-beta is **kkrunchy_k7**-packed (newer than candytron's kkrunchy; source in
repo at `kkrunchy_k7/`), which adds an x86 **split-stream disasm filter** (opcodes
split from operands; reversed by stage `depack2.asm`/`DisUnFilter`). The generic
`era unpack` faulted at the stub's import-resolution loop (no Windows loader →
`call [LoadLibraryA]` faults) **before** the un-filter ran, leaving `.text` in split
form — hence the earlier "fild-heart present 7×/0×" and bare-operand LCG findings.
**Fix:** stub LoadLibraryA/GetProcAddress so the import loop completes → depack2
un-filters → coherent code (`kkrieger-extraction/kkr_unpack.py`; image base 0x7c0000,
not 0x400000). The own-engine oracle loads and RUNS kkrieger's genuine V2 engine and
renders **all 13 channels** (peak 0.153). Not a blanket kkrunchy limit; candytron
(older kkrunchy) was always fine.

**The earlier "osc-amplitude silence" was a MISDIAGNOSIS** (corrected 2026-06-10). It
is *not* a missing osc-gain global / brüllwürfel SR-const class. The first ~21 s is
genuinely **ch15-only** (every other channel's tick-0/12 events are note-OFFs, vel 0;
real notes start tick ~3072), and ch15 is the **ronan speech channel** — `RenderBlock`
routes `cl==15` through `syRonanProcess`, which vocodes the carrier to **silence
without lyrics**. NOP the ch15 ronan guard (`KK_NORONAN`) and the intro renders the raw
carrier (peak 0.27). (The portable defaults to `V2_RONAN 0`, so it too leaves ch15 raw;
neither side currently drives speech.) **Instrument fidelity, ch15 excluded:** whole-mix
corr 0.85 (peaks/rms within 3 %); per-channel solos — tonal ch7 **0.998** (sample-
aligned; a tiny ≈326 Hz 5th-harmonic timbre residual), FM ch11 0.77 (energy matches,
phase decorrelates). The FM/noise/drum channels phase-decorrelate — the same
continuous-osc-phase / noise-seed razor-tie class as fr024/fr029 late-song (§ above) and
brüllwürfel §7, but heavily exercised by kkrieger's FM bass + drums.

**True speech A/B: genuine ronan speaks; portable ch15 near-silent — LOCALIZED to the
CARRIER, not ronan (2026-06-11).** Found `synthSetLyrics` @0x80742f (reads wsptr
0x84286c, `rep movsd` 64 ptrs → texts 0x842a00) and wired the oracle's `ssReset` to call
it (`-DVA_LYRICS`). The genuine engine vocodes real speech on ch15 (peak 0.21, rms 0.038,
326–457 Hz F1/F2). The portable (`V2_RONAN=1`) feeds the same lyrics + note-ons yet ch15
is near-silent (peak 0.015, rms 0.003, corr 0.05). **The "ronan version/era difference"
guess is DISPROVEN; the bug is the ch15 oscillator carrier.** Evidence (full writeup +
repro in `kkrieger-extraction/NOTES.md` §3):
- **ronan is identical & faithful.** `werkkzeug3_kkrieger/ronan.cpp` ≡ the forked
  `v2/ronan.cpp` (syls/phonemes/`db2lin`/post-EQ all equal; the only different variant,
  RG2/ViruzII, is *quieter*). Traced: the sequencer walks the marker-less lyric
  `AH27OH40…` correctly (curp2 60→39→42→25→16), `a_voicing≈0.397`, formant gains right.
  ronan vocodes whatever carrier it gets. (kkrieger is the only `!`/`_`-free, free-run
  lyric in the corpus — candytron/josie are all `!`-gated — but that path is proven here
  too, so the candytron corr-0.99966 validation was *not* the gap.)
- **The carrier is harmonic-collapsed.** ch15 raw-carrier energy matches the genuine
  (rms 0.039 vs 0.041) but its spectrum is ~all <200 Hz (sub-200 ≈12300 vs ~250 above
  500 Hz); the genuine carrier is broadband. A formant filter is a high-Q bandpass at
  500–3000 Hz → with no harmonics there, ronan outputs silence (retains 8% of carrier
  energy vs the genuine's 86% → the 11×).
- **Root cause = the FM-osc PHASE SCHEME (root-caused 2026-06-11).** ch15 patch pgm12 runs
  three same-pitch oscs (osc0 PULSE, osc1 SAW, osc2 FM mode 5, 65 Hz); V2's FM osc *replaces*
  the buffer using pulse+saw (±2) as its modulator, so the carrier = the FM output. The
  portable renders v5 FM with the **integer `fistp` scheme** (`renderFMSin_v5`:
  `fistp(mod·4·2³¹)`), which **overflows 100 %** on the ±2 modulator (x87 fistp →
  `0x80000000`) → degenerate pure sine. kkrieger-beta's 2004 engine uses the **float-phase
  scheme** (`renderFMSin`: `(mod·2 + phase_float)·2π`, `fastsinrc`) → rich FM. **Proven from
  both binaries:** candytron FM @0x41e066 = integer/`fistp`/native-`fsin` (depth 4.0);
  kkrieger FM @0x8086a2 = float/`fadd`/`fastsinrc` (depth 2.0). NOT a per-channel "razor
  phase"/cancellation effect — it is a deterministic scheme mismatch.
- **Fix PROVEN (not yet gated in).** Dispatching kkrieger's FM to `renderFMSin` makes the
  ch15 carrier match the genuine (sub-200/above-500 8370/6838 vs 8372/6839), ch15 speech
  **corr 1.000** (100 % energy, was 11× too quiet), and — because every FM channel ran the
  same broken scheme — the **whole 30 s mix** vs the own-engine oracle goes **corr 0.44 →
  0.9995**, rms|d| 0.039 → 0.0013, max|d| 0.013: **ε-floor**, the fr024/fr029 razor-tie band.
  So the fix promotes kkrieger from "energy-faithful, not bit-exact" to **own-engine-proven
  at ε-floor**, and subsumes the old "FM/noise razor-phase" residual.
- **Root of the ambiguity = build-era split inside format v5 (no auto-gate).** fr029 (v5,
  ~2003) uses **integer** FM and is proven faithful with it (flip → regresses 0.91); kkrieger
  (v5, 2004) uses **float**. Both fingerprint as v5 (identical `globSize`=22 + patch-param-
  count) and **no content heuristic separates them**: candytron (2003) has speech+ch15+integer
  FM; and fr029's FM modulator *also* overflows yet wants the degenerate integer result — so
  neither "uses speech/ch15" nor "FM overflows" implies float. The era model assumed float-FM =
  v6 (2012); kkrieger-beta proves it shipped in 2004. **2nd build-era ambiguity** after the
  brüllwürfel noise-seed sub-era (§7). The format version *cannot* express it; the caller must
  supply the missing coordinate — see the Era resolution below.

**RESOLVED (2026-06-12) — the Era interface.** The format version is a lossy proxy for the
engine BUILD that rendered a song (the score, not the orchestra); a few ledger rows flip
mid-version on the build-date timeline the format can't see. The player now takes an **`Era`**
(`v2eras.h`) = a format-version base + a sparse per-row override of the ledger. `Era::Auto()`
(default) = the detected version (every clean case, byte-identical to before); a caller who
knows the provenance names a profile from the **`eras::` catalog**, each entry one disassembled
binary. The pinned straddler:

```cpp
inline constexpr Era kkrieger2004 = Era::v(5)
                                      .with(DELTA_NATIVE_FSIN,   Era::New)   // sine+FM poly @0x808178
                                      .with(DELTA_NATIVE_FPATAN, Era::New);  // overdrive fastatan @0x808100
```

PROVEN by disasm of the unpacked binary (image base 0x7c0000): kkrieger's sine osc AND FM both
call the fastsin **poly** @0x808178 (Horner, double coeffs 0x8080a8/b0/b8, no `fsin`), and the
per-sample overdrive is the fastatan **poly** @0x808100 (rational, coeffs 0x8080c8..0x808110,
`fdiv`, no `fpatan` — the only native `fpatan` in the synth are the era-independent π/4 const +
set-time `odGain2`). Everything else stays v5: NO_DCOFFSET old (no 2^-18 bias in the render),
POLY_32 old (pool 32) — so kkrieger is a **mixed intermediate build**, NOT v6, and forcing v6
would wrongly inject the DC bias + grow the pool to 64. The transcendental cluster flipped as a
unit; the float FM falls out of `NATIVE_FSIN` being New (the dispatch at `v2core.cpp` already
keyed FM on that row — no new code there). `open(kkrieger.v2m, len, eras::kkrieger2004)` →
whole-30s corr vs the genuine-engine oracle **0.46 → 0.99999** (no-ronan oracle), ε-floor.

Representation is two 32-bit masks, so every `eras::` entry is `constexpr` and folds every gate
at compile time (no longer needs `V2_VER_MIN == V2_VER_MAX`). The plumbing: `Player::open(...,
const Era&)` (+ a back-compat `int` overload) → `V2MPlayer::SetEra` → `synthSetEra` →
`V2Instance::era`; `old(DELTA_X)` routes through it, with a single-version fast path that bypasses
the masks (cross-era overrides are unrepresentable when the other version's code isn't compiled).

**Residuals.** (a) the small ch7 ≈326 Hz 5th-harmonic timbre delta (the only thing between the
*profiled* song and bit-exact). ronan is faithful and needs no change. The FM-scheme gating that
was blocking is now closed by the Era interface above.

---

### Scrapped / descoped

- **ALSA live playback tool `v2play` (task 9.0b)** — descoped by user
  2026-06-06. `libv2redux` stays dependency-free; offline rendering is
  fully covered by `v2dump` (WAV/f32, whole-song or fixed-length). No
  `v2play.c` was ever committed, so nothing to remove. Out of scope for this
  change; revisit only if an embedding consumer needs live ALSA output.
