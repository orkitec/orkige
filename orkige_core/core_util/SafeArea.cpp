/********************************************************************
	created:	Friday 2026/07/10 at 12:00
	filename: 	SafeArea.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

#include "core_util/SafeArea.h"

namespace Orkige
{
	//---------------------------------------------------------
	SafeAreaInsets SafeAreaInsets::fromSafeRect(unsigned int surfaceWidth,
		unsigned int surfaceHeight, int safeX, int safeY,
		int safeWidth, int safeHeight)
	{
		SafeAreaInsets insets;
		// the safe rect sits inside the surface: each inset is the gap between
		// the surface edge and the corresponding safe edge (never negative)
		const int left		= safeX;
		const int top		= safeY;
		const int right		= static_cast<int>(surfaceWidth) - (safeX + safeWidth);
		const int bottom	= static_cast<int>(surfaceHeight) - (safeY + safeHeight);
		insets.mLeft	= left		> 0 ? static_cast<unsigned int>(left)	: 0u;
		insets.mTop		= top		> 0 ? static_cast<unsigned int>(top)	: 0u;
		insets.mRight	= right		> 0 ? static_cast<unsigned int>(right)	: 0u;
		insets.mBottom	= bottom	> 0 ? static_cast<unsigned int>(bottom)	: 0u;
		return insets;
	}
	//---------------------------------------------------------
	void UiAnchor::place(float sizeW, float sizeH, float marginX, float marginY,
		unsigned int windowW, unsigned int windowH,
		SafeAreaInsets const & insets, bool anchorRight, bool anchorBottom,
		float & outX, float & outY)
	{
		// the safe box in surface pixels
		const float safeLeft	= static_cast<float>(insets.mLeft);
		const float safeTop		= static_cast<float>(insets.mTop);
		const float safeRight	= static_cast<float>(windowW) -
			static_cast<float>(insets.mRight);
		const float safeBottom	= static_cast<float>(windowH) -
			static_cast<float>(insets.mBottom);

		outX = anchorRight
			? safeRight - sizeW - marginX
			: safeLeft + marginX;
		outY = anchorBottom
			? safeBottom - sizeH - marginY
			: safeTop + marginY;

		// clamp so the rect never crosses an inset it hugs; a rect larger than
		// the safe box pins to the left/top safe edge
		const float maxX = safeRight - sizeW;
		const float maxY = safeBottom - sizeH;
		if(outX > maxX) { outX = maxX; }
		if(outX < safeLeft) { outX = safeLeft; }
		if(outY > maxY) { outY = maxY; }
		if(outY < safeTop) { outY = safeTop; }
	}
}
