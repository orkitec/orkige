/********************************************************************
	created:	Saturday 2026/07/11 at 15:00
	filename: 	FastGuiScrollView.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

#include "engine_fastgui/FastGuiScrollView.h"
#include "engine_fastgui/FastGuiManager.h"
#include <core_util/UiLayout.h>

#include <algorithm>
#include <cmath>

namespace Orkige
{
	namespace
	{
		//! pixels scrolled per wheel notch (120 raw units per notch)
		const float SCROLL_WHEEL_STEP = 48.0f;
	}
	//---------------------------------------------------------
	FastGuiScrollView::FastGuiScrollView(String const & id,
		Ogre::Vector2 const & position, Ogre::Vector2 const & size,
		String const & atlas, uint z)
		: FastGuiWidget(id, atlas, z), left(position.x), top(position.y),
		width(size.x), height(size.y), scrollY(0.0f), viewportExtent(size.y),
		contentExtent(size.y), dragging(false), dragLastY(0.0f)
	{
	}
	//---------------------------------------------------------
	FastGuiScrollView::~FastGuiScrollView()
	{
		// the clip layer is shared with the content widgets (owned by the view);
		// drop our clip so a later widget on the same z is not trimmed
		if(this->layer != NULL)
		{
			this->layer->clearScissor();
		}
	}
	//---------------------------------------------------------
	void FastGuiScrollView::setPosition(Ogre::Real l, Ogre::Real t)
	{
		this->left = l;
		this->top = t;
	}
	//---------------------------------------------------------
	void FastGuiScrollView::setSize(Ogre::Real w, Ogre::Real h)
	{
		this->width = w;
		this->height = h;
	}
	//---------------------------------------------------------
	Ogre::Vector2 FastGuiScrollView::getSize()
	{
		return Ogre::Vector2(this->width, this->height);
	}
	//---------------------------------------------------------
	Ogre::Vector2 FastGuiScrollView::getPosition()
	{
		return Ogre::Vector2(this->left, this->top);
	}
	//---------------------------------------------------------
	LayoutVec2 FastGuiScrollView::getScrollOffset() const
	{
		LayoutVec2 offset;
		offset.x = 0.0f;
		offset.y = this->scrollY;
		return offset;
	}
	//---------------------------------------------------------
	float FastGuiScrollView::getMaxScroll() const
	{
		return std::max(0.0f,
			float(this->contentExtent) - float(this->viewportExtent));
	}
	//---------------------------------------------------------
	void FastGuiScrollView::onLayoutResolved(Ogre::Vector2 const & viewportSize,
		Ogre::Vector2 const & content)
	{
		this->viewportExtent = viewportSize.y;
		this->contentExtent = content.y;
		// re-clamp against the fresh extents (a resize may shrink the range)
		const float clamped = clampScroll(this->scrollY,
			float(this->contentExtent), float(this->viewportExtent));
		this->scrollY = clamped;
		// clip the content layer to the viewport rect (pixels). The scroll view
		// draws nothing itself; its content widgets sit on this same z layer.
		if(this->layer != NULL)
		{
			DrawLayer2D::ScissorRect scissor;
			scissor.left = int(std::floor(this->left));
			scissor.top = int(std::floor(this->top));
			scissor.width = int(std::ceil(this->width));
			scissor.height = int(std::ceil(this->height));
			this->layer->setScissor(scissor);
		}
	}
	//---------------------------------------------------------
	void FastGuiScrollView::clampAndApply()
	{
		this->scrollY = clampScroll(this->scrollY, float(this->contentExtent),
			float(this->viewportExtent));
		// a changed offset re-runs the resolve so the content rects shift
		FastGuiManager::getSingleton().markLayoutDirty();
	}
	//---------------------------------------------------------
	void FastGuiScrollView::setScroll(float offsetY)
	{
		this->scrollY = offsetY;
		this->clampAndApply();
	}
	//---------------------------------------------------------
	bool FastGuiScrollView::containsPoint(Ogre::Vector2 const & point) const
	{
		return point.x >= this->left && point.x <= this->left + this->width &&
			point.y >= this->top && point.y <= this->top + this->height;
	}
	//---------------------------------------------------------
	void FastGuiScrollView::onMouseWheel(Ogre::Vector2 const & cursorPos,
		int wheelDelta)
	{
		if(!this->containsPoint(cursorPos) || this->getMaxScroll() <= 0.0f)
		{
			return;
		}
		// a notch up (positive) reveals content above -> offset toward 0
		this->scrollY += (float(wheelDelta) / 120.0f) * SCROLL_WHEEL_STEP;
		this->clampAndApply();
	}
	//---------------------------------------------------------
	void FastGuiScrollView::onCursorPressed(Ogre::Vector2 const & cursorPos)
	{
		if(this->containsPoint(cursorPos) && this->getMaxScroll() > 0.0f)
		{
			this->dragging = true;
			this->dragLastY = cursorPos.y;
		}
	}
	//---------------------------------------------------------
	void FastGuiScrollView::onCursorReleased(Ogre::Vector2 const & cursorPos)
	{
		(void)cursorPos;
		this->dragging = false;
	}
	//---------------------------------------------------------
	void FastGuiScrollView::onCursorMoved(Ogre::Vector2 const & cursorPos)
	{
		if(!this->dragging)
		{
			return;
		}
		// dragging down (positive dy) pulls the content down (reveals its top)
		const float delta = cursorPos.y - float(this->dragLastY);
		this->dragLastY = cursorPos.y;
		if(delta != 0.0f)
		{
			this->scrollY += delta;
			this->clampAndApply();
		}
	}
	//---------------------------------------------------------
	OABSTRACT_IMPL(FastGuiScrollView)
		OFUNC(setScroll)
		OFUNC(getScroll)
		OFUNC(getMaxScroll)
	OOBJECT_END
}
