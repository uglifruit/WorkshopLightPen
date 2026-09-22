// tape.h — the one audio buffer, shared by Mode 4 (scrub) and Mode 7 (jog).
//
// Recording is a gesture, not a running loop: hold Down, and the take is
// exactly as long as you held it, up to kMaxLen. Both modes record and play
// the same buffer, so a take grabbed in one is playable in the other.
//
// 1.75s at 48kHz is 168KB of the RP2040's 256KB. That is the practical
// ceiling here: with the rest of the card at ~34KB it leaves ~54KB spare.

#pragma once
#include <cstdint>
#include "fastmath.h"

namespace lp {

class Tape
{
public:
	static constexpr uint32_t kMaxLen = 84000;   // 1.75s
	static constexpr uint32_t kMinLen = 2400;    // 50ms: shorter is a slip, not a take

	void StartRecord()
	{
		write_ = 0;
		recording_ = true;
	}

	/// A take shorter than kMinLen is discarded and the previous one kept —
	/// though its first few ms have already been overwritten.
	void StopRecord()
	{
		recording_ = false;
		if (write_ >= kMinLen) len_ = write_;
	}

	bool Recording() const { return recording_; }
	bool HasAudio() const { return len_ >= kMinLen; }
	uint32_t Length() const { return len_; }

	/// Every sample while recording. Runs out at the end of the buffer
	/// rather than wrapping: the take is whatever fitted.
	void Write(int32_t s)
	{
		if (write_ < kMaxLen) buf_[write_++] = static_cast<int16_t>(s);
	}

	/// idx must be < Length(). frac is Q16 within the sample.
	int32_t Read(uint32_t idx, int32_t fracQ16) const
	{
		uint32_t i1 = idx + 1;
		if (i1 >= len_) i1 = 0;
		int32_t s0 = buf_[idx];
		int32_t s1 = buf_[i1];
		return s0 + (((s1 - s0) * fracQ16) >> 16);
	}

	/// Unipolar Q16 -> a position in the take, in Q8 samples. Split in two so
	/// there is no 64-bit multiply in the audio path: len * 65535 overflows
	/// int32 on its own. The caller slews this, then splits it with
	/// idx = q8 >> 8 and fracQ16 = (q8 & 0xFF) << 8.
	int32_t PositionQ8(int32_t u) const
	{
		int32_t span = static_cast<int32_t>(len_) - 2;
		return ((u >> 8) * span) + (((u & 0xFF) * span) >> 8);
	}

private:
	int16_t  buf_[kMaxLen];
	uint32_t len_ = 0;
	uint32_t write_ = 0;
	bool     recording_ = false;
};

extern Tape gTape;

} // namespace lp
