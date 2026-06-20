#!/usr/bin/env python3
"""bufcmp -- compare/inspect raw float32 render buffers (the oracle contract).

The verification primitive used at every migration step: load two interleaved
stereo f32 dumps and report sample-count match, signal level (rule out silence),
max|d| / rms, count over tolerance, and the first divergence point. numpy-
accelerated (a max|d| over the fr08 663 s render -- ~58 M floats / 234 MB -- is
~50 ms vs ~30 s for a pure-Python loop); falls back to array if numpy is absent.

Supersedes the inline `array('f')...max|d|` one-liners and the pure-Python
v2/validate/compare.py.

  compare(a, b, eps=0.0) -> dict
  samples(path, start, n) -> list of floats  (extract a window of points)
"""

try:
    import numpy as _np
except Exception:
    _np = None


def _load(path):
    if _np is not None:
        return _np.fromfile(path, dtype="<f4")
    import array
    a = array.array("f")
    a.frombytes(open(path, "rb").read())
    return a


def compare(a_path, b_path, eps=0.0):
    """Compare two f32 buffers. Returns a dict; 'match' is True iff max|d| <= eps."""
    a = _load(a_path)
    b = _load(b_path)
    res = {"n_a": len(a), "n_b": len(b), "eps": eps}
    if len(a) != len(b):
        res.update(length_mismatch=True, match=False)
        return res
    res["length_mismatch"] = False
    if _np is not None:
        d = _np.abs(a - b)
        res["peak_a"] = float(_np.abs(a).max()) if len(a) else 0.0
        res["peak_b"] = float(_np.abs(b).max()) if len(b) else 0.0
        res["max_err"] = float(d.max()) if len(d) else 0.0
        res["rms_err"] = float(_np.sqrt((d.astype(_np.float64) ** 2).mean())) if len(d) else 0.0
        over = _np.nonzero(d > eps)[0]
        res["over"] = int(over.size)
        res["first_div"] = int(over[0]) if over.size else -1
    else:
        import math
        peak_a = peak_b = max_err = sq = 0.0
        first = -1
        over = 0
        for i in range(len(a)):
            pa, pb = abs(a[i]), abs(b[i])
            if pa > peak_a:
                peak_a = pa
            if pb > peak_b:
                peak_b = pb
            dd = abs(a[i] - b[i])
            if dd > max_err:
                max_err = dd
            sq += dd * dd
            if dd > eps:
                over += 1
                if first < 0:
                    first = i
        res.update(peak_a=peak_a, peak_b=peak_b, max_err=max_err,
                   rms_err=math.sqrt(sq / len(a)) if a else 0.0, over=over, first_div=first)
    res["match"] = res["over"] == 0
    return res


def samples(path, start, n):
    """Extract n floats starting at float-index `start` (for inspecting a point)."""
    buf = _load(path)
    return [float(x) for x in buf[start:start + n]]


def format_report(res):
    out = []
    if res.get("length_mismatch"):
        out.append("LENGTH MISMATCH: %d vs %d floats" % (res["n_a"], res["n_b"]))
        out.append("VERDICT         : DIVERGE")
        return "\n".join(out)
    n = res["n_a"]
    out.append("floats compared : %d (%d stereo samples)" % (n, n // 2))
    warn = "  <-- WARNING: near silence" if max(res["peak_a"], res["peak_b"]) < 1e-4 else ""
    out.append("peak |A| / |B|  : %.6f / %.6f%s" % (res["peak_a"], res["peak_b"], warn))
    out.append("tolerance (eps) : %g" % res["eps"])
    out.append("max abs error   : %.9g" % res["max_err"])
    out.append("rms abs error   : %.9g" % res["rms_err"])
    out.append("samples > eps   : %d / %d" % (res["over"], n))
    if res["first_div"] >= 0:
        fd = res["first_div"]
        out.append("first divergence: float #%d (stereo sample %d, %s ch)" % (
            fd, fd // 2, "L" if fd % 2 == 0 else "R"))
    out.append("VERDICT         : %s" % ("MATCH (within eps)" if res["match"] else "DIVERGE"))
    return "\n".join(out)
