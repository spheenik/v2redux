#!/usr/bin/env python3
"""Fidelity sweep: render each corpus song through its genuine period-engine
oracle and through the v2redux port, and compare. This is the CORRECTNESS gate
that test/check.py (determinism only, sha256 vs port-generated baselines) can't
provide -- it is what would have caught the ch15-Ronan silencing bug.

Self-contained: the period images (fidelity/images/*.bin) and oracle harness
binaries (fidelity/oracles/*) are vendored locally (gitignored -- third-party
demo binaries) so the sweep survives a reboot that wipes /tmp. Provenance and
regeneration steps are in fidelity/README.md.

Usage:
  python3 fidelity/sweep.py [--lib build/libv2redux.a] [--secs 40]
Exit code is nonzero if any song fails its correlation threshold.
"""
import os, sys, subprocess, argparse
import numpy as np

FID  = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(FID)
SCR  = os.path.join(FID, "_scratch")
SR   = 44100

# style "c1": harness <image> <out.f32> <chunk> <secs>   (carves in-image v2m)
# style "c2": harness <image> <v2m>    <out.f32> <secs>   (takes a v2m arg)
SONGS = [
    dict(name="fr08",      img="fr08.bin",      orc="c1_fr08",      style="c1",
         v2m="fr08.v2m",      era="fr08",         corr=0.9999,  note="bit-exact (ch15 fix)"),
    dict(name="flybye",    img="flybye.bin",    orc="c1_flybye",    style="c1",
         v2m="flybye.v2m",    era="flybye",       corr=0.9999,  note="bit-exact"),
    dict(name="fr014",     img="fr014.bin",     orc="c1_fr014",     style="c1",
         v2m="fr014.v2m",     era="fr014",        corr=0.999,   note="documented 0.99989"),
    dict(name="fr019",     img="fr019.bin",     orc="c1_fr019",     style="c1",
         v2m="fr019.v2m",     era="fr019",        corr=0.9999,  note="ε-floor"),
    dict(name="candytron", img="candytron.bin", orc="c2_candytron", style="c2",
         v2m="candytron.v2m", era="candytron",    corr=0.999,   note="Ronan speech residual"),
    dict(name="kkrieger",  img="kkrieger.bin",  orc="kkrieger",     style="c2",
         v2m="kkrieger.v2m",  era="kkrieger2004", corr=0.999,   note="needs kkrieger2004 era (FM float)"),
]


def read_mono(path):
    x = np.fromfile(path, dtype=np.float32)
    return x[:len(x) // 2 * 2].reshape(-1, 2).mean(axis=1)


def corr_relrms(a, b):
    n = min(len(a), len(b))
    a, b = a[:n], b[:n]
    va, vb = np.std(a), np.std(b)
    corr = float(np.corrcoef(a, b)[0, 1]) if va > 0 and vb > 0 else 0.0
    rms = float(np.sqrt(np.mean((a - b) ** 2)))
    peak = float(np.max(np.abs(a))) or 1e-9
    return corr, rms / peak


def build_portrender(lib):
    out = os.path.join(SCR, "portrender")
    src = os.path.join(FID, "portrender.cpp")
    if os.path.exists(out) and os.path.getmtime(out) > max(os.path.getmtime(src),
                                                           os.path.getmtime(lib)):
        return out
    cmd = ["g++", "-std=c++17", "-O2", "-ffp-contract=off",
           "-I", os.path.join(ROOT, "src"), src, lib, "-o", out]
    subprocess.run(cmd, check=True)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--lib", default=os.path.join(ROOT, "build", "libv2redux.a"),
                    help="path to a Ronan-ON libv2redux.a (cmake -DV2_RONAN=ON)")
    ap.add_argument("--secs", type=float, default=40.0)
    args = ap.parse_args()
    os.makedirs(SCR, exist_ok=True)

    if not os.path.exists(args.lib):
        sys.exit("libv2redux.a not found: %s\n  build it: cmake -S . -B build "
                 "-DV2_RONAN=ON && cmake --build build --target v2redux" % args.lib)
    render = build_portrender(args.lib)

    secs = "%g" % args.secs
    fails = 0
    print("%-10s %-8s %-9s %-9s %s" % ("song", "verdict", "corr", "rel-rms", "note"))
    for s in SONGS:
        img = os.path.join(FID, "images", s["img"])
        orc = os.path.join(FID, "oracles", s["orc"])
        v2m = os.path.join(ROOT, "corpus", "embedded", s["v2m"])
        for p in (img, orc, v2m):
            if not os.path.exists(p):
                print("%-10s MISSING  (%s)" % (s["name"], p)); fails += 1; break
        else:
            of = os.path.join(SCR, s["name"] + "_oracle.f32")
            pf = os.path.join(SCR, s["name"] + "_port.f32")
            if s["style"] == "c1":
                ocmd = [orc, img, of, "4096", secs]
            else:
                ocmd = [orc, img, v2m, of, secs]
            subprocess.run(ocmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
            subprocess.run([render, v2m, pf, secs, s["era"]],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
            corr, relrms = corr_relrms(read_mono(of), read_mono(pf))
            ok = corr >= s["corr"]
            fails += not ok
            print("%-10s %-8s %-9.5f %-9.5f %s" % (
                s["name"], "PASS" if ok else "FAIL", corr, relrms, s["note"]))

    print()
    print("FIDELITY: %s (%d/%d songs match their oracle)" % (
        "OK" if fails == 0 else "FAIL", len(SONGS) - fails, len(SONGS)))
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
