/********************************************************************
	created:	Wednesday 2010/10/27 at 13:08
	filename: 	GuiWidget.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

#include "engine_gui/GuiWidget.h"
#include "engine_render/RenderSystem.h"
#include "engine_gui/GuiManager.h"
#include "engine_gui/GuiLayerHandle.h"
#include "engine_graphic/Engine.h"

#include <OgreString.h>
#include <OgreMath.h>

namespace Orkige
{
	//---------------------------------------------------------
	// build a weak layer handle from a live widget: the widget's view is the
	// liveness key (the layer dies with its screen), the widget's UiLayer the
	// guarded target. @see engine_gui/GuiLayerHandle.h
	MetaLuaDetail::GuiLayerHandle makeLayerHandle(GuiWidget & widget)
	{
		return MetaLuaDetail::GuiLayerHandle{ widget.getView(), widget.getLayer() };
	}
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	const float GuiWidget::DISABLED_ALPHA = 0.5f;
	const float GuiWidget::ALPHA_INPUT_THRESHOLD = 0.05f;
	//---------------------------------------------------------
	GuiWidget::GuiWidget(String const & id, String const & atlas, uint z) : IGuiObject(id), visible(true), enabled(true), layoutEnabled(false), layoutUseSafeArea(false), renderScaleX(1.0f), renderScaleY(1.0f), renderRotationDeg(0.0f), groupAlpha(1.0f), alphaBlocksInput(true)
	{
		this->view = GuiManager::getSingleton().getCreateView(atlas);
		oAssert(view.lock());
		this->layer = view.lock()->getLayer(z);
		oAssert(this->layer);
	}
	//---------------------------------------------------------
	GuiWidget::~GuiWidget()
	{
	}
	//---------------------------------------------------------
	void GuiWidget::centerHorizontal()
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
	void GuiWidget::setEnabled(bool enable)
	{
		if(this->enabled == enable)
		{
			return;
		}
		this->enabled = enable;
		this->onEnabledChanged(enable);
	}
	//---------------------------------------------------------
	bool GuiWidget::isEffectivelyEnabled() const
	{
		// disabled if this widget OR any layout ancestor is disabled (a disabled
		// group/panel disables its subtree). Bounded by the layout depth.
		if(!this->enabled)
		{
			return false;
		}
		optr<GuiWidget> ancestor = this->layoutParent.lock();
		while(ancestor)
		{
			if(!ancestor->enabled)
			{
				return false;
			}
			ancestor = ancestor->layoutParent.lock();
		}
		return true;
	}
	//---------------------------------------------------------
	void GuiWidget::setRenderScale(float scaleX, float scaleY)
	{
		this->renderScaleX = scaleX;
		this->renderScaleY = scaleY;
		this->refreshRenderTransform();
	}
	//---------------------------------------------------------
	void GuiWidget::setRenderRotation(float degrees)
	{
		this->renderRotationDeg = degrees;
		this->refreshRenderTransform();
	}
	//---------------------------------------------------------
	void GuiWidget::refreshRenderTransform()
	{
		// scale + rotation about the widget CENTRE (window pixels): the visual
		// grows/turns in place while the layout rect stays put. Recomputed from
		// the live rect each call, so a widget that moves AND scales keeps its
		// pivot centred.
		Ui2DTransform transform;
		const Ogre::Vector2 pos = this->getPosition();
		const Ogre::Vector2 size = this->getSize();
		transform.pivotX = pos.x + size.x * 0.5f;
		transform.pivotY = pos.y + size.y * 0.5f;
		transform.scaleX = this->renderScaleX;
		transform.scaleY = this->renderScaleY;
		transform.rotation = Ogre::Math::DegreesToRadians(this->renderRotationDeg);
		this->applyRenderTransform(transform);
	}
	//---------------------------------------------------------
	void GuiWidget::setGroupAlpha(float alpha)
	{
		this->groupAlpha = alpha;
		// this widget updates immediately; the manager re-runs the cascade over
		// the subtree at the next frame (a parent fade dims its children)
		this->applyRenderAlpha(this->getEffectiveAlpha());
		GuiManager::getSingleton().markGroupAlphaDirty();
	}
	//---------------------------------------------------------
	float GuiWidget::getEffectiveAlpha() const
	{
		float alpha = this->groupAlpha;
		optr<GuiWidget> ancestor = this->layoutParent.lock();
		while(ancestor)
		{
			alpha *= ancestor->groupAlpha;
			ancestor = ancestor->layoutParent.lock();
		}
		return alpha;
	}
	//---------------------------------------------------------
	void GuiWidget::setAlphaBlocksInput(bool enable)
	{
		this->alphaBlocksInput = enable;
	}
	//---------------------------------------------------------
	void GuiWidget::setTransition(String const & spec)
	{
		this->transition = parseTransition(spec);
	}
	//---------------------------------------------------------
	bool GuiWidget::acceptsInput() const
	{
		if(!this->isEffectivelyEnabled())
		{
			return false;
		}
		// a faded-out subtree stops hit-testing (unless it opted out): input falls
		// through to whatever is behind it
		if(this->alphaBlocksInput &&
			this->getEffectiveAlpha() < ALPHA_INPUT_THRESHOLD)
		{
			return false;
		}
		return true;
	}
	//---------------------------------------------------------
	void GuiWidget::ensureLayout()
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
		GuiManager::getSingleton().markLayoutDirty();
	}
	//---------------------------------------------------------
	void GuiWidget::setParent(optr<GuiWidget> const & parent)
	{
		this->ensureLayout();
		this->layoutParent = parent;	// woptr: a parent does not own its child
		GuiManager::getSingleton().markLayoutDirty();
	}
	//---------------------------------------------------------
	void GuiWidget::setAnchors(float minX, float minY, float maxX, float maxY)
	{
		this->ensureLayout();
		this->layout.anchorMin.x = minX;
		this->layout.anchorMin.y = minY;
		this->layout.anchorMax.x = maxX;
		this->layout.anchorMax.y = maxY;
		GuiManager::getSingleton().markLayoutDirty();
	}
	//---------------------------------------------------------
	void GuiWidget::setAnchorPreset(String const & preset)
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
		GuiManager::getSingleton().markLayoutDirty();
	}
	//---------------------------------------------------------
	void GuiWidget::setPivot(float x, float y)
	{
		this->ensureLayout();
		this->layout.pivot.x = x;
		this->layout.pivot.y = y;
		GuiManager::getSingleton().markLayoutDirty();
	}
	//---------------------------------------------------------
	void GuiWidget::setOffsets(float left, float top, float right, float bottom)
	{
		this->ensureLayout();
		this->layout.setOffsets(left, top, right, bottom);
		GuiManager::getSingleton().markLayoutDirty();
	}
	//---------------------------------------------------------
	void GuiWidget::setAnchoredPosition(float x, float y)
	{
		this->ensureLayout();
		this->layout.setAnchoredPosition(x, y);
		GuiManager::getSingleton().markLayoutDirty();
	}
	//---------------------------------------------------------
	void GuiWidget::setSizeDelta(float width, float height)
	{
		this->ensureLayout();
		this->layout.setSizeDelta(width, height);
		GuiManager::getSingleton().markLayoutDirty();
	}
	//---------------------------------------------------------
	void GuiWidget::setUseSafeArea(bool enable)
	{
		this->ensureLayout();
		this->layoutUseSafeArea = enable;
		GuiManager::getSingleton().markLayoutDirty();
	}
	//---------------------------------------------------------
	void GuiWidget::setLayoutGroup(String const & type)
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
		GuiManager::getSingleton().markLayoutDirty();
	}
	//---------------------------------------------------------
	void GuiWidget::setGroupPadding(float left, float top, float right, float bottom)
	{
		this->ensureLayout();
		this->layoutGroup.padding.left = left;
		this->layoutGroup.padding.top = top;
		this->layoutGroup.padding.right = right;
		this->layoutGroup.padding.bottom = bottom;
		GuiManager::getSingleton().markLayoutDirty();
	}
	//---------------------------------------------------------
	void GuiWidget::setGroupSpacing(float spacing, float spacingY)
	{
		this->ensureLayout();
		this->layoutGroup.spacing = spacing;
		this->layoutGroup.spacingY = spacingY;
		GuiManager::getSingleton().markLayoutDirty();
	}
	//---------------------------------------------------------
	void GuiWidget::setChildAlignment(String const & align)
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
		GuiManager::getSingleton().markLayoutDirty();
	}
	//---------------------------------------------------------
	void GuiWidget::setChildForceExpand(bool enable)
	{
		this->ensureLayout();
		this->layoutGroup.childForceExpand = enable;
		GuiManager::getSingleton().markLayoutDirty();
	}
	//---------------------------------------------------------
	void GuiWidget::setGridCellSize(float width, float height)
	{
		this->ensureLayout();
		this->layoutGroup.cellSize.x = width;
		this->layoutGroup.cellSize.y = height;
		GuiManager::getSingleton().markLayoutDirty();
	}
	//---------------------------------------------------------
	void GuiWidget::setGridConstraint(String const & constraint, int count)
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
		GuiManager::getSingleton().markLayoutDirty();
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
	void GuiWidget::setContentSizeFit(String const & horizontal, String const & vertical)
	{
		this->ensureLayout();
		this->layoutFit.horizontal = parseFitMode(horizontal);
		this->layoutFit.vertical = parseFitMode(vertical);
		GuiManager::getSingleton().markLayoutDirty();
	}
	//---------------------------------------------------------
	Ogre::Vector2 GuiWidget::getPreferredSize()
	{
		// the base preferred size is the widget's current pixel size; text
		// widgets override to measure their caption instead
		return this->getSize();
	}
	//---------------------------------------------------------
	void GuiWidget::applyResolvedRect(float x, float y, float width, float height)
	{
		// captions assert on subpixel positions; the resolver output is pixels,
		// floor to whole ones (matches centerHorizontal / the caption path)
		this->setPosition(Ogre::Math::Floor(x), Ogre::Math::Floor(y));
		this->setSize(Ogre::Math::Floor(width), Ogre::Math::Floor(height));
	}
	//---------------------------------------------------------
	/*
	void GuiWidget::setVisibility(bool enable)
	{
		if (enable && !this->visible)
		{
			//optr<GuiWidget> widget = boost::static_pointer_cast<Orkige::GuiWidget>(this);
			//GuiManager::getSingleton().addWidget(widget);
		}
		else if (!enable && this->visible)
		{
			GuiManager::getSingleton().destroyWidget(this->getObjectID());
		}
		this->visible = enable;
	}
	//---------------------------------------------------------
	bool GuiWidget::getVisibility()
	{
		oAssertDesc(this->visible == GuiManager::getSingleton().widgetExists(this->getObjectID()), "");
		return this->visible;
	}
	*/
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------

	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	OABSTRACT_IMPL(GuiWidget)
		OFUNC(setPosition)
		OFUNC(setSize)
		OFUNC(getSize)
		OFUNC(getPosition)
		OFUNC(centerHorizontal)
		// uniform interactive state: a disabled widget is input-inert (skipped
		// in the manager dispatch) and dims its visuals
		OFUNC(setEnabled)
		OFUNC(isEnabled)
		// visibility rides on the shared per-z UiLayer (see the jumper
		// HUD): widget:getLayer():hide()/show()/isVisible(). getLayer hands Lua a
		// WEAK GuiLayerHandle, not the raw UiLayer: a layer is SCREEN-scoped and
		// dies with its view (an .oui hot-reload or a preview teardown destroys
		// the screen mid-session), so a cached layer handle locks the view per
		// call and raises "layer handle is dead" once the screen is gone rather
		// than dangling. @see engine_gui/GuiLayerHandle.h
		OFUNC_CUSTOM(getLayer, [](Orkige::GuiWidget & widget)
			{ return Orkige::makeLayerHandle(widget); })
		// rect-anchor layout (opt-in): parent under another widget and pin/
		// stretch against it or the screen root. A widget that never calls
		// these keeps its absolute authored pixels (no migration). setParent
		// is bound on the WidgetHandle (a widget-valued PARAMETER locked inside
		// the wrapper - engine_module/module.cpp), which retired the interim
		// by-reference OFUNC_CUSTOM adapter this site used to carry.
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
		// widget animation: scale/rotation about the centre + cascading group
		// alpha. Driven declaratively by widget:tween(...) but exposed raw too.
		OFUNC(setRenderScale)
		OFUNC(getRenderScaleX)
		OFUNC(getRenderScaleY)
		OFUNC(setRenderRotation)
		OFUNC(getRenderRotation)
		OFUNC(setGroupAlpha)
		OFUNC(getGroupAlpha)
		OFUNC(getEffectiveAlpha)
		OFUNC(setAlphaBlocksInput)
		OFUNC(setTransition)
	OOBJECT_END
}