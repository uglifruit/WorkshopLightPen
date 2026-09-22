// Mode 3 — Barcode Reader.
//
// Black and white, not colour: the three LDRs are summed to one brightness,
// which is what a printed barcode actually varies. (Coloured stripes still
// work — they are read by their lightness.) Summing also trebles the signal
// and averages the three gels' noise.
//
// Hold Down and swipe across the code; release and it loops. The take is cut
// into ELEMENTS — runs of black or white — by a Schmitt trigger on the
// brightness, so what loops is the code's own bar pattern rather than a
// sampled waveform:
//
//   Pulse Out 1   every element, black or white
//   Pulse Out 2   only elements wider than the take's average: the wide bars
//   CV Out 2      the scanned brightness
//   Audio Out 1   the current element's width, average width = half scale
//   Audio Out 2   high through black elements, low through white: the code
//                 itself as a gate
//
// Main: playback speed, 1/8x to 8x, original speed in a dead zone at noon.
// Keep Y (smoothing) low while scanning, or the bars blur into each other.

#pragma once
#include "engine.h"

namespace lp {

class BarcodeMode : public Engine
{
public:
	void ControlTick(const SensorFrame &f, const Ctrl &c, EngineOut &out) override;
	void AudioTick(const SensorFrame &f, const Inputs &in, EngineOut &out) override;
	void OnDownPress() override;
	void OnDownRelease(bool afterHold) override;

	/// 2.73s at one sample per control tick (1.5kHz). Faster than the old
	/// 375Hz: a hand swipe puts a lot of bars through in a second, and the
	/// width of each one is the whole point here.
	static constexpr int kMaxLen   = 4096;
	static constexpr int kMaxElems = 256;

private:
	struct Elem
	{
		uint16_t start;
		uint16_t width;
		uint8_t  dark;
	};

	static constexpr uint32_t kMinLen       = 16;   // ~10ms: shorter is a slip
	static constexpr uint16_t kMinElemWidth = 2;    // ignore a flicker this narrow
	static constexpr int      kAnalyseChunk = 64;   // samples per control tick

	void StartAnalysis();
	void AnalyseChunk();
	void FinishAnalysis();

	uint8_t Sample(int take, uint32_t i) const { return rec_[take][i]; }
	int Spare() const { return 1 - active_; }

	// Two takes: the new one is recorded and analysed in the spare while the
	// old one keeps playing, and only swaps in once it is known to be good.
	uint8_t  rec_[2][kMaxLen];
	Elem     elems_[2][kMaxElems];
	uint16_t elemCount_[2] = { 0, 0 };
	uint16_t meanWidth_[2] = { 1, 1 };
	uint32_t len_[2] = { 0, 0 };
	int      active_ = 0;
	bool     hasTake_ = false;

	// Recording
	bool     recording_ = false;
	uint32_t recLen_ = 0;
	uint8_t  recMin_ = 255;
	uint8_t  recMax_ = 0;

	// Analysis
	bool     analysing_ = false;
	uint32_t cursor_ = 0;
	uint32_t elemStart_ = 0;
	uint16_t count_ = 0;
	bool     dark_ = false;
	uint8_t  lo_ = 0, hi_ = 0;

	// Playback
	uint32_t pos_ = 0;       // Q16 samples
	uint16_t elemIdx_ = 0;
	int      trig_[2] = { 0, 0 };
	int32_t  target_[3] = { 0, 0, 0 };   // brightness, width, black gate
	int32_t  smooth_[3] = { 0, 0, 0 };
};

} // namespace lp
