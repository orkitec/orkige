/**************************************************************
	created:	2026/07/22 at 15:00
	filename: 	GradeDesc.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __GradeDesc_h__22_7_2026__15_00_00__
#define __GradeDesc_h__22_7_2026__15_00_00__

namespace Orkige
{
	//! @brief pure, renderer-independent description of the output GRADE - the
	//! scene's authored look (contrast + saturation), what
	//! RenderWorld::setOutputGrade consumes.
	//!
	//! The grade is the ONE output look stage both render flavors run
	//! IDENTICALLY (the shared curve is GradeMath::apply), so whatever look the
	//! content dials stays matched across flavors by construction. It processes
	//! the 3D scene only - the 2D tier (sprites / vector shapes / gui) is
	//! EXCLUDED by contract so UI stays crisp and WYSIWYG (the bloom rule); the
	//! backends sequence the grade pass AFTER the scene (and after bloom when
	//! both are on - grade is the LAST thing before the 2D/UI composition).
	//!
	//! DEFAULT OFF and byte-stable: while @c enabled is false NO grade pass
	//! runs and the frame is byte-identical to a build with no grade code (the
	//! toggle-identity discipline). @c contrast 0 + @c saturation 1 is the
	//! identity even when enabled, but the pass only builds while enabled, so
	//! the neutral state costs nothing.
	struct GradeDesc
	{
		bool	enabled;	//!< content switch: false = no grade pass, byte-identical output
		float	contrast;	//!< S-curve strength around the 0.5 pivot, [0;1] (0 = identity)
		float	saturation;	//!< colour saturation about luma (1 = identity, 0 = greyscale)

		//! neutral defaults: grade OFF, an identity curve. An app opts in by
		//! setting enabled and dialing contrast/saturation.
		GradeDesc()
			: enabled(false)
			, contrast(0.0f)
			, saturation(1.0f)
		{
		}

		//! @brief the sanitised copy the backends apply: contrast clamped to
		//! [0;1] (the monotonic, non-inverting ceiling of the S-curve blend -
		//! @see GradeMath), saturation clamped to [0;4] (non-negative, a
		//! reasonable over-saturation ceiling). Pure so it unit-tests headlessly
		//! and both flavors feed the shaders identical numbers.
		GradeDesc sanitised() const
		{
			GradeDesc out = *this;
			if(out.contrast < 0.0f)			{ out.contrast = 0.0f; }
			else if(out.contrast > 1.0f)	{ out.contrast = 1.0f; }
			if(out.saturation < 0.0f)		{ out.saturation = 0.0f; }
			else if(out.saturation > 4.0f)	{ out.saturation = 4.0f; }
			return out;
		}
	};
}

#endif //__GradeDesc_h__22_7_2026__15_00_00__
