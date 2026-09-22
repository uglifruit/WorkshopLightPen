// calibration.h — two-point (white / black) calibration of the three LDRs.
//
// Hold Down while powering on, then release. Point the wand at WHITE paper and
// tap Down; then at BLACK card and tap Down. Each tap averages ~0.34s. Per
// channel, black becomes 0 and white full scale, so the three gels read on the
// same scale however much light each one passes, and a wand built with the
// LDRs on the low side of the divider (inverted) calibrates just the same.
// Switch Up at any point cancels and keeps the previous calibration.
//
// The result is saved to flash (calibstore.h) and loaded on every boot.
// This class only captures; main.cpp owns the switch, the save and the reboot.

#pragma once
#include <cstdint>
#include "fastmath.h"

namespace lp {

struct RawRGB
{
	int32_t r, g, b;
};

struct CalibData
{
	int32_t black[3];
	int32_t white[3];
};

/// Less white-to-black difference than this (of +/-2048) is not a reading:
/// the LDR is missing, or both captures saw the same thing. ~0.2V.
constexpr int32_t kMinCalibSpan = 64;

/// What an uncalibrated card assumes: 0V is black, full scale is white.
static inline CalibData CalibDefaults()
{
	return CalibData{ { 0, 0, 0 }, { 2047, 2047, 2047 } };
}

/// Bit i set: channel i (R, G, B) has too small a span to use.
static inline uint8_t CalibFailMask(const CalibData &d)
{
	uint8_t mask = 0;
	for (int i = 0; i < 3; i++)
	{
		int32_t span = d.white[i] - d.black[i];
		if (span < kMinCalibSpan && span > -kMinCalibSpan) mask |= 1u << i;
	}
	return mask;
}

class Calibration
{
public:
	static constexpr int     kCaptureShift   = 14;                 // 16384 samples, ~0.34s
	static constexpr int32_t kCaptureSamples = 1 << kCaptureShift;
	static constexpr int     kDoneTicks      = kCtrlRate;          // 1s "saved" display
	static constexpr int     kFailTicks      = kCtrlRate * 2;      // 2s "failed" display

	void Begin()
	{
		step_ = Step::WaitWhite;
		ticks_ = 0;
	}

	/// Every sample.
	void Sample(const RawRGB &raw)
	{
		if (step_ != Step::CaptureWhite && step_ != Step::CaptureBlack) return;
		sum_[0] += raw.r;
		sum_[1] += raw.g;
		sum_[2] += raw.b;
		if (++count_ < kCaptureSamples) return;

		int32_t *dst = (step_ == Step::CaptureWhite) ? data_.white : data_.black;
		for (int i = 0; i < 3; i++) dst[i] = sum_[i] >> kCaptureShift;

		if (step_ == Step::CaptureWhite)
		{
			step_ = Step::WaitBlack;
		}
		else
		{
			failMask_ = CalibFailMask(data_);
			step_ = failMask_ ? Step::Failed : Step::Done;
			ticks_ = failMask_ ? kFailTicks : kDoneTicks;
		}
	}

	/// Control rate: the debounced Down press.
	void Tap()
	{
		if (step_ == Step::WaitWhite)      StartCapture(Step::CaptureWhite);
		else if (step_ == Step::WaitBlack) StartCapture(Step::CaptureBlack);
	}

	/// Control rate.
	void Tick()
	{
		blink_++;
		if (ticks_ > 0 && --ticks_ == 0)
		{
			if (step_ == Step::Failed) Begin();
			else if (step_ == Step::Done) step_ = Step::Save;
		}
	}

	/// The capture is good and has been shown: save it and reboot.
	bool ReadyToSave() const { return step_ == Step::Save; }
	const CalibData &Result() const { return data_; }

	/// Control rate. Fills level[6], 0..4095. Layout as leds.h: left column
	/// 0/2/4, right column 1/3/5.
	///   waiting for white: LED 0 blinks        right column: raw R/G/B
	///   waiting for black: LEDs 0 and 2 blink  right column: raw R/G/B
	///   measuring: all six fill up
	///   good: all six steady, then save and reboot
	///   failed: the failing channels' right-column LEDs flash fast, then
	///           back to white
	void Leds(const RawRGB &raw, uint16_t level[6]) const
	{
		for (int i = 0; i < 6; i++) level[i] = 0;
		bool slow = (blink_ % kCtrlRate) < kCtrlRate / 2;   // 1Hz
		bool fast = (blink_ % 188) < 94;                    // 8Hz

		switch (step_)
		{
		case Step::WaitWhite:
		case Step::WaitBlack:
			level[0] = slow ? 4095 : 0;
			if (step_ == Step::WaitBlack) level[2] = level[0];
			level[1] = Meter(raw.r);
			level[3] = Meter(raw.g);
			level[5] = Meter(raw.b);
			break;
		case Step::CaptureWhite:
		case Step::CaptureBlack:
		{
			int n = 1 + ((count_ * 5) >> kCaptureShift);
			for (int i = 0; i < n; i++) level[i] = 4095;
			break;
		}
		case Step::Failed:
			for (int ch = 0; ch < 3; ch++)
				if ((failMask_ >> ch) & 1) level[1 + 2 * ch] = fast ? 4095 : 0;
			break;
		case Step::Done:
		case Step::Save:
			for (int i = 0; i < 6; i++) level[i] = 4095;
			break;
		}
	}

private:
	enum class Step : uint8_t { WaitWhite, CaptureWhite, WaitBlack, CaptureBlack, Failed, Done, Save };

	void StartCapture(Step s)
	{
		step_ = s;
		count_ = 0;
		sum_[0] = sum_[1] = sum_[2] = 0;
	}

	static uint16_t Meter(int32_t raw)
	{
		return static_cast<uint16_t>(clamp_i32(raw * 2, 0, 4095));
	}

	CalibData data_ = CalibDefaults();
	Step      step_ = Step::WaitWhite;
	int32_t   count_ = 0;
	int32_t   sum_[3] = { 0, 0, 0 };
	int       ticks_ = 0;
	uint32_t  blink_ = 0;
	uint8_t   failMask_ = 0;
};

} // namespace lp
