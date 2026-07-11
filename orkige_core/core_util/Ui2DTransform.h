/********************************************************************
	created:	Saturday 2026/07/11 at 20:00
	filename: 	Ui2DTransform.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __Ui2DTransform_h__11_7_2026__20_00_00__
#define __Ui2DTransform_h__11_7_2026__20_00_00__

//! @file Ui2DTransform.h
//! @brief a pure 2D scale + rotation about a pivot point, applied to the gui
//! renderer's emitted vertex positions (@see UiRect/UiCaption/UiMarkupText). The
//! pivot is the SHARED origin of layout and animation (the rect-transform model):
//! a widget scales/spins about its own pivot so the visual grows/turns in place
//! while its layout rect stays where the resolver put it. Plain floats and
//! window-pixel coordinates, no renderer types, so the maths resolves identically
//! on every backend and a unit test pins it headlessly. Identity is the common
//! case (no animation) and short-circuits to a copy, so a static UI pays nothing.

#include <cmath>

namespace Orkige
{
	//! @brief scale + rotation about a fixed pivot (window pixels, radians).
	//! Default-constructed it is the identity (scale 1, no rotation).
	struct Ui2DTransform
	{
		float	pivotX = 0.0f;		//!< pivot point x (window pixels)
		float	pivotY = 0.0f;		//!< pivot point y (window pixels)
		float	scaleX = 1.0f;		//!< scale about the pivot on x
		float	scaleY = 1.0f;		//!< scale about the pivot on y
		float	rotation = 0.0f;	//!< rotation about the pivot (radians, CW in
									//!< the top-left-origin pixel space)

		//! @brief is this the identity transform (nothing to apply)? The emitters
		//! skip the per-vertex maths entirely when true.
		inline bool isIdentity() const
		{
			return this->scaleX == 1.0f && this->scaleY == 1.0f &&
				this->rotation == 0.0f;
		}

		//! @brief map a point through the transform: translate to the pivot, scale,
		//! rotate, translate back. @param x/y the input point (window pixels);
		//! @param outX/outY receive the transformed point.
		inline void apply(float x, float y, float & outX, float & outY) const
		{
			// scale about the pivot
			float dx = (x - this->pivotX) * this->scaleX;
			float dy = (y - this->pivotY) * this->scaleY;
			if(this->rotation != 0.0f)
			{
				const float c = std::cos(this->rotation);
				const float s = std::sin(this->rotation);
				const float rx = dx * c - dy * s;
				const float ry = dx * s + dy * c;
				dx = rx;
				dy = ry;
			}
			outX = this->pivotX + dx;
			outY = this->pivotY + dy;
		}
	};
}

#endif //__Ui2DTransform_h__11_7_2026__20_00_00__
