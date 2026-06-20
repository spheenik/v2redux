"""era-extraction toolkit -- shared offline tooling for the period-binary hunt.

See README.md. Public API:
    packers.detect_packer(data|path) -> 'aplib'|'kkrunchy'|'ruletool'|'none'
    packers.unpack(path) -> bytes            (flat image)
    carve.find_v2ms(image) -> [span dict]
    eras.assay(image) -> {constants, opcodes}
    disasm.disasm(image, va, n) -> [insn]
"""
