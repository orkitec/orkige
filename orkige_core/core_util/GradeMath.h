/**************************************************************
	created:	2026/07/22 at 15:00
	filename: 	GradeMath.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __GradeMath_h__22_7_2026__15_00_00__
#define __GradeMath_h__22_7_2026__15_00_00__

namespace Orkige
{
	//! @brief the pure output-grade curve: the EXACT contrast + saturation
	//! transform both render flavors implement in their grade shaders. Kept
	//! renderer-independent here so it unit-tests headlessly AND so the shaders
	//! (classic viewport compositor + next CompositorManager2 quad) can be
	//! read against ONE reference - the shared-curve guarantee that makes the
	//! authored look identical on both flavors by construction (@see GradeDesc,
	//! RenderWorld::setOutputGrade).
	//!
	//! COLOUR SPACE: the transform operates on DISPLAY-space (gamma-encoded)
	//! colour in [0;1] - where contrast/saturation are conventionally authored,
	//! and the space the classic scene texture already stores. The next shader
	//! wraps this with a linear<->display adapter (its scene texture samples
	//! LINEAR), so both apply the identical curve to comparable display values;
	//! that adapter is the only per-flavor difference (the hemisphere-ambient
	//! "recover to display space to match" precedent). The delta a grade
	//! induces therefore matches across flavors within tolerance
	//! (RenderWorld::setOutputGrade's cross-flavor look-parity guarantee).
	//!
	//! CURVE CHOICE:
	//!  - contrast: a smoothstep S-curve around a 0.5 pivot, blended in by
	//!    @p contrast strength: mix(x, smoothstep(x), contrast). smoothstep
	//!    (3x^2-2x^3) is monotonic and clamp-safe on [0;1] with an exact 0.5
	//!    fixed point, so 0.5 stays 0.5 while darks darken and brights brighten.
	//!    For @p contrast in [0;1] the blend stays MONOTONIC (its derivative is
	//!    (1-contrast) + 6*contrast*x*(1-x) >= 0), which is why GradeDesc clamps
	//!    contrast to [0;1] - the honest ceiling of a non-inverting curve on the
	//!    un-tonemapped clip.
	//!  - saturation: mix(luma, colour, saturation) about the Rec.601 luma
	//!    (1 = identity, 0 = greyscale, >1 = more saturated).
	namespace GradeMath
	{
		//! Rec.601 luma of a display-space colour (matches the bloom bright-pass)
		inline float luma(float r, float g, float b)
		{
			return 0.299f * r + 0.587f * g + 0.114f * b;
		}

		//! smoothstep(0,1,x) = the S-curve building block (assumes x in [0;1])
		inline float sCurve(float x)
		{
			return x * x * (3.0f - 2.0f * x);
		}

		inline float clamp01(float x)
		{
			return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
		}

		inline float mix(float a, float b, float t)
		{
			return a + (b - a) * t;
		}

		//! @brief apply the grade curve to a display-space colour IN PLACE.
		//! Inputs are clamped to [0;1] first (the un-tonemapped clip), the
		//! S-curve contrast is applied per channel, then saturation is applied
		//! about the luma; the result is clamped to [0;1]. With @p contrast 0
		//! and @p saturation 1 this is the identity (byte-stable neutral).
		inline void apply(float & r, float & g, float & b,
			float contrast, float saturation)
		{
			r = clamp01(r); g = clamp01(g); b = clamp01(b);
			r = mix(r, sCurve(r), contrast);
			g = mix(g, sCurve(g), contrast);
			b = mix(b, sCurve(b), contrast);
			const float l = luma(r, g, b);
			r = mix(l, r, saturation);
			g = mix(l, g, saturation);
			b = mix(l, b, saturation);
			r = clamp01(r); g = clamp01(g); b = clamp01(b);
		}
	}
}

#endif //__GradeMath_h__22_7_2026__15_00_00__
