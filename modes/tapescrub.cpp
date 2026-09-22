#include "modes/tapescrub.h"
#include "tape.h"
#include "pico.h"

namespace lp {

void TapeScrubMode::OnEnter()
{
	svf_.Reset();
}

void TapeScrubMode::OnDownPress()
{
	gTape.StartRecord();
}

void TapeScrubMode::OnDownRelease(bool)
{
	gTape.StopRecord();
}

void TapeScrubMode::ControlTick(const SensorFrame &f, const Ctrl &c, EngineOut &)
{
	svf_.Set(f.ur, f.ub);
	blend_.Set(c.main);
}

void __not_in_flash_func(TapeScrubMode::AudioTick)(const SensorFrame &f, const Inputs &in, EngineOut &out)
{
	int32_t sig;
	if (gTape.Recording())
	{
		gTape.Write(in.audio1);
		sig = in.audio1;                       // monitor what is going down
		headQ8_ = 0;
	}
	else if (gTape.HasAudio())
	{
		// Inertia on the head: Green arrives in ADC-sized steps, which would
		// click without it. ~2.7ms.
		headQ8_ = slew(headQ8_, gTape.PositionQ8(f.ug), 7);
		sig = gTape.Read(static_cast<uint32_t>(headQ8_ >> 8), (headQ8_ & 0xFF) << 8);
	}
	else
	{
		sig = in.audio1;
	}

	svf_.Process(sig);
	int16_t s = clamp12(blend_.Render(svf_));
	out.audio1 = s;
	out.audio2 = s;
}

} // namespace lp
