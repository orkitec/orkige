/********************************************************************
	created:	Wednesday 2010/10/27 at 13:08
	filename: 	FastGuiWidget.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
*********************************************************************/

#include "engine_fastgui/FastGuiWidget.h"
#include "engine_render/RenderSystem.h"
#include "engine_fastgui/FastGuiManager.h"
#include "engine_graphic/Engine.h"

#include <OgreString.h>

namespace Orkige
{
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	FastGuiWidget::FastGuiWidget(String const & id, String const & atlas, uint z) : IGuiObject(id), visible(true), layoutEnabled(false), layoutUseSafeArea(false)
	{
		this->view = FastGuiManager::getSingleton().getCreateView(atlas);
		oAssert(view.lock());
		this->layer = view.lock()->getLayer(z);
		oAssert(this->layer);
	}
	//---------------------------------------------------------
	FastGuiWidget::~FastGuiWidget()
	{
	}
	//---------------------------------------------------------
	void FastGuiWidget::centerHorizontal()
	{
		Ogre::Vector2 size = this->getSize();
		Ogre::Vector2 pos = this->getPosition();
		unsigned int screenWidth = 0, screenHeight = 0;
		RenderSystem::get()->getWindowSize(screenWidth, screenHeight);
		// floor to a whole pixel - Caption asserts on subpixel positions
		pos.x = Ogre::Math::Floor((screenWidth/2.f)-(size.x/2.f));
		this->setPosition(pos.x, pos.y);
	}
	//---------------------------------------------------------
	void FastGuiWidget::ensureLayout()
	{
		if(this->layoutEnabled)
		{
			return;
		}
		this->layoutEnabled = true;
		// seed the node from the current on-screen rect (top-left point anchor)
		// so the widget stays exactly where it was authored until anchors or
		// offsets change - opt-in with no visible jump
		applyAnchorPreset(this->layout, LAP_TOPLEFT);
		this->layout.pivot.x = 0.0f;
		this->layout.pivot.y = 0.0f;
		const Ogre::Vector2 pos = this->getPosition();
		const Ogre::Vector2 size = this->getSize();
		this->layout.offsetMin.x = pos.x;
		this->layout.offsetMin.y = pos.y;
		this->layout.offsetMax.x = pos.x + size.x;
		this->layout.offsetMax.y = pos.y + size.y;
		FastGuiManager::getSingleton().markLayoutDirty();
	}
	//---------------------------------------------------------
	void FastGuiWidget::setParent(optr<FastGuiWidget> const & parent)
	{
		this->ensureLayout();
		this->layoutParent = parent;	// woptr: a parent does not own its child
		FastGuiManager::getSingleton().markLayoutDirty();
	}
	//---------------------------------------------------------
	void FastGuiWidget::setAnchors(float minX, float minY, float maxX, float maxY)
	{
		this->ensureLayout();
		this->layout.anchorMin.x = minX;
		this->layout.anchorMin.y = minY;
		this->layout.anchorMax.x = maxX;
		this->layout.anchorMax.y = maxY;
		FastGuiManager::getSingleton().markLayoutDirty();
	}
	//---------------------------------------------------------
	void FastGuiWidget::setAnchorPreset(String const & preset)
	{
		this->ensureLayout();
		String key = preset;
		Ogre::StringUtil::toLowerCase(key);
		LayoutAnchorPreset resolved = LAP_TOPLEFT;
		if(key == "topleft")				resolved = LAP_TOPLEFT;
		else if(key == "top")				resolved = LAP_TOP;
		else if(key == "topright")			resolved = LAP_TOPRIGHT;
		else if(key == "left")				resolved = LAP_LEFT;
		else if(key == "center" || key == "centre")	resolved = LAP_CENTER;
		else if(key == "right")				resolved = LAP_RIGHT;
		else if(key == "bottomleft")		resolved = LAP_BOTTOMLEFT;
		else if(key == "bottom")			resolved = LAP_BOTTOM;
		else if(key == "bottomright")		resolved = LAP_BOTTOMRIGHT;
		else if(key == "stretchtop")		resolved = LAP_STRETCH_TOP;
		else if(key == "stretchmiddle")		resolved = LAP_STRETCH_MIDDLE;
		else if(key == "stretchbottom")		resolved = LAP_STRETCH_BOTTOM;
		else if(key == "stretchleft")		resolved = LAP_STRETCH_LEFT;
		else if(key == "stretchcenter" || key == "stretchcentre")	resolved = LAP_STRETCH_CENTER;
		else if(key == "stretchright")		resolved = LAP_STRETCH_RIGHT;
		else if(key == "stretchall" || key == "stretch")	resolved = LAP_STRETCH_ALL;
		else
		{
			oAssertDesc(!"Unknown anchor preset", "Unknown anchor preset: " << preset);
		}
		applyAnchorPreset(this->layout, resolved);
		FastGuiManager::getSingleton().markLayoutDirty();
	}
	//---------------------------------------------------------
	void FastGuiWidget::setPivot(float x, float y)
	{
		this->ensureLayout();
		this->layout.pivot.x = x;
		this->layout.pivot.y = y;
		FastGuiManager::getSingleton().markLayoutDirty();
	}
	//---------------------------------------------------------
	void FastGuiWidget::setOffsets(float left, float top, float right, float bottom)
	{
		this->ensureLayout();
		this->layout.setOffsets(left, top, right, bottom);
		FastGuiManager::getSingleton().markLayoutDirty();
	}
	//---------------------------------------------------------
	void FastGuiWidget::setAnchoredPosition(float x, float y)
	{
		this->ensureLayout();
		this->layout.setAnchoredPosition(x, y);
		FastGuiManager::getSingleton().markLayoutDirty();
	}
	//---------------------------------------------------------
	void FastGuiWidget::setSizeDelta(float width, float height)
	{
		this->ensureLayout();
		this->layout.setSizeDelta(width, height);
		FastGuiManager::getSingleton().markLayoutDirty();
	}
	//---------------------------------------------------------
	void FastGuiWidget::setUseSafeArea(bool enable)
	{
		this->ensureLayout();
		this->layoutUseSafeArea = enable;
		FastGuiManager::getSingleton().markLayoutDirty();
	}
	//---------------------------------------------------------
	void FastGuiWidget::setLayoutGroup(String const & type)
	{
		this->ensureLayout();
		String key = type;
		Ogre::StringUtil::toLowerCase(key);
		if(key == "horizontal")		this->layoutGroup.type = LGT_Horizontal;
		else if(key == "vertical")	this->layoutGroup.type = LGT_Vertical;
		else if(key == "grid")		this->layoutGroup.type = LGT_Grid;
		else if(key == "none")		this->layoutGroup.type = LGT_None;
		else
		{
			oAssertDesc(!"Unknown layout group", "Unknown layout group: " << type);
		}
		FastGuiManager::getSingleton().markLayoutDirty();
	}
	//---------------------------------------------------------
	void FastGuiWidget::setGroupPadding(float left, float top, float right, float bottom)
	{
		this->ensureLayout();
		this->layoutGroup.padding.left = left;
		this->layoutGroup.padding.top = top;
		this->layoutGroup.padding.right = right;
		this->layoutGroup.padding.bottom = bottom;
		FastGuiManager::getSingleton().markLayoutDirty();
	}
	//---------------------------------------------------------
	void FastGuiWidget::setGroupSpacing(float spacing, float spacingY)
	{
		this->ensureLayout();
		this->layoutGroup.spacing = spacing;
		this->layoutGroup.spacingY = spacingY;
		FastGuiManager::getSingleton().markLayoutDirty();
	}
	//---------------------------------------------------------
	void FastGuiWidget::setChildAlignment(String const & align)
	{
		this->ensureLayout();
		String key = align;
		Ogre::StringUtil::toLowerCase(key);
		if(key == "start")			this->layoutGroup.childAlign = LAL_Start;
		else if(key == "center" || key == "centre")	this->layoutGroup.childAlign = LAL_Center;
		else if(key == "end")		this->layoutGroup.childAlign = LAL_End;
		else
		{
			oAssertDesc(!"Unknown child alignment", "Unknown child alignment: " << align);
		}
		FastGuiManager::getSingleton().markLayoutDirty();
	}
	//---------------------------------------------------------
	void FastGuiWidget::setChildForceExpand(bool enable)
	{
		this->ensureLayout();
		this->layoutGroup.childForceExpand = enable;
		FastGuiManager::getSingleton().markLayoutDirty();
	}
	//---------------------------------------------------------
	void FastGuiWidget::setGridCellSize(float width, float height)
	{
		this->ensureLayout();
		this->layoutGroup.cellSize.x = width;
		this->layoutGroup.cellSize.y = height;
		FastGuiManager::getSingleton().markLayoutDirty();
	}
	//---------------------------------------------------------
	void FastGuiWidget::setGridConstraint(String const & constraint, int count)
	{
		this->ensureLayout();
		String key = constraint;
		Ogre::StringUtil::toLowerCase(key);
		if(key == "flexible")		this->layoutGroup.constraint = LGC_Flexible;
		else if(key == "columns")	this->layoutGroup.constraint = LGC_FixedColumns;
		else if(key == "rows")		this->layoutGroup.constraint = LGC_FixedRows;
		else
		{
			oAssertDesc(!"Unknown grid constraint", "Unknown grid constraint: " << constraint);
		}
		this->layoutGroup.constraintCount = count;
		FastGuiManager::getSingleton().markLayoutDirty();
	}
	//---------------------------------------------------------
	namespace
	{
		LayoutFitMode parseFitMode(String const & mode)
		{
			String key = mode;
			Ogre::StringUtil::toLowerCase(key);
			if(key == "preferred")	return LFM_Preferred;
			// "none"/"unconstrained"/empty all mean "size from the anchors"
			return LFM_Unconstrained;
		}
	}
	//---------------------------------------------------------
	void FastGuiWidget::setContentSizeFit(String const & horizontal, String const & vertical)
	{
		this->ensureLayout();
		this->layoutFit.horizontal = parseFitMode(horizontal);
		this->layoutFit.vertical = parseFitMode(vertical);
		FastGuiManager::getSingleton().markLayoutDirty();
	}
	//---------------------------------------------------------
	Ogre::Vector2 FastGuiWidget::getPreferredSize()
	{
		// the base preferred size is the widget's current pixel size; text
		// widgets override to measure their caption instead
		return this->getSize();
	}
	//---------------------------------------------------------
	void FastGuiWidget::applyResolvedRect(float x, float y, float width, float height)
	{
		// captions assert on subpixel positions; the resolver output is pixels,
		// floor to whole ones (matches centerHorizontal / the caption path)
		this->setPosition(Ogre::Math::Floor(x), Ogre::Math::Floor(y));
		this->setSize(Ogre::Math::Floor(width), Ogre::Math::Floor(height));
	}
	//---------------------------------------------------------
	/*
	void FastGuiWidget::setVisibility(bool enable)
	{
		if (enable && !this->visible)
		{
			//optr<FastGuiWidget> widget = boost::static_pointer_cast<Orkige::FastGuiWidget>(this);
			//FastGuiManager::getSingleton().addWidget(widget);
		}
		else if (!enable && this->visible)
		{
			FastGuiManager::getSingleton().destroyWidget(this->getObjectID());
		}
		this->visible = enable;
	}
	//---------------------------------------------------------
	bool FastGuiWidget::getVisibility()
	{
		oAssertDesc(this->visible == FastGuiManager::getSingleton().widgetExists(this->getObjectID()), "");
		return this->visible;
	}
	*/
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------

	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	OABSTRACT_IMPL(FastGuiWidget)
		OFUNC(setPosition)
		OFUNC(setSize)
		OFUNC(getSize)
		OFUNC(getPosition)
		OFUNC(centerHorizontal)
		// visibility rides on the shared per-z UiLayer (see the jumper
		// HUD): widget:getLayer():hide()/show()/isVisible()
		OFUNC(getLayer)
		// rect-anchor layout (opt-in): parent under another widget and pin/
		// stretch against it or the screen root. A widget that never calls
		// these keeps its absolute authored pixels (no migration).
		OFUNC(setParent)
		OFUNC(setAnchors)
		OFUNC(setAnchorPreset)
		OFUNC(setPivot)
		OFUNC(setOffsets)
		OFUNC(setAnchoredPosition)
		OFUNC(setSizeDelta)
		OFUNC(setUseSafeArea)
		// layout groups + content-size-fit: make a widget arrange its children
		// (h/v stack, grid) and/or size itself to its content
		OFUNC(setLayoutGroup)
		OFUNC(setGroupPadding)
		OFUNC(setGroupSpacing)
		OFUNC(setChildAlignment)
		OFUNC(setChildForceExpand)
		OFUNC(setGridCellSize)
		OFUNC(setGridConstraint)
		OFUNC(setContentSizeFit)
	OOBJECT_END
}