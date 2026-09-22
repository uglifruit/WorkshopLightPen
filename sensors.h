// sensors.h — the shared R/G/B pipeline every mode reads from.
//
// raw -> calibration (black = 0, white = full) -> X-knob gain -> clamp
//     -> Y-knob slew -> SensorFrame
//
// Red is CV In 1, Green CV In 2, Blue Audio In 2 (read as DC), signed 12-bit
// as the library returns them.

#pragma once
#include <cstdint>
#include "fastmath.h"
#include "calibration.h"

namespace lp {

struct SensorFrame
{
	int32_t ur, ug, ub;   // Q16 0..65535: 0 = calibrated black, 65535 = white
};

/// Unipolar Q16 (0..65535) to 0..~5V in CVOut2Precise units (+/-262143 ~ +/-6V).
static inline int32_t __attribute__((always_inline)) q16_to_cv5v(int32_t u)
{
	return (u * 27307) >> 13;
}

/// Unipolar Q16 to 0..~5V on an audio out. The Rev 1 documentation confirms
/// the CV/Audio outputs are bipolar DC-coupled, approximately -6V to +6V.
static inline int16_t __attribute__((always_inline)) q16_to_audio5v(int32_t u)
{
	return static_cast<int16_t>((u * 1706) >> 16);
}

class SensorPipeline
{
public:
	/// Once, at boot. Divides, so never from the audio path. The data must
	/// have passed CalibFailMask(); a zero span here would divide by zero.
	void SetCalibration(const CalibData &d)
	{
		for (int i = 0; i < 3; i++)
		{
			int32_t span = d.white[i] - d.black[i];
			int32_t mag = span < 0 ? -span : span;
			// Q8 scale so that (white - black) * scale >> 8 == 65535. Rounded
			// up so white really reaches full scale. |span| >= 64 caps it at
			// 262140, and 4095 * 262140 still fits int32.
			int32_t s = (65535 * 256 + mag - 1) / mag;
			black_[i] = d.black[i];
			scaleQ8_[i] = span < 0 ? -s : s;
		}
	}

	/// Control rate: the knob laws use pow2_scale, which is too dear per sample.
	void SetKnobs(int32_t xKnob, int32_t yKnob)
	{
		// X: gain, +/-2 octaves (0.25x..4x) around unity at noon, held as
		// Q10 (unity 1024) so the product below fits 31 bits.
		gainQ10_ = pow2_scale(kQ16One, (xKnob - 2048) * 4) >> 6;
		// Y: slew shift 5..14. The floor of 5 (~240Hz) matches the smoothing the
		// library already gives the CV inputs, so Blue — on the audio input,
		// which only gets a 12kHz notch — is no noisier than Red and Green.
		// The top, 14, is a ~340ms time constant.
		shift_ = static_cast<uint8_t>(5 + ((yKnob * 10) >> 12));
	}

	/// Every sample.
	void Update(const RawRGB &raw, SensorFrame &f)
	{
		f.ur = Channel(raw.r, 0);
		f.ug = Channel(raw.g, 1);
		f.ub = Channel(raw.b, 2);
	}

private:
	inline int32_t __attribute__((always_inline)) Channel(int32_t raw, int i)
	{
		int32_t u = ((raw - black_[i]) * scaleQ8_[i]) >> 8;
		// Up to 4x over white, so gain below unity can still pull a bright
		// reading back into range. 262143 * 4096 fits int32.
		u = clamp_i32(u, 0, 262143);
		u = clamp_i32((u * gainQ10_) >> 10, 0, 65535);
		// Slewed after the clamp, so an over-bright flash does not linger.
		// slew_exact, not slew: Modes 1, 3 and 6 threshold-compare this value,
		// and the plain shift stalls short of its target by a
		// direction-dependent amount.
		state_[i] = slew_exact(state_[i], u, shift_);
		return state_[i];
	}

	int32_t black_[3]   = { 0, 0, 0 };
	int32_t scaleQ8_[3] = { 8196, 8196, 8196 };   // the CalibDefaults() mapping
	int32_t state_[3]   = { 0, 0, 0 };
	int32_t gainQ10_    = 1024;
	uint8_t shift_      = 5;
};

} // namespace lp
