# v2m/embedded — genuine songs carved from the demo binaries

The GROUND TRUTH corpus: each file is the V2M **embedded in its demo binary**,
carved with `toolkit/era carve <unpacked.bin> --extract 0` at its NATIVE format
version. No converted files — a converted V2M is never a basis for any conclusion.
Reproducible from `~/downloads/<zip>` via `toolkit/era unpack` + `era carve`
(the packed release binaries stay out of the repo).

| file | era | demo | source release (sha256) | v2m sha256 | note |
| --- | --- | --- | --- | --- | --- |
| `fr08.v2m` | v0 | fr-08 .the .product (final) | `fr08_final.zip` `12a3a2099555…` | `36e0546d709cca86…` |  |
| `flybye.v2m` | v1 | fr-013 flybye (unofficial) | `fr013_unoff.zip` `60b203067426…` | `1ebaf51d6e71ad44…` |  |
| `fr014.v2m` | v3 | fr-014 mark&sweep | `fr014.zip` `ea7f2d3a506e…` | `d505ce35fd29528f…` |  |
| `fr022_party.v2m` | v3 | fr-022 ein schlag (ms2002 party) | `fr-022.zip` `54ca9808bc26…` | `a7b279bb95d92334…` |  |
| `fr019.v2m` | v4 | fr-019 poem to a horse (ms2002 party) | `fr019_party.zip` `1fa7e27a8989…` | `9c7e7e07bead2067…` |  |
| `brullwurfel.v2m` | v5 | fr-028 brüllwürfel (musicdisk) | `fr-028.zip` `2dfa53e5c470…` | `75a6192810da581e…` |  |
| `candytron.v2m` | v5 | fr-030 candytron (final == party) | `fr-030_candytron_final.zip` `3e1b7ae4577c…` | `5bf17fb8d58d608c…` |  |
| `fr022_final.v2m` | v5 | fr-022 ein schlag (final) | `fr-022_final.zip` `2774a44e0401…` | `54c5b6982be94402…` | early-v5 old-core (§3: renders era-inaccurate) |
| `fr024.v2m` | v5 | fr-024 | `fr024.zip` `eeffa806d154…` | `0d2ae1f73a1f6841…` |  |
| `fr027.v2m` | v5 | fr-027 out of the blue | `fr-027_final.zip` `43f24097262f…` | `3aa68bf67800b9a4…` | early-v5 old-core (§3: renders era-inaccurate) |
| `fr029.v2m` | v5 | fr-029 | `fr029.zip` `4d6584d61ebb…` | `eb33d567ab798534…` |  |
| `kkrieger.v2m` | v5 | .kkrieger (beta) | `kkrieger-beta.zip` `0de0b9abafd7…` | `a497ccb3c4626d5b…` |  |

Not yet carvable (packed-blob / unpack failures): fr-011, fr-020,
fr-036 zeitmaschine, fr-minus-03-2 (the known-hard ones).
