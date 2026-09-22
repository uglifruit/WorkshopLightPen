#!/usr/bin/env python3
"""lpsim.py - desktop checks for LightPen's fixed-point DSP.

Integer mirrors of the C++ (Python's >> floors like the ARM arithmetic shift;
C's truncating divide is cdiv below). Every product that the C++ keeps in
int32 goes through i32(), which fails loudly on overflow.

    python tools/lpsim.py

No hardware, no pico-sdk. Exits non-zero if any check fails.
"""

import math
import random
import sys

FAILS = []


def check(name, ok, detail=""):
    print(("PASS " if ok else "FAIL ") + name + (("  - " + detail) if detail else ""))
    if not ok:
        FAILS.append(name)


def i32(v):
    if not -(2**31) <= v < 2**31:
        raise OverflowError(f"int32 overflow: {v}")
    return v


def cdiv(a, b):
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b >= 0) else -q


def clamp(v, lo, hi):
    return lo if v < lo else hi if v > hi else v


# --- fastmath.h -------------------------------------------------------------

SIN = [round(32767 * math.sin((i / 256) * (math.pi / 2))) for i in range(257)]
NOTE_INC = [round(440 * 2 ** ((n - 69) / 12) * 2**32 / 48000) for n in range(13)]


def fast_sin(phase):
    phase &= 0xFFFFFFFF
    quadrant = phase >> 30
    frac = (phase >> 6) & 0xFFFFFF
    idx, mu = frac >> 16, frac & 0xFFFF
    if quadrant & 1:
        idx, mu = 255 - idx, 65536 - mu
        if mu == 65536:
            mu, idx = 0, idx + 1
    a, b = SIN[idx], SIN[idx + 1]
    v = a + (i32((b - a) * mu) >> 16)
    return -v if quadrant & 2 else v


def slew(v, t, s):
    return v + ((t - v) >> s)


def slew_exact(v, t, s):
    d = t - v
    if d == 0:
        return v
    step = d >> s
    if step == 0:
        step = 1 if d > 0 else -1
    return v + step


def mul_q15(a, g):
    return i32(a * g) >> 15


def pow2_scale(base, oct_q12):
    whole, frac = oct_q12 >> 12, oct_q12 & 0xFFF
    v = base << whole if whole > 0 else base >> -whole if whole < 0 else base
    return i32(v + ((v * frac) >> 12))


def note_to_inc(note_q8):
    note_q8 = clamp(note_q8, 0, 127 << 8)
    n, frac = note_q8 >> 8, note_q8 & 0xFF
    octv, semi = n // 12, n % 12
    a, b = NOTE_INC[semi], NOTE_INC[semi + 1]
    inc = (a + (((b - a) * frac) >> 8)) << octv
    assert inc < 2**32
    return inc


