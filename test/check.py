#!/usr/bin/env python3
"""check.py -- portable-player corpus test driver (task 8.1).

Two layers:
  1. DETERMINISM (runs anywhere): render the corpus with ../v2dump and
     compare sha256 hashes against the checked-in baselines.sha256.
     The hash IS the cross-host contract (design D8): same file + seed 0
     + same compiled version range => identical bits on every host.
  2. EPSILON (lab machines, optional): with --oracle-dir pointing at
     reference renders (harness_asm output for v6 files, the C1 fr08
     ground truth for v0), compute per-file rms/max|d| and print the
     table published in the change docs (task 8.4).

Usage:
  ./check.py                         # hash check (uses ../v2dump)
  ./check.py --update                # regenerate baselines.sha256
  ./check.py --oracle-dir DIR        # also compute eps vs DIR/<name>.f32
  ./check.py --dump PATH             # use a different v2dump binary
  ./check.py --seconds N             # render length (default 60)

Renders use chunk 4096; chunk-size invariance is covered by build-time tests.
"""

import argparse, hashlib, math, os, struct, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_V2M = os.path.normpath(os.path.join(HERE, "..", "corpus"))

# corpus: name -> path. GENUINE files only -- the original embedded V2Ms at their
# native era. NO v6-converted files: the portable plays originals directly, so a
# converted corpus is neither needed nor a valid basis for any conclusion (USER
# RULING 2026-06-10). This is a cross-host DETERMINISM gate (same bytes every
# host), not a fidelity check -- fidelity is the era oracles on embedded songs.
def corpus():
    # GENUINE files only: every song carved from a demo binary (v2m/embedded/,
    # rendered at its native era) + the two genuine v6 songs. No converted files.
    emb = os.path.join(REPO_V2M, "embedded")
    files = [(fn[:-4], os.path.join(emb, fn))
             for fn in sorted(os.listdir(emb)) if fn.endswith(".v2m")]
    files.append(("pzero_new",           os.path.join(REPO_V2M, "pzero_new.v2m")))           # genuine v6
    files.append(("v2_zeitmaschine_new", os.path.join(REPO_V2M, "v2_zeitmaschine_new.v2m"))) # genuine v6
    return files

def render(dump, path, out, seconds):
    r = subprocess.run([dump, path, out, str(seconds), "4096"],
                       capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError(f"{path}: v2dump failed: {r.stderr.strip()}")

def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for blk in iter(lambda: f.read(1 << 20), b""):
            h.update(blk)
    return h.hexdigest()

def eps(render_path, oracle_path):
    # stream float32 pairs; rms in double
    n = 0; acc = 0.0; mx = 0.0
    with open(render_path, "rb") as a, open(oracle_path, "rb") as b:
        while True:
            ba = a.read(1 << 20); bb = b.read(1 << 20)
            m = min(len(ba), len(bb)) & ~3
            if m == 0:
                break
            fa = struct.unpack(f"<{m//4}f", ba[:m])
            fb = struct.unpack(f"<{m//4}f", bb[:m])
            for x, y in zip(fa, fb):
                d = x - y
                acc += d * d
                ad = abs(d)
                if ad > mx: mx = ad
            n += m // 4
    return (math.sqrt(acc / n) if n else 0.0, mx, n)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--update", action="store_true")
    ap.add_argument("--oracle-dir")
    ap.add_argument("--dump", default=os.path.join(HERE, "..", "v2dump"))
    ap.add_argument("--seconds", type=float, default=60.0)
    ap.add_argument("--workdir", default="/tmp/v2redux-check")
    args = ap.parse_args()

    os.makedirs(args.workdir, exist_ok=True)
    baseline_file = os.path.join(HERE, "baselines.sha256")
    baselines = {}
    if os.path.exists(baseline_file):
        for line in open(baseline_file):
            line = line.strip()
            if line and not line.startswith("#"):
                h, name = line.split()
                baselines[name] = h

    fails = 0
    new_lines = []
    eps_rows = []
    for name, path in corpus():
        out = os.path.join(args.workdir, name + ".f32")
        render(args.dump, path, out, args.seconds)
        h = sha256(out)
        new_lines.append(f"{h}  {name}")
        if args.update:
            print(f"baseline {name} = {h[:16]}…")
        elif name not in baselines:
            print(f"MISSING baseline for {name} (run --update)"); fails += 1
        elif baselines[name] != h:
            print(f"HASH MISMATCH {name}"); fails += 1
        else:
            print(f"OK  {name}")

        if args.oracle_dir:
            op = os.path.join(args.oracle_dir, name + ".f32")
            if os.path.exists(op):
                r, m, n = eps(out, op)
                eps_rows.append((name, r, m, n))

    if args.update:
        with open(baseline_file, "w") as f:
            f.write(f"# sha256 of v2dump renders: seed 0, {args.seconds:g}s, chunk 4096,\n")
            f.write("# full version range build. The cross-host determinism contract\n")
            f.write("# (portable-determinism spec): these hashes match on EVERY host.\n")
            f.write("\n".join(new_lines) + "\n")
        print(f"wrote {baseline_file}")

    if eps_rows:
        print("\nepsilon vs oracle (rms / max|d| over %d floats):" % eps_rows[0][3])
        for name, r, m, _ in eps_rows:
            print(f"  {name:24s} rms {r:.3e}  max {m:.3e}")

    sys.exit(1 if fails else 0)

if __name__ == "__main__":
    main()
