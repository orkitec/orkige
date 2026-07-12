/**************************************************************
	created:	2026/07/12 at 16:00
	filename: 	CameraFollow.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __CameraFollow_h__12_7_2026__16_00_00__
#define __CameraFollow_h__12_7_2026__16_00_00__

#include <algorithm>
#include <cmath>

namespace Orkige
{
	//! @brief pure smooth-follow math for a 2D camera: the frame-rate
	//! INDEPENDENT approach factor a camera uses to chase a moving target.
	//! Composes with (is orthogonal to) the CameraFit ortho policy - follow moves
	//! the camera POSITION, fit sizes the PROJECTION - so they never fight.
	//! @remarks Renderer- and math-library-independent (plain floats) so it unit
	//! tests headlessly; CameraComponent applies it per axis.
	namespace CameraFollow
	{
		//! @brief the 0..1 interpolation factor for this frame: how far the camera
		//! moves from its current position toward the target this tick.
		//! @param damping the smoothing time constant in SECONDS - roughly how
		//! long the camera takes to close the gap. 0 (or negative) = instant SNAP
		//! (factor 1); larger = softer/slower. Framerate-independent via an
		//! exponential decay (equal ground covered per real second regardless of
		//! the tick rate), so `new = current + (target - current) * factor`.
		//! @param dt the frame delta in seconds (>= 0)
		//! @return a factor in [0, 1]
		inline float smoothFactor(float damping, float dt)
		{
			if(dt <= 0.0f)
			{
				return 0.0f;	// no time passed: no movement
			}
			if(damping <= 1.0e-5f)
			{
				return 1.0f;	// no smoothing: snap exactly onto the target
			}
			// exponential smoothing: the remaining gap decays by exp(-dt/tau) each
			// second, so the covered fraction is 1 - exp(-dt/tau). Independent of
			// how the frame is sliced, unlike a raw `rate * dt` lerp.
			const float factor = 1.0f - std::exp(-dt / damping);
			return std::clamp(factor, 0.0f, 1.0f);
		}

		//! @brief advance one scalar axis toward its goal by the smoothed factor.
		//! @param current the axis' current value
		//! @param goal the target value (target position + any authored offset)
		//! @param damping @see smoothFactor
		//! @param dt frame delta in seconds
		//! @return the new axis value
		inline float approach(float current, float goal, float damping, float dt)
		{
			return current + (goal - current) * smoothFactor(damping, dt);
		}
	}
}

#endif //__CameraFollow_h__12_7_2026__16_00_00__
