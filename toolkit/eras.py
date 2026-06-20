#!/usr/bin/env python3
"""eras -- read era-delta behavior out of an unpacked V2 synth image.

Scan for the constants/opcodes that distinguish the 2000-core from the 2004-core
(the candytron-assay method). Presence of a constant/opcode in the synth region
means that code path exists in that binary; absence of the modern variant (these
64ks ship only the player, no editor) means the old path is the only one. Cross-check
addresses against the synth region (the rdtsc cluster + fpatan locate it).
"""

import struct
import re

IMG_BASE = 0x400000

CONSTS = {
    # --- DELTA_NOISE_LCG_MSVC: 2000 = MSVC rand() LCG; 2004 = different LCG ---
    "MSVC LCG mul 214013 (old)":      struct.pack("<I", 214013),
    "MSVC LCG add 2531011 (old)":     struct.pack("<I", 2531011),
    "modern LCG 196314165 (new)":     struct.pack("<I", 196314165),
    "modern LCG 907633515 (new)":     struct.pack("<I", 907633515),
    # --- DELTA_OSC_FREQ_CONST: 2000 = baked const; 2004 = runtime fcoscbase ---
    "baked oscfreq 3185015.0 (old)":  struct.pack("<f", 3185015.0),
    # --- DELTA_RDTSC_SEED: 2000/v5 = rdtsc; v6 = fixed seed table ------------
    "fixed oscseed 0xdeadbeef (v6)":  struct.pack("<I", 0xDEADBEEF),
    "fixed oscseed 0xbaadf00d (v6)":  struct.pack("<I", 0xBAADF00D),
    "fixed oscseed 0xd3adc0de (v6)":  struct.pack("<I", 0xD3ADC0DE),
    # --- DELTA_NO_DCOFFSET: 2004 adds the 2^-18 denormal bias ---------------
    "fcdcoffset 2^-18 (v6 only)":     struct.pack("<f", 1.0 / 262144.0),
}

OPCODES = {
    "rdtsc (0f31)":  b"\x0f\x31",   # osc-noise / S&H / dist seeds
    "f2xm1 (d9f0)":  b"\xd9\xf0",   # calcfreq pow2
    "fscale (d9fd)": b"\xd9\xfd",
    "fpatan (d9f3)": b"\xd9\xf3",   # overdrive native atan (old until v6)
    "fsin (d9fe)":   b"\xd9\xfe",   # osc/LFO native sine (old until v6)
}


def assay(data):
    """Return {'constants': {name: [va,...]}, 'opcodes': {name: [va,...]}}."""
    consts = {name: [IMG_BASE + m.start() for m in re.finditer(re.escape(pat), data)]
              for name, pat in CONSTS.items()}
    opcodes = {name: [IMG_BASE + m.start() for m in re.finditer(re.escape(pat), data)]
               for name, pat in OPCODES.items()}
    return {"constants": consts, "opcodes": opcodes}


def format_report(data):
    nz = 100 * sum(1 for b in data if b) / len(data) if data else 0
    out = [f"image: {len(data)} bytes, nonzero {nz:.2f}%", "", "constants:"]
    r = assay(data)
    for name, locs in r["constants"].items():
        out.append(f"  {name:34} {'FOUND ' + str([hex(x) for x in locs[:4]]) if locs else 'absent'}")
    out.append("")
    out.append("opcodes:")
    for name, locs in r["opcodes"].items():
        out.append(f"  {name:18} count={len(locs):3} {[hex(x) for x in locs[:6]]}")
    return "\n".join(out)