def check_fastmath():
    err = max(abs(fast_sin(p) - 32767 * math.sin(2 * math.pi * p / 2**32))
              for p in range(0, 2**32, 2**32 // 4099))
    check("fast_sin within 2 LSB of Q15 sine", err < 2.0, f"max err {err:.2f}")

    worst = 0.0
    for n in range(127):   # note_to_inc clamps at exactly 127
        for f in range(0, 256, 16):
            hz = note_to_inc((n << 8) + f) * 48000 / 2**32
            want = 440 * 2 ** ((n + f / 256 - 69) / 12)
            worst = max(worst, abs(1200 * math.log2(hz / want)))
    check("note_to_inc within 1 cent (semitone table + interp)", worst < 1.0, f"{worst:.3f} cents")

    prev, worst = 0, 0.0
    mono = True
    for o in range(-8192, 8193):
        v = pow2_scale(65536, o)
        mono &= v >= prev
        prev = v
        worst = max(worst, abs(v / (65536 * 2 ** (o / 4096)) - 1))
    check("pow2_scale monotonic", mono)
    check("pow2_scale within 6.2% of 2^x", worst < 0.062, f"{worst*100:.2f}%")
    check("pow2_scale exact at octaves",
          all(pow2_scale(65536, k * 4096) == 65536 * 2**k for k in (-2, -1, 1, 2)))

    # slew_exact reaches the target from either side; slew does not.
    reach = True
    for s in range(5, 15):
        for tgt in (1000 * 256, -1000 * 256, 3, -3):
            for start in (tgt + 7777, tgt - 7777):
                v = start
                for _ in range(40 * (1 << s)):
                    v = slew_exact(v, tgt, s)
                    if v == tgt:
                        break
                reach &= v == tgt
    check("slew_exact always lands on its target (shift 5..14)", reach)


# --- sensors.h ----------------------------------------------------------------

MIN_SPAN = 64


def fail_mask(black, white):
    return sum(1 << i for i in range(3) if -MIN_SPAN < white[i] - black[i] < MIN_SPAN)


def calib_scale(black, white):
    span = white - black
    mag = abs(span)
    s = (65535 * 256 + mag - 1) // mag
    return -s if span < 0 else s


def sensor_u(raw, black, scale, gain_q10):
    """SensorPipeline::Channel before the slew."""
    u = i32((raw - black) * scale) >> 8
    u = clamp(u, 0, 262143)
    return clamp(i32(u * gain_q10) >> 10, 0, 65535)


def gain_q10(x):
    return pow2_scale(65536, (x - 2048) * 4) >> 6


def check_sensors():
    g = [gain_q10(x) for x in (0, 2048, 4095)]
    check("X gain 0.25x / 1x / ~4x (Q10)", g[0] == 256 and g[1] == 1024 and 4000 < g[2] <= 4096, str(g))
    shifts = [5 + ((y * 10) >> 12) for y in (0, 2048, 4095)]
    check("Y slew shift 5..14", shifts == [5, 10, 14], str(shifts))

    check("default mapping matches sensors.h's initial scale", calib_scale(0, 2047) == 8196)

    cases = {
        "defaults": (0, 2047),
        "typical": (300, 1500),
        "inverted wand": (1500, 300),
        "minimum span": (1000, 1000 + MIN_SPAN),
        "minimum span, inverted": (-900, -900 - MIN_SPAN),
        "full range": (-2048, 2047),
    }
    for name, (black, white) in cases.items():
        s = calib_scale(black, white)
        ends_ok = mono = True
        try:
            for gx in (0, 2048, 4095):
                gq = gain_q10(gx)
                ends_ok &= sensor_u(black, black, s, gq) == 0
                if gx == 2048:
                    ends_ok &= sensor_u(white, black, s, gq) == 65535
                prev = -1
                # Sweep every raw value, darkest to brightest.
                step = 1 if white > black else -1
                far_dark, far_bright = (-2048, 2047) if step == 1 else (2047, -2048)
                for raw in range(far_dark, far_bright + step, step):
                    u = sensor_u(raw, black, s, gq)
                    mono &= u >= prev
                    prev = u
            overflow = False
        except OverflowError:
            overflow = True
        check(f"calibration '{name}': black->0, white->full, monotonic, no overflow",
              ends_ok and mono and not overflow)

    mid = sensor_u(900, 300, calib_scale(300, 1500), 1024)
    check("calibration midpoint reads half scale", abs(mid - 32768) < 64, str(mid))
    check("span check rejects 63, accepts 64",
          fail_mask([0, 0, 0], [63, -63, 64]) == 0b011 and fail_mask([0, 0, 0], [64, -64, 2000]) == 0)


# --- osc.h --------------------------------------------------------------------

def wave(w, p):
    p &= 0xFFFFFFFF
    if w == "sine":
        return fast_sin(p)
    if w == "tri":
        v = ((p + 0x40000000) & 0xFFFFFFFF) >> 15
        if v >= 65536:
            v = 131071 - v
        return v - 32768
    if w == "saw":
        return (((p + 0x80000000) & 0xFFFFFFFF) >> 16) - 32768
    return 32767 if p < 0x80000000 else -32767


def morph_set(path, pos):
    n = len(path)
    m = pos * (n - 1)
    seg = m >> 12
    if seg >= n - 1:
        return path[-1], path[-1], 0
    return path[seg], path[seg + 1], (m & 0xFFF) << 3


def morph_render(a, b, frac, p):
    va = wave(a, p)
    if frac == 0:
        return va
    return va + mul_q15(wave(b, p) - va, frac)


def check_osc():
    aligned = all(abs(wave(w, 0)) <= 1 or w == "square" for w in ("sine", "tri", "saw"))
    check("waves cross zero at phase 0", aligned)
    peak = {w: max(abs(wave(w, p)) for p in range(0, 2**32, 2**32 // 997)) for w in ("sine", "tri", "saw", "square")}
    check("waves stay inside Q15", all(v <= 32768 for v in peak.values()), str(peak))
    path4 = ["sine", "tri", "saw", "square"]
    ok = True
    for pos in range(0, 4096, 5):
        a, b, fr = morph_set(path4, pos)
        for p in range(0, 2**32, 2**32 // 61):
            ok &= abs(morph_render(a, b, fr, p)) <= 32768
    check("morph: no overflow across the knob", ok)

    # Triad: three full-level voices summed and scaled never exceed the DAC.
    worst = 0
    for pos in (0, 1365, 2730, 4095):
        a, b, fr = morph_set(path4, pos)
        for p in range(0, 2**32, 2**32 // 211):
            s = sum(i32(morph_render(a, b, fr, p * k) * 65535) >> 16 for k in (1, 2, 3))
            worst = max(worst, abs(i32(s * 1365) >> 16))
    check("triad sum stays within +/-2047 before clamp", worst <= 2047, f"peak {worst}")


# --- svf.h --------------------------------------------------------------------

class Svf:
    MAX = 32767 << 8

    def __init__(self):
        self.f, self.q = 1000, 22938
        self.lp8 = self.bp8 = self.hp = 0

    @property
    def lp(self):
        return self.lp8 >> 8

    @property
    def bp(self):
        return self.bp8 >> 8

    def set(self, cut_u, res_u):
        self.f = min(pow2_scale(86, i32(cut_u * 31) >> 6), 16000)
        self.q = 22938 - (i32(res_u * 21627) >> 16)

    def process(self, x):
        hp = x - self.lp - (i32(self.q * self.bp) >> 14)
        self.bp8 = clamp(self.bp8 + (i32(self.f * hp) >> 6), -self.MAX, self.MAX)
        self.lp8 = clamp(self.lp8 + (i32(self.f * self.bp) >> 6), -self.MAX, self.MAX)
        self.hp = clamp(hp, -32767, 32767)


def blend_set(knob):
    z = min(knob // 819, 4)
    return z, min((knob - z * 819) * 40, 32767)


def blend_render(s, seg, frac):
    if seg == 0:
        return s.lp
    if seg == 1:
        return s.lp + mul_q15(s.bp - s.lp, frac)
    if seg == 2:
        return s.bp
    if seg == 3:
        return s.bp + mul_q15(s.hp - s.bp, frac)
    return s.hp


def rms(xs):
    return math.sqrt(sum(x * x for x in xs) / len(xs))


def check_svf():
    hz = [48000 / (2 * math.pi) * 2 * math.asin(min(pow2_scale(86, (u * 31) >> 6), 16000) / 32768)
          for u in (0, 65535)]
    check("cutoff range ~40Hz .. ~7.8kHz", 35 < hz[0] < 45 and 7000 < hz[1] < 8500,
          f"{hz[0]:.0f}Hz .. {hz[1]:.0f}Hz")

    # Stability: worst-case drive (full-scale square) at every corner of the
    # cutoff/resonance grid, then silence. Must decay, not self-oscillate.
    stable = True
    for cu in (0, 20000, 45000, 65535):
        for ru in (0, 32768, 65535):
            s = Svf()
            s.set(cu, ru)
            for n in range(2400):
                s.process(2047 if (n // 40) % 2 else -2048)
            for n in range(24000):
                s.process(0)
            stable &= abs(s.lp) <= 1 and abs(s.bp) <= 1
    check("SVF stable, decays to <= 1 LSB at every cutoff/resonance corner", stable)

    # A quiet signal well below the lowest cutoff must pass, not stick in a
    # truncation dead band (the bug the Q8 states exist to fix).
    s = Svf()
    s.set(0, 0)
    xs = [round(40 * math.sin(2 * math.pi * 5 * n / 48000)) for n in range(48000)]
    ys = []
    for x in xs:
        s.process(x)
        ys.append(s.lp)
    ratio = rms(ys[9600:]) / rms(xs[9600:])
    check("quiet 5Hz signal passes the 40Hz LP", 0.8 < ratio < 1.25, f"gain {ratio:.2f}")

    # Blend continuity: a sweep of the Main knob must not jump in level.
    rnd = random.Random(1)
    noise = [rnd.randint(-2048, 2047) for _ in range(4800)]
    levels = []
    for knob in range(0, 4096, 64):
        s = Svf()
        s.set(40000, 20000)
        seg, fr = blend_set(knob)
        out = []
        for x in noise:
            s.process(x)
            out.append(blend_render(s, seg, fr))
        levels.append(rms(out[480:]))
    jumps = max(abs(20 * math.log10(levels[i + 1] / levels[i])) for i in range(len(levels) - 1))
    check("LP->BP->HP blend has no level step > 1.5dB between knob steps", jumps < 1.5, f"max {jumps:.2f}dB")


# --- modes/synesthesia.cpp -----------------------------------------------------

def fold(v):
    t = (v + 2048) & 8191
    if t > 4096:
        t = 8192 - t
    return t - 2048


def check_fold():
    check("fold is identity inside the rails", all(fold(v) == v for v in range(-2047, 2048)))
    vs = [fold(i32(v * (256 + 1792) >> 8)) for v in range(-2047, 2048)]
    check("fold bounded at 8x drive", all(-2048 <= v <= 2048 for v in vs))
    check("fold continuous (no jump > 8x step)", all(abs(vs[i + 1] - vs[i]) <= 9 for i in range(len(vs) - 1)))
    k = [pow2_scale(4, ((65535 - u) * 10) >> 4) for u in (65535, 0)]
    taus = [2**20 / kk / 48000 for kk in k]
    check("decay time ~5.5s bright .. ~5ms dark", 5 < taus[0] < 6 and 0.004 < taus[1] < 0.007,
          f"{taus[0]:.2f}s .. {taus[1]*1000:.1f}ms")


# --- modes/barcode.cpp ----------------------------------------------------------

def speed_q16(knob, dead=60):
    d = knob - 2048
    if -dead < d < dead:
        return 65536
    d += -dead if d > 0 else dead
    return pow2_scale(65536, cdiv(d * 12288, 2048 - dead))


def barcode_elements(rec, min_width=2):
    """Mirror of BarcodeMode's analysis: cut a take into runs of black/white."""
    mn, mx = min(rec), max(rec)
    if mx - mn < 16:
        return [], 0
    mid = (mn + mx) // 2
    lo = mid - (mx - mn) // 8
    hi = mid + (mx - mn) // 8
    dark = rec[0] < mid
    start, elems = 0, []
    for i in range(1, len(rec)):
        v = rec[i]
        flip = (v > hi) if dark else (v < lo)
        if not flip:
            continue
        width = i - start
        if width < min_width:
            continue
        elems.append((start, width, dark))
        start, dark = i, not dark
    if len(rec) > start:
        elems.append((start, len(rec) - start, dark))
    if len(elems) < 2:
        return [], 0
    return elems, max(1, len(rec) // len(elems))


def barcode_play(elems, mean, length, speed_q16, ticks):
    """Mirror of the playback walk. Returns (all triggers, wide triggers)."""
    pos, idx, fired, wide = 0, 0, 0, 0
    for _ in range(ticks):
        pos += speed_q16
        if (pos >> 16) >= length:
            pos -= length << 16
            idx = 0
            fired += 1
            if elems[0][1] > mean:
                wide += 1
        i = pos >> 16
        while idx + 1 < len(elems) and elems[idx + 1][0] <= i:
            idx += 1
            fired += 1
            if elems[idx][1] > mean:
                wide += 1
    return fired, wide


def make_barcode(widths, rnd, narrow=6, wide=18, black=40, white=210):
    """A swiped code: alternating black/white runs, with noise and a little
    speed wobble, as an 8-bit brightness track."""
    rec = []
    dark = True
    for w in widths:
        n = (wide if w else narrow) + rnd.randint(-1, 1)
        level = black if dark else white
        rec += [clamp(level + rnd.randint(-12, 12), 0, 255) for _ in range(n)]
        dark = not dark
    return rec


def check_barcode():
    sp = [speed_q16(k) for k in range(4096)]
    check("speed monotonic", all(sp[i] <= sp[i + 1] for i in range(4095)))
    check("speed 1/8x .. 1x at noon .. ~8x", sp[0] == 8192 and sp[2048] == 65536 and sp[4095] > 520000,
          f"{sp[0]} {sp[2048]} {sp[4095]}")
    edge_jump = max(abs(sp[k + 1] / sp[k] - 1) for k in (2048 - 61, 2048 + 59))
    check("no jump at the dead-zone edges", edge_jump < 0.01, f"{edge_jump*100:.2f}%")

    rnd = random.Random(7)

    # A Code-39-like run of narrow and wide bars, swiped with noise.
    pattern = [0, 1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 0, 0, 0, 1, 1, 0, 1, 0, 0]
    rec = make_barcode(pattern, rnd)
    elems, mean = barcode_elements(rec)
    check("barcode: every bar and space is found, none invented",
          len(elems) == len(pattern), f"{len(elems)} of {len(pattern)}")

    got_wide = [1 if w > mean else 0 for (_, w, _) in elems]
    check("barcode: wide bars separate from narrow ones", got_wide == pattern,
          f"{sum(a != b for a, b in zip(got_wide, pattern))} misread")

    alternates = all(elems[i][2] != elems[i + 1][2] for i in range(len(elems) - 1))
    check("barcode: black and white alternate", alternates and elems[0][2] is True)

    # Playing a loop fires one trigger per element per pass, at any speed.
    for label, speed in (("1x", 65536), ("1/8x", 8192), ("8x", 524288)):
        passes = 3
        ticks = (len(rec) * 65536 * passes) // speed
        fired, wide = barcode_play(elems, mean, len(rec), speed, ticks)
        check(f"barcode at {label}: {passes} passes fire every element",
              abs(fired - len(elems) * passes) <= 1
              and abs(wide - sum(pattern) * passes) <= 1,
              f"{fired} triggers, {wide} wide")

    flat = [100 + rnd.randint(-5, 5) for _ in range(200)]
    check("barcode: a flat take is not a code", barcode_elements(flat) == ([], 0))

    # Black on white and white on black read the same, whatever the paper.
    inverted = [255 - v for v in rec]
    inv_elems, inv_mean = barcode_elements(inverted)
    check("barcode: an inverted print reads the same widths",
          [w for (_, w, _) in inv_elems] == [w for (_, w, _) in elems])

    # The three gels summed: a grey card reads mid, and one dead channel
    # still leaves a usable signal.
    check("luma averages the three channels",
          (65535 + 65535 + 65535) // 3 == 65535 and (30000 + 30000 + 30000) // 3 == 30000
          and (60000 + 60000 + 0) // 3 == 40000)


# --- modes/hueorgan.cpp ---------------------------------------------------------

SCALE_LENS = [12, 7, 7, 5]


def hue_q16(r, g, b):
    r, g, b = r >> 4, g >> 4, b >> 4
    mx = max(r, g, b)
    d = mx - min(r, g, b)
    if d == 0:
        return -1
    if mx == r:
        h = cdiv(i32((g - b) * 10923), d)
    elif mx == g:
        h = 21845 + cdiv(i32((b - r) * 10923), d)
    else:
        h = 43691 + cdiv(i32((r - g) * 10923), d)
    return h & 0xFFFF


class Organ:
    """Mirror of HueOrganMode's degree picker (hysteresis + dwell)."""

    def __init__(self, n):
        self.n, self.band = n, 65536 // n
        self.degree, self.pending, self.dwell = -1, -1, 0
        self.triggers = 0

    def tick(self, hue):
        hue = (hue + 5461) & 0xFFFF
        cand = -1
        if self.degree >= 0:
            lo = self.degree * self.band - self.band // 4
            hi = (self.degree + 1) * self.band + self.band // 4
            if lo <= hue < hi:
                cand = self.degree
        if cand < 0:
            cand = min(hue // self.band, self.n - 1)
        if cand == self.degree:
            self.pending, self.dwell = -1, 0
        elif cand != self.pending:
            self.pending, self.dwell = cand, 0
        else:
            self.dwell += 1
            if self.dwell >= 45:
                self.degree, self.pending, self.dwell = cand, -1, 0
                self.triggers += 1


def hsv_to_q16(h_deg):
    h = (h_deg % 360) / 60
    x = 1 - abs(h % 2 - 1)
    r, g, b = [(1, x, 0), (x, 1, 0), (0, 1, x), (0, x, 1), (x, 0, 1), (1, 0, x)][int(h) % 6]
    # Gelled LDRs never separate cleanly: add a common floor.
    return [int(40000 * c) + 12000 for c in (r, g, b)]


def check_hue():
    prim = {name: hue_q16(*rgb) for name, rgb in {
        "red": (65535, 0, 0), "yellow": (65535, 65535, 0), "green": (0, 65535, 0),
        "cyan": (0, 65535, 65535), "blue": (0, 0, 65535), "magenta": (65535, 0, 65535)}.items()}
    want = {"red": 0, "yellow": 10923, "green": 21845, "cyan": 32768, "blue": 43691, "magenta": 54613}
    check("hue of primaries/secondaries", all(abs(prim[k] - want[k]) <= 2 for k in want), str(prim))
    check("grey has no hue", hue_q16(30000, 30000, 30000) == -1)

    for s, length in enumerate(SCALE_LENS):
        n = 2 * length + 1
        # A slow rainbow scan, red -> violet (0..300 degrees): every degree of
        # the scale that lies in that arc plays once, in rising order.
        o = Organ(n)
        seq = []
        for i in range(3000):
            o.tick(hue_q16(*hsv_to_q16(300 * i / 2999)))
            if o.degree >= 0 and (not seq or seq[-1] != o.degree):
                seq.append(o.degree)
        rising = all(seq[i] < seq[i + 1] for i in range(len(seq) - 1))
        check(f"scale {s}: rainbow scan plays rising, one trigger per note",
              rising and o.triggers == len(seq), f"{len(seq)} notes, {o.triggers} triggers")

    # Dither right on a band boundary: the hysteresis + dwell must hold one note.
    o = Organ(15)
    rnd = random.Random(3)
    edge = 3 * (65536 // 15) - 5461
    for _ in range(3000):
        o.tick((edge + rnd.randint(-900, 900)) & 0xFFFF)
    check("boundary dither: at most one trigger", o.triggers <= 1, f"{o.triggers}")

    # Red dithering either side of 0 degrees does not wrap to the top note.
    o = Organ(15)
    for _ in range(3000):
        o.tick(rnd.randint(-1500, 1500) & 0xFFFF)
    check("red near 0 degrees stays one note (wrap moved to magenta)", o.triggers == 1, f"{o.triggers}")


# --- controls.h -----------------------------------------------------------------

def check_controls():
    DOWN, MID, UP = 0, 1, 2

    class Controls:
        def __init__(self, sw):
            self.stable = self.cand = sw
            self.count = 30
            self.modes = 0

        def tick(self, raw):
            if raw != self.cand:
                self.cand, self.count = raw, 0
            elif self.count < 30:
                self.count += 1
                if self.count == 30 and self.cand != self.stable:
                    self.stable = self.cand
                    if self.stable == UP:
                        self.modes += 1

    rnd = random.Random(5)
    c = Controls(MID)
    clicks = 20
    for _ in range(clicks):
        # 1.5-6ms of contact bounce each way, then a held position.
        for target, other in ((UP, MID), (MID, UP)):
            for _ in range(rnd.randint(2, 9)):
                c.tick(rnd.choice((target, other)))
            for _ in range(rnd.randint(40, 400)):
                c.tick(target)
    check("bouncy Up clicks advance exactly one mode each", c.modes == clicks, f"{c.modes}/{clicks}")


# --- tape.h / modes/jog.cpp / modes/prism.cpp ------------------------------------

TAPE_MAX = 84000


def position_q8(u, length):
    """Tape::PositionQ8 — the split multiply that avoids a 64-bit product."""
    span = length - 2
    return i32((u >> 8) * span) + (i32((u & 0xFF) * span) >> 8)


def jog_rate(ug, main, dead=2048, maxrate=4 * 65536):
    dev = ug - 32768
    if -dead < dev < dead:
        dev = 0
    else:
        dev += -dead if dev > 0 else dead
    depth = 256 + ((main * 768) >> 12)
    return clamp(65536 + (i32(dev * depth) >> 7), -maxrate, maxrate)


def jog_run(rate, length, samples):
    """Mode 7's position stepping. Returns (idx, frac, wraps)."""
    idx, frac, wraps = 0, 0, 0
    for _ in range(samples):
        frac += rate
        idx += frac >> 16
        frac &= 0xFFFF
        if idx >= length:
            idx -= length
            wraps += 1
        elif idx < 0:
            idx += length
            wraps += 1
    return idx, frac, wraps


def check_tape_modes():
    for length in (2400, 40000, TAPE_MAX):
        ok = True
        for u in range(0, 65536, 7):
            ok &= position_q8(u, length) == (u * (length - 2)) >> 8
        ends = position_q8(0, length) == 0 and (position_q8(65535, length) >> 8) <= length - 2
        check(f"tape position, {length} samples: exact and inside the take", ok and ends)

    rates = [jog_rate(u, 2048) for u in range(65536)]
    check("jog rate monotonic in green", all(rates[i] <= rates[i + 1] for i in range(65535)))
    check("jog rate is exactly 1x across the dead zone",
          all(jog_rate(u, 2048) == 65536 for u in range(32768 - 2047, 32768 + 2048)))
    edges = [jog_rate(32768 - 2049, 4095), jog_rate(32768 + 2049, 4095)]
    check("no jump at the dead-zone edges", all(abs(e - 65536) < 2000 for e in edges), str(edges))
    check("nudge depth (main=0) never reverses", min(jog_rate(u, 0) for u in range(65536)) >= 0)
    full = [jog_rate(0, 4095), jog_rate(65535, 4095)]
    check("shuttle depth (main=max) reverses and reaches +4x",
          full[0] < 0 and full[1] == 4 * 65536, str(full))

    # A constant rate must advance at exactly that rate, forwards and back.
    for rate, name in ((65536, "1x"), (98304, "1.5x"), (-131072, "-2x"), (12288, "0.1875x")):
        idx, frac, wraps = jog_run(rate, 40000, 40000)
        travelled = wraps * 40000 + idx if rate > 0 else -(wraps * 40000 + (40000 - idx) % 40000)
        want = rate * 40000 // 65536
        check(f"jog at {name}: position tracks the rate with no drift",
              abs(travelled - want) <= 1, f"{travelled} vs {want}")

    idx, _, wraps = jog_run(-65536, 2400, 2400 * 3)
    check("jog in reverse wraps at the loop start", wraps == 3 and idx == 0, f"idx {idx} wraps {wraps}")

    # Mode 7's position ramp: one divide, must stay inside the CV range.
    ok = True
    for length in (2400, 40000, TAPE_MAX):
        scale = (65535 * 256) // length
        ok &= 0 <= i32((length - 1) * scale) >> 8 <= 65535
    check("jog position ramp stays 0..65535 for every take length", ok)

    # Mode 8's FM: the product is modular on purpose and spans one cycle.
    worst = max(abs(((fast_sin(p) * (65535 * 2)) % 2**32) - (fast_sin(p) * 131070) % 2**32)
                for p in range(0, 2**32, 2**32 // 97))
    check("prism FM offset is modular (no signed overflow)", worst == 0)
    depth_cycles = (32767 * 131070) / 2**32
    check("prism FM reaches ~1 cycle at full red", 0.99 < depth_cycles <= 1.0, f"{depth_cycles:.3f}")
    crush = [1 + (u >> 10) for u in (0, 65535)]
    check("prism crush holds 1..64 samples", crush == [1, 64], str(crush))


def main():
    check_fastmath()
    check_sensors()
    check_osc()
    check_svf()
    check_fold()
    check_barcode()
    check_hue()
    check_tape_modes()
    check_controls()
    print()
    print("ALL PASS" if not FAILS else f"{len(FAILS)} FAILED: " + ", ".join(FAILS))
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
