#!/usr/bin/env python3
"""tap -- read typed data at a virtual address in an unpacked image (STATIC).

The data-side sibling of disasm: where disasm decodes code at a VA, tap decodes
data. Reads the unpacked image FILE at (va - 0x400000) and interprets it as a
typed value (u32/i32/u16/u8/f32/f64) or raw bytes/hex. Replaces the ad-hoc
`struct.unpack` one-liners for "what value sits at this position".

NOTE: this is a STATIC peek at the on-disk image. Runtime signal-chain data (osc/
flt/dist buffers, voice/filter workspace state) does not exist in the static image
-- it is computed inside the running -m32 oracle and must be tapped there with the
oracle.h tap helpers (oracle_u32/oracle_buf/oracle_tap_*). A pointer slot that the
binary fills at startup reads as 0 here.

  read(data, 0x41dca8, 'f32')        -> [value]
  read(data, 0x400000, 'hex', 8)     -> '4d 5a 66 61 ...'
"""

import struct

IMG_BASE = 0x400000

_FMT = {
    "u32": ("<I", 4), "i32": ("<i", 4),
    "u16": ("<H", 2), "i16": ("<h", 2),
    "u8":  ("<B", 1), "i8":  ("<b", 1),
    "f32": ("<f", 4), "f64": ("<d", 8),
}


def read(data, va, typ="u32", count=1, base=IMG_BASE):
    """Decode `count` items of `typ` at `va`. typ 'hex'/'bytes' => raw bytes."""
    off = va - base
    if off < 0 or off >= len(data):
        raise ValueError("va %#x outside image (%d bytes, base %#x)" % (va, len(data), base))
    if typ in ("hex", "bytes"):
        return data[off:off + count]
    if typ not in _FMT:
        raise ValueError("unknown type %r (use %s|hex|bytes)" % (typ, "|".join(_FMT)))
    fmt, sz = _FMT[typ]
    return [struct.unpack_from(fmt, data, off + i * sz)[0] for i in range(count)]


def format_read(data, va, typ="u32", count=1, base=IMG_BASE):
    vals = read(data, va, typ, count, base)
    if typ in ("hex", "bytes"):
        return " ".join("%02x" % b for b in vals)
    if typ == "f32" or typ == "f64":
        return "  ".join("%.9g" % v for v in vals)
    return "  ".join(("%#x" % v if typ.startswith("u") else str(v)) for v in vals)
