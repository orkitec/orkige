/********************************************************************
	created:	Saturday 2026/07/11 at 22:20
	filename: 	SoundVariation.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __SoundVariation_h__11_7_2026__22_20_00__
#define __SoundVariation_h__11_7_2026__22_20_00__

//! @file SoundVariation.h
//! @brief the pure pitch/volume randomization math a SoundSource applies on each
//! play() so a repeated effect (a footstep, a coin, a hit) never sounds
//! mechanically identical. No OpenAL, no source - a unit test feeds fixed
//! [0,1) samples and asserts the endpoints/midpoint, and the source draws the
//! sample from its own RNG at play time. Both channels take a symmetric range:
//! a range of 0 leaves the value untouched (the honest off switch).

#include <algorithm>

namespace Orkige
{
	//! @brief a pitch multiplier varied symmetrically about 1.0 by @p range.
	//! @param range the +/- fraction (0 = no variation, 0.1 = +/-10%); a
	//! negative range is treated as its magnitude. @param unit01 a random sample
	//! in [0,1) (0 -> the low edge, 0.5 -> 1.0, 1 -> the high edge). The result
	//! is clamped above 0 so AL_PITCH stays valid at large ranges.
	inline float variedPitch(float range, float unit01)
	{
		const float r = range < 0.0f ? -range : range;
		const float pitch = 1.0f + r * (2.0f * unit01 - 1.0f);
		return pitch < 0.01f ? 0.01f : pitch;
	}

	//! @brief a gain varied symmetrically about @p baseGain by @p range, then
	//! clamped to the 0..1 mixer window. @param baseGain the source's effective
	//! gain before variation; @param range the +/- fraction; @param unit01 a
	//! random sample in [0,1).
	inline float variedGain(float baseGain, float range, float unit01)
	{
		const float r = range < 0.0f ? -range : range;
		const float gain = baseGain * (1.0f + r * (2.0f * unit01 - 1.0f));
		return std::clamp(gain, 0.0f, 1.0f);
	}
}

#endif //__SoundVariation_h__11_7_2026__22_20_00__
