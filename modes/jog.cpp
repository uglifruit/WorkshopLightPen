#include "modes/jog.h"
#include "tape.h"
#include "pico.h"

namespace lp {

void JogMode::OnEnter()
{
	svf_.Reset();
	rate_ = rateTarget_ = kQ16One;
	idx_ = 0;
	frac_ = 0;
}

void JogMode::OnDownPress()
{
	gTape.StartRecord();
}

void JogMode::OnDownRelease(bool)
{
	gTape.StopRecord();
	idx_ = 0;
	frac_ = 0;
	rate_ = kQ16One;
}

void JogMode::ControlTick(const SensorFrame &f, const Ctrl &c, EngineOut &out)
{
	// Green either side of mid-grey, with a dead zone so a steady hand is
	// exactly 1x. Main sets how much a full deflection is worth: +/-1x at the
	// bottom of the knob (a nudge), +/-4x at the top (a shuttle).
	int32_t dev = f.ug - 32768;
	if (dev > -kDeadZone && dev < kDeadZone) dev = 0;
	else dev += (dev > 0) ? -kDeadZone : kDeadZone;
	int32_t depthQ8 = 256 + ((c.main * 768) >> 12);
	rateTarget_ = clamp_i32(kQ16One + ((dev * depthQ8) >> 7), -kMaxRate, kMaxRate);

	// Blue is the platter's weight: shift 2 is a light hand on a 7", 13 is a
	// heavy flywheel that takes a moment to answer.
	inertia_ = static_cast<uint8_t>(2 + ((f.ub * 12) >> 16));

	svf_.Set(f.ur, kJogRes);

	out.pulse1 = trig_ > 0;
	if (trig_ > 0) trig_--;
	if (wrapped_)
	{
		wrapped_ = false;
		trig_ = kTrigTicks;
	}
	out.pulse2 = rate_ < 0;

	if (gTape.HasAudio() && !gTape.Recording())
	{
		// Play position as a ramp. One divide, at control rate.
		int32_t posScale = (65535 * 256) / static_cast<int32_t>(gTape.Length());
		out.cv2 = q16_to_cv5v(clamp_i32((idx_ * posScale) >> 8, 0, 65535));
	}
	else
	{
		out.cv2 = 0;
	}
}

void __not_in_flash_func(JogMode::AudioTick)(const SensorFrame &, const Inputs &in, EngineOut &out)
{
	int32_t sig;
	if (gTape.Recording())
	{
		gTape.Write(in.audio1);
		sig = in.audio1;
	}
	else if (gTape.HasAudio())
	{
		// slew_exact, not slew: the plain shift stalls short of its target,
		// and a rate stuck 0.1x away from 1x is audibly out of tune.
		rate_ = slew_exact(rate_, rateTarget_, inertia_);

		frac_ += rate_;
		idx_ += frac_ >> 16;       // floors, so a negative rate steps back
		frac_ &= 0xFFFF;

		int32_t len = static_cast<int32_t>(gTape.Length());
		if (idx_ >= len)     { idx_ -= len; wrapped_ = true; }
		else if (idx_ < 0)   { idx_ += len; wrapped_ = true; }

		sig = gTape.Read(static_cast<uint32_t>(idx_), frac_);
	}
	else
	{
		sig = in.audio1;
	}

	svf_.Process(sig);
	int16_t s = clamp12(svf_.Lp());
	out.audio1 = s;
	out.audio2 = s;
}

} // namespace lp
