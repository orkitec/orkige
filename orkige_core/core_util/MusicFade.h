/**************************************************************
	created:	2026/07/12 at 16:00
	filename: 	MusicFade.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __MusicFade_h__12_7_2026__16_00_00__
#define __MusicFade_h__12_7_2026__16_00_00__

#include <algorithm>
#include <cmath>

namespace Orkige
{
	//! @brief pure music-crossfade gain math: the outgoing/incoming track own
	//! volumes at a crossfade progress. EQUAL-POWER (constant-energy) so the
	//! perceived loudness stays roughly flat through the swap - a linear
	//! (outGain = 1-t) crossfade dips audibly at the midpoint where both tracks
	//! sit at 0.5. Renderer/audio-backend independent (plain floats) so it unit
	//! tests headlessly; the `music.crossFade` binding drives it with ONE tween.
	namespace MusicFade
	{
		//! @brief the two track gains at crossfade progress `t`.
		//! @param t the crossfade progress in [0, 1] (0 = fully outgoing, 1 =
		//! fully incoming); clamped
		//! @param outGain (out) the OUTGOING track's own volume: 1 at t=0, 0 at t=1
		//! @param inGain  (out) the INCOMING track's own volume: 0 at t=0, 1 at t=1
		//! @remarks equal-power: outGain^2 + inGain^2 == 1 for every t, so the
		//! summed energy is constant across the fade.
		inline void crossfadeGains(float t, float & outGain, float & inGain)
		{
			const float clamped = std::clamp(t, 0.0f, 1.0f);
			// quarter-circle mapping: cos falls 1->0 while sin rises 0->1, and
			// cos^2 + sin^2 == 1 keeps the energy constant
			const float angle = clamped * 1.57079632679489661923f;	// t * pi/2
			// clamp away the tiny float noise at the endpoints (cos(pi/2) lands a
			// hair below 0) so the gains are clean non-negative volumes
			outGain = std::clamp(std::cos(angle), 0.0f, 1.0f);
			inGain = std::clamp(std::sin(angle), 0.0f, 1.0f);
		}
	}
}

#endif //__MusicFade_h__12_7_2026__16_00_00__
