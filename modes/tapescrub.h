// Mode 4 — Tape Scrubber (ColourGrid KAOSS pad).
//
// Hold Down and Audio In 1 records for exactly as long as you hold it (up to
// Tape::kMaxLen). Release, and Green — the map's X axis — places the read head
// anywhere in that take, interpolated, with a little inertia so it scrubs like
// tape rather than stepping.
// Red (Y axis) is the filter cutoff, Blue (the diagonal) its resonance, and
// Main walks the filter LP -> BP -> HP.
// Before anything is recorded, and while recording, the live input passes
// through the same filter.
//
// The take is shared with Mode 7, which plays it as a moving loop.

#pragma once
#include "engine.h"
#include "svf.h"

namespace lp {

class TapeScrubMode : public Engine
{
public:
	void OnEnter() override;
	void ControlTick(const SensorFrame &f, const Ctrl &c, EngineOut &out) override;
	void AudioTick(const SensorFrame &f, const Inputs &in, EngineOut &out) override;
	void OnDownPress() override;
	void OnDownRelease(bool afterHold) override;

private:
	int32_t  headQ8_ = 0;   // read position, Q8 samples, smoothed
	Svf      svf_;
	SvfBlend blend_;
};

} // namespace lp
