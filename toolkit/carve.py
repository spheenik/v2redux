#!/usr/bin/env python3
"""carve -- locate embedded v2m song spans in a flat image.

The canonical parser (formerly fr08-extraction/findv2m3.py, newest/strictest of the
three generations). Validates the v2m structure -- timediv/maxtime/gdnum sanity, the
16-channel note/pc/pb/cc walk, global+patch+speech blob sizes, full InitBase speech
pointer validation -- and keeps non-overlapping spans (earliest-start, then largest).
"""

import struct


def _u32(d, o):
    return struct.unpack_from("<I", d, o)[0]


def _try_parse(d, o):
    n = len(d)
    try:
        if o + 12 > n:
            return None
        timediv = _u32(d, o); maxtime = _u32(d, o + 4); gdnum = _u32(d, o + 8)
        if not (32 <= timediv <= 2000):
            return None
        if not (2000 <= maxtime <= 40_000_000):
            return None
        if not (1 <= gdnum <= 8000):
            return None
        p = o + 12 + 10 * gdnum
        active = 0
        for _ch in range(16):
            if p + 4 > n:
                return None
            notenum = _u32(d, p); p += 4
            if notenum:
                if notenum > 500000:
                    return None
                active += 1; p += 5 * notenum
                if p + 4 > n:
                    return None
                pcnum = _u32(d, p); p += 4
                if pcnum > 500000:
                    return None
                p += 4 * pcnum
                if p + 4 > n:
                    return None
                pbnum = _u32(d, p); p += 4
                if pbnum > 500000:
                    return None
                p += 5 * pbnum
                for _cn in range(7):
                    if p + 4 > n:
                        return None
                    ccnum = _u32(d, p); p += 4
                    if ccnum > 500000:
                        return None
                    p += 4 * ccnum
        if active < 2:
            return None
        if p + 4 > n:
            return None
        gsize = _u32(d, p)
        if not (0 < gsize <= 16384):
            return None
        p += 4 + gsize
        if p + 4 > n:
            return None
        psize = _u32(d, p)
        if not (50 <= psize <= 1048576):
            return None
        p += 4 + psize
        if p + 4 > n:
            return None
        spsize = _u32(d, p); p += 4
        # full InitBase speech validation
        if spsize and spsize < 8192:
            if p + spsize > n:
                return None
            speech = p
            nptr = _u32(d, speech)
            if nptr > 256:                       # would overflow speechptrs[256]
                return None
            for i in range(nptr):
                if speech + 4 + i * 4 + 4 > n:
                    return None
                off = _u32(d, speech + 4 + i * 4)
                if off >= spsize:                # pointer must stay inside speech blob
                    return None
            p += spsize
        return dict(start=o, end=p, size=p - o, timediv=timediv, maxtime=maxtime,
                    gdnum=gdnum, active=active, gsize=gsize, psize=psize, spsize=spsize)
    except Exception:
        return None


def find_v2ms(data, min_size=2000):
    """Return non-overlapping v2m spans (dicts) in `data`, earliest-start first."""
    hits = []
    for o in range(0, len(data) - 12, 1):
        r = _try_parse(data, o)
        if r and r["size"] >= min_size:
            hits.append(r)
    hits.sort(key=lambda h: (h["start"], -h["size"]))
    chosen = []
    covered = 0
    for h in hits:
        if h["start"] >= covered:
            chosen.append(h)
            covered = h["end"]
    return chosen


def extract(data, span):
    """Slice the carved bytes for a span dict (or {'start','end'})."""
    return data[span["start"]:span["end"]]
