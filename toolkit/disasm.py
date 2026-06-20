#!/usr/bin/env python3
"""disasm -- disassemble a flat unpacked V2 image (VA = 0x400000 + offset).

Used to read the signature-less player/render rows out of a binary and compare
across eras. (The canonical CLI form, formerly flybye-extraction/disasm.py; the
fr08 hard-coded one-binary form is retired.)

  disasm(image_bytes, 0x410455, 40) -> [(addr, hexbytes, mnemonic, op_str), ...]
"""

IMG_BASE = 0x400000


def disasm(data, va, n=60, base=IMG_BASE, window=400):
    """Decode up to `n` instructions starting at virtual address `va`."""
    import capstone
    off = va - base
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    out = []
    for i, ins in enumerate(md.disasm(data[off:off + window], va)):
        if i >= n:
            break
        out.append((ins.address, ins.bytes.hex(), ins.mnemonic, ins.op_str))
    return out


def format_lines(insns):
    return "\n".join("%08x  %-20s %s %s" % (a, b, m, o) for (a, b, m, o) in insns)
