/********************************************************************
	created:	Friday 2026/07/11 at 10:00
	filename: 	TiltCalibration.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __TiltCalibration_h__11_7_2026__10_00_00__
#define __TiltCalibration_h__11_7_2026__10_00_00__

//! @file TiltCalibration.h
//! @brief backend-neutral tilt-calibration math: a tilt-controlled game lets
//! the player hold a comfortable pose and tap "Calibrate" - that pose becomes
//! the neutral (0,-1) gravity direction. The correction is a single rotation
//! about Z (the tilt is planar - z is always 0), stored as one angle in
//! radians. No renderer or platform types: InputManager applies it to its raw
//! gravity direction and a unit test shares the exact same functions.

#include <cmath>

namespace Orkige
{
	namespace TiltCalibration
	{
		//! @brief the calibration angle (radians) that maps the pose (x,y) onto
		//! the upright reference (0,-1): the signed Z rotation from the
		//! reference direction TO the captured pose. Capturing the current pose
		//! and storing this angle makes that pose read as neutral afterwards.
		//! @remarks the reference (0,-1) is the y-down upright convention the
		//! rest of the tilt code uses (InputManager::tiltVectorFromAngle).
		//! A near-zero pose has no direction - returns 0 (identity).
		inline float angleForPose(float x, float y)
		{
			if ((x * x + y * y) < 1.0e-8f)
			{
				return 0.0f;
			}
			// atan2 of the reference (0,-1) is -pi/2; the calibration angle is
			// the pose's angle measured from that reference
			return std::atan2(y, x) + 1.5707963267948966f;
		}
		//! @brief apply a stored calibration to a raw tilt direction (x,y): a Z
		//! rotation by -calibAngle, so the pose the calibration was captured at
		//! rotates back onto the upright reference (0,-1). In place; the vector
		//! magnitude is preserved (callers normalize afterwards as they see fit).
		inline void apply(float & x, float & y, float calibAngle)
		{
			if (calibAngle == 0.0f)
			{
				return;
			}
			const float s = std::sin(-calibAngle);
			const float c = std::cos(-calibAngle);
			const float rotatedX = x * c - y * s;
			const float rotatedY = x * s + y * c;
			x = rotatedX;
			y = rotatedY;
		}
	}
}

#endif //__TiltCalibration_h__11_7_2026__10_00_00__
