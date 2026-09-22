// Mode 7 — Jog Wheel.
//
// Same take as Mode 4, played the other way round. Instead of the wand
// placing the head, the loop runs by itself at normal speed and the wand
// sets its SPEED, like a hand on a turntable: mid-grey leaves it at 1x,
// brighter drives it faster, darker drags it down through a standstill and
// into reverse.
//
// Main: how hard the wand pushes, from a gentle nudge to a full shuttle.
// Blue: the platter's weight — how quickly the speed follows your hand.
// Red: low-pass cutoff. Down: record a new take (as Mode 4).
// CV Out 2 is the play position, Pulse Out 1 fires at the loop start, and
// Pulse Out 2 is high while it runs backwards.

#pragma once
#include "engine.h"
#include "svf.h"

namespace lp {

class JogMode : public Engine
{
public:
	void OnEnter() override;
	void ControlTick(const SensorFrame &f, const Ctrl &c, EngineOut &out) override;
	void AudioTick(const SensorFrame &f, const Inputs &in, EngineOut &out) override;
	void OnDownPress() override;
	void OnDownRelease(bool afterHold) override;

private:
	static constexpr int32_t kMaxRate = 4 * kQ16One;   // +/-4x
	static constexpr int32_t kDeadZone = 2048;         // of +/-32768: rest is exactly 1x
	static constexpr int32_t kJogRes = 12000;

	int32_t  rate_ = kQ16One;
	int32_t  rateTarget_ = kQ16One;
	int32_t  idx_ = 0;
	int32_t  frac_ = 0;
	uint8_t  inertia_ = 6;
	int      trig_ = 0;
	bool     wrapped_ = false;
	Svf      svf_;
};

} // namespace lp
