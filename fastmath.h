// fastmath.h — fixed-point primitives for the 48kHz audio path.
//
// Ported from WorkshopNibbleDrum's fastmath.h (namespace nko), trimmed to what
// LightPen uses, plus pow2_scale() and note_to_inc() for knob laws and pitch.
//
// Everything here is integer-only. No libm, no float: the build runs with
// -Wdouble-promotion -Wfloat-conversion and the RP2040 has no FPU, so a stray
// double would cost hundreds of cycles inside ProcessSample().
//
// Conventions:
//   Q15  — int32_t, 32768 == 1.0. Used for signed unit values (waveforms, mixes).
//   Q16  — int32_t, 65536 == 1.0. Used for levels and unipolar sensor values.
//   phase— uint32_t, the full 32-bit range == one cycle. Wraps for free.

#pragma once
#include <cstdint>

namespace lp {

constexpr int32_t kQ15One = 32768;
constexpr int32_t kQ16One = 65536;

constexpr int32_t kSampleRate = 48000;
constexpr int32_t kCtrlDiv    = 32;                       // control tick every 32 samples
constexpr int32_t kCtrlRate   = kSampleRate / kCtrlDiv;   // 1500Hz

// ---------------------------------------------------------------------------
// Sine
// ---------------------------------------------------------------------------

// Quarter-wave sine table, Q15. 257 entries so the interpolator can always read
// kSinTable[i+1] without a bounds check.
extern const int16_t kSinTable[257];

/// Sine of a 32-bit phase (full range = one cycle). Returns Q15, -32767..32767.
static inline int32_t __attribute__((always_inline)) fast_sin(uint32_t phase)
{
	uint32_t quadrant = phase >> 30;
	uint32_t frac     = (phase >> 6) & 0xFFFFFF;
	uint32_t idx      = frac >> 16;
	uint32_t mu       = frac & 0xFFFF;

	// Quadrants 1 and 3 traverse the quarter-wave backwards.
	if (quadrant & 1)
	{
		idx = 255 - idx;
		mu  = 65536 - mu;
		if (mu == 65536) { mu = 0; idx++; }
	}

	int32_t a = kSinTable[idx];
	int32_t b = kSinTable[idx + 1];
	int32_t v = a + (((b - a) * static_cast<int32_t>(mu)) >> 16);

	return (quadrant & 2) ? -v : v;
}

// ---------------------------------------------------------------------------
// Smoothing
// ---------------------------------------------------------------------------

/// One-pole slew toward a target: v += (target - v) >> shift.
///
/// STALLS by design once |target - v| < 2^shift, and the stall is asymmetric
/// (arithmetic shift floors toward -inf). Fine for audio smoothing; NOT fine
/// anywhere the value is later compared against a threshold — use slew_exact().
static inline int32_t __attribute__((always_inline)) slew(int32_t v, int32_t target, uint8_t shift)
{
	return v + ((target - v) >> shift);
}

/// One-pole slew that always REACHES its target: if the shifted step rounds to
/// zero but a difference remains, move one LSB. NibbleDrum measured 17 units of
/// direction-dependent error without this guard (tools/ghostsim.py there).
static inline int32_t __attribute__((always_inline)) slew_exact(int32_t v, int32_t target, uint8_t shift)
{
	int32_t d = target - v;
	if (d == 0) return v;
	int32_t step = d >> shift;
	if (step == 0) step = (d > 0) ? 1 : -1;
	return v + step;
}

// ---------------------------------------------------------------------------
// Scaling helpers
// ---------------------------------------------------------------------------

/// Multiply a signal by a Q15 gain. Callers keep |a| * |g| inside 31 bits:
/// a full-scale Q15 difference (65535) times a Q15 weight (<= 32767) just fits.
static inline int32_t __attribute__((always_inline)) mul_q15(int32_t a, int32_t g)
{
	return (a * g) >> 15;
}

/// Clamp to the DAC's signed 12-bit range.
static inline int16_t __attribute__((always_inline)) clamp12(int32_t v)
{
	if (v < -2048) return -2048;
	if (v >  2047) return  2047;
	return static_cast<int16_t>(v);
}

static inline int32_t __attribute__((always_inline)) clamp_i32(int32_t v, int32_t lo, int32_t hi)
{
	return v < lo ? lo : (v > hi ? hi : v);
}

/// base * 2^(octQ12 / 4096), for exponential knob and sensor laws.
///
/// Piecewise-linear within each octave: monotonic, exact at whole octaves,
/// at most ~6% sharp mid-octave — plenty for a control law, not for pitch
/// (pitch uses note_to_inc's semitone table). Negative octaves allowed.
/// 64-bit intermediate, so CONTROL RATE ONLY.
static inline int32_t pow2_scale(int32_t base, int32_t octQ12)
{
	int32_t whole = octQ12 >> 12;          // floors, also for negatives
	int32_t frac  = octQ12 & 0xFFF;
	int64_t v = base;
	if (whole > 0) v <<= whole;
	else if (whole < 0) v >>= -whole;
	return static_cast<int32_t>(v + ((v * frac) >> 12));
}

// ---------------------------------------------------------------------------
// Pitch
// ---------------------------------------------------------------------------

// Phase increments at 48kHz for MIDI notes 0..12 (C-1 .. C0), A4 = 440Hz.
extern const uint32_t kNoteInc[13];

/// MIDI note in Q8 (note * 256) -> phase increment per sample at 48kHz.
/// Semitone table plus linear interpolation, then an octave shift.
/// Uses a divide, so CONTROL RATE ONLY.
static inline uint32_t note_to_inc(int32_t noteQ8)
{
	noteQ8 = clamp_i32(noteQ8, 0, 127 << 8);
	int32_t n    = noteQ8 >> 8;
	int32_t frac = noteQ8 & 0xFF;
	int32_t oct  = n / 12;
	int32_t semi = n - oct * 12;
	uint32_t a = kNoteInc[semi];
	uint32_t b = kNoteInc[semi + 1];
	uint32_t inc = a + (((b - a) * static_cast<uint32_t>(frac)) >> 8);
	return inc << oct;
}

} // namespace lp
