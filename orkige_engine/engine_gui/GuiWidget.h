/********************************************************************
	created:	Wednesday 2010/10/27 at 13:08
	filename: 	GuiWidget.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec	
*********************************************************************/
#ifndef __GuiWidget_h__27_10_2010__13_08_39__
#define __GuiWidget_h__27_10_2010__13_08_39__

#include "engine_gui/IGuiObject.h"
#include "engine_gui/GuiView.h"
#include "engine_gui/UiRenderer.h"
#include <core_util/UiLayout.h>
#include <core_util/Ui2DTransform.h>
#include <core_util/UiTransition.h>

namespace Orkige
{
	class ORKIGE_ENGINE_DLL GuiWidget : public IGuiObject
	{
		OOBJECT(GuiWidget, IGuiObject);
		//--- Types -------------------------------------------------
	public:
		//! @brief the opacity a disabled widget dims its sprites/text to (the
		//! widgets that lack a dedicated `_disabled` sprite fade instead)
		static const float DISABLED_ALPHA;
		//! @brief below this effective (cascaded) alpha a widget stops hit-testing
		//! - a faded-out subtree is passed through by input (the convention), unless
		//! a widget opts out with setAlphaBlocksInput(false)
		static const float ALPHA_INPUT_THRESHOLD;
	protected:
	private:
		//--- Variables ---------------------------------------------
	public:
	protected:
		UiLayer* layer;
		woptr<GuiView> view;
	private:
		bool visible;
		//! @brief input-inert when false: the manager's dispatch loop skips a
		//! disabled widget (@see GuiManager) so it neither reacts nor consumes,
		//! and the widget dims its visuals via onEnabledChanged. Default true.
		bool enabled;
		//! @brief rect-anchor layout (opt-in). A widget with layoutEnabled is
		//! placed by GuiManager's per-frame resolve pass against its parent
		//! (or the screen root) instead of the absolute pixels it was authored
		//! with; a widget that never touches a layout setter keeps behaving
		//! exactly as before (absolute pixels) - zero migration.
		bool layoutEnabled;
		LayoutNode layout;
		woptr<GuiWidget> layoutParent;
		//! resolve against the safe-area root rather than the full window (only
		//! consulted for a parentless widget)
		bool layoutUseSafeArea;
		//! group arrangement (LGT_None = children keep their own anchors) and
		//! content-size-fit; both feed the manager's two-pass resolve. Opt-in
		//! like the anchor node - untouched leaves a plain absolute widget.
		LayoutGroup layoutGroup;
		LayoutContentFit layoutFit;
		//! @brief the widget-animation render transform (scale + Z rotation about
		//! the widget centre) applied to the emitted vertices. Identity = no
		//! transform. Driven by widget:tween(scale/rotation) and press feedback;
		//! composes ON TOP of the layout rect so animation and layout coexist.
		float renderScaleX;
		float renderScaleY;
		float renderRotationDeg;		//!< Z rotation in degrees (2D convention)
		//! @brief this widget's own group-alpha 0..1 (setGroupAlpha). The EFFECTIVE
		//! alpha multiplies this down the layout-parent chain (cascading), so a
		//! parent fade dims its whole subtree. @see getEffectiveAlpha
		float groupAlpha;
		//! @brief does a faded-out (effective alpha < threshold) widget block input?
		//! Default true (a hidden subtree is inert); false lets input pass through.
		bool alphaBlocksInput;
		//! @brief the show/hide transition this widget plays (from the .oui
		//! `transition` key or setTransition). UTT_None = snap. @see GuiManager
		//! playWidgetTransition
		UiTransitionSpec transition;
		//--- Methods -----------------------------------------------
	public:
		GuiWidget(String const & id, String const & atlas, uint z);
		virtual ~GuiWidget();

		//! set position of this widget
		virtual void setPosition(Ogre::Real left, Ogre::Real top) = 0;
		//! set size of this widget
		virtual void setSize(Ogre::Real width, Ogre::Real height) = 0;
		//! get size of this widget
		virtual Ogre::Vector2 getSize() = 0;
		//! get position of this widget
		virtual Ogre::Vector2 getPosition() = 0;
		//! get layer this widget is in
		inline UiLayer* getLayer() const;
		//! get the view of this layer
		inline woptr<GuiView> getView();
		//! center widget horizontally on the screen
		void centerHorizontal();

		//! @brief enable/disable the widget. A disabled widget is input-inert
		//! (the manager skips it) and dims its visuals. Gated uniformly for
		//! every interactive widget in ONE place - the dispatch loop.
		void setEnabled(bool enable);
		//! @brief is the widget interactive (default true)?
		inline bool isEnabled() const { return this->enabled; }
		//! @brief is the widget interactive AND every layout ancestor enabled?
		//! A disabled group/panel disables its whole subtree - the manager gates
		//! input on this, not the local flag. Walks the layout-parent chain.
		bool isEffectivelyEnabled() const;

		//--- widget animation: render transform + cascading alpha (@see the tween
		//--- surface widget:tween(...) and the manager's per-frame alpha pass) ---
		//! @brief scale the widget about its centre (1 = natural size). The visual
		//! grows/shrinks in place; the layout rect is untouched, so this composes
		//! with the layout instead of fighting it. Applied to every visual element.
		void setRenderScale(float scaleX, float scaleY);
		inline float getRenderScaleX() const { return this->renderScaleX; }
		inline float getRenderScaleY() const { return this->renderScaleY; }
		//! @brief rotate the widget about its centre (degrees, Z axis - the 2D
		//! convention). 0 = upright.
		void setRenderRotation(float degrees);
		inline float getRenderRotation() const { return this->renderRotationDeg; }
		//! @brief this widget's own group alpha 0..1. The effective alpha cascades:
		//! it multiplies through the layout-parent chain, so fading a panel fades
		//! its children too. Applied through the manager's per-frame alpha pass.
		void setGroupAlpha(float alpha);
		inline float getGroupAlpha() const { return this->groupAlpha; }
		//! @brief the effective (cascaded) alpha: this widget's group alpha times
		//! every layout ancestor's. Drives the emitted vertex alpha and hit-testing.
		float getEffectiveAlpha() const;
		//! @brief let input pass through this widget once it fades out (default the
		//! subtree blocks input while visible, and stops once effectively invisible)
		void setAlphaBlocksInput(bool enable);
		inline bool getAlphaBlocksInput() const { return this->alphaBlocksInput; }
		//! @brief does the widget currently accept input? Effectively enabled AND
		//! (visible enough OR not alpha-gated). The manager's dispatch gates on this.
		bool acceptsInput() const;
		//! @brief recompute the render transform from the current scale/rotation and
		//! the widget's live centre, and push it to the visual elements. Called on a
		//! scale/rotation change (and each animation step, so a moving+scaling widget
		//! keeps its pivot at the centre).
		void refreshRenderTransform();
		//! @brief apply a resolved 2D transform to this widget's visual elements.
		//! The base is a no-op; visual widgets (decor/label/textbox/text field) push
		//! it to their UiRenderer elements, composites forward to their sub-widgets.
		virtual void applyRenderTransform(Ui2DTransform const & transform) { (void)transform; }
		//! @brief apply a resolved alpha multiplier (0..1) to this widget's visual
		//! elements. Same override pattern as applyRenderTransform.
		virtual void applyRenderAlpha(float alphaMultiplier) { (void)alphaMultiplier; }
		//! @brief set the show/hide transition from a declarative string
		//! ("fade 0.2", "slide-up 0.3", "pop", "none"). @see GuiManager::playWidgetTransition
		void setTransition(String const & spec);
		//! @brief the parsed transition spec (UTT_None = snap)
		inline UiTransitionSpec const & getTransition() const { return this->transition; }

		//--- rect-anchor layout (opt-in; @see GuiManager resolve pass) ---
		//! @brief parent this widget under another: it then resolves inside the
		//! parent's rect. A parentless widget resolves against the screen root.
		void setParent(optr<GuiWidget> const & parent);
		//! @brief anchor min/max as parent-rect fractions 0..1 (min==max on an
		//! axis = a point anchor; min<max = stretch with the parent)
		void setAnchors(float minX, float minY, float maxX, float maxY);
		//! @brief anchor min/max from a named preset (TopLeft, Center,
		//! StretchAll, StretchTop, ... - the 9-way alignment vocabulary plus the
		//! stretch bands); case-insensitive
		void setAnchorPreset(String const & preset);
		//! @brief the 0..1 pivot the anchoredPosition refers to inside the rect
		void setPivot(float x, float y);
		//! @brief offsets from the anchor rect corners to the widget corners,
		//! design units (left/top = offsetMin, right/bottom = offsetMax)
		void setOffsets(float left, float top, float right, float bottom);
		//! @brief position the pivot point relative to the anchor (design units)
		void setAnchoredPosition(float x, float y);
		//! @brief size added beyond the anchor-rect span (design units)
		void setSizeDelta(float width, float height);
		//! @brief resolve a parentless widget against the safe rect (window minus
		//! the notch/home-bar insets) instead of the full window
		void setUseSafeArea(bool enable);

		//--- layout groups + content-size-fit (opt-in; @see the 2c core) ---
		//! @brief make this widget a group that auto-arranges its layout
		//! children, overriding their anchors ("none"/"horizontal"/"vertical"/
		//! "grid"; case-insensitive). "none" reverts to plain anchor children.
		void setLayoutGroup(String const & type);
		//! @brief a group's inner padding, in design units (left/top/right/bottom)
		void setGroupPadding(float left, float top, float right, float bottom);
		//! @brief a group's between-children gap (design units); the second value
		//! is the grid's row gap (<= 0 reuses the first for both axes)
		void setGroupSpacing(float spacing, float spacingY = 0.0f);
		//! @brief a horizontal/vertical group's cross-axis child alignment
		//! ("start"/"center"/"end")
		void setChildAlignment(String const & align);
		//! @brief stretch a horizontal/vertical group's children across the cross axis
		void setChildForceExpand(bool enable);
		//! @brief a grid group's cell size in design units
		void setGridCellSize(float width, float height);
		//! @brief a grid group's constraint ("flexible"/"columns"/"rows") + count
		void setGridConstraint(String const & constraint, int count);
		//! @brief content-size-fit per axis ("none"/"preferred"): a preferred axis
		//! sizes the widget to its measured content (a button to its label, a
		//! group to its children)
		void setContentSizeFit(String const & horizontal, String const & vertical);

		//! @brief is this widget placed by the layout resolver?
		inline bool isLayoutEnabled() const { return this->layoutEnabled; }
		//! @brief the layout descriptor (the resolver's input; @see GuiManager)
		inline LayoutNode const & getLayoutNode() const { return this->layout; }
		//! @brief the group arrangement descriptor (LGT_None = not a group)
		inline LayoutGroup const & getLayoutGroup() const { return this->layoutGroup; }
		//! @brief the content-size-fit descriptor
		inline LayoutContentFit const & getContentFit() const { return this->layoutFit; }
		//! @brief the layout parent (empty for a screen-root child)
		inline woptr<GuiWidget> getLayoutParent() const { return this->layoutParent; }
		//! @brief resolve against the safe-area root (parentless widgets only)
		inline bool getUseSafeArea() const { return this->layoutUseSafeArea; }
		//! @brief the intrinsic preferred content size fed to content-size-fit and
		//! group arrangement. The base is the widget's current size; text widgets
		//! override to measure their caption.
		virtual Ogre::Vector2 getPreferredSize();
		//! @brief a scroll viewport's current scroll offset in window pixels
		//! (0 for every other widget); the resolver shifts this widget's children
		//! by it. @see GuiScrollView
		virtual LayoutVec2 getScrollOffset() const { return LayoutVec2(); }
		//! @brief the manager hands a scroll viewport its resolved size + the
		//! content's preferred extent each relayout (a no-op on plain widgets)
		virtual void onLayoutResolved(Ogre::Vector2 const & viewportSize,
			Ogre::Vector2 const & contentExtent) { (void)viewportSize; (void)contentExtent; }
		//! @brief a mouse-wheel notch over @p cursorPos (@p wheelDelta = 120 per
		//! notch, sign = direction); a scroll viewport scrolls, others ignore it
		virtual void onMouseWheel(Ogre::Vector2 const & cursorPos, int wheelDelta)
		{ (void)cursorPos; (void)wheelDelta; }
		//! @brief push a resolved absolute rect into the widget (setPosition +
		//! setSize); the resolver calls this each relayout
		void applyResolvedRect(float x, float y, float width, float height);

		//! show or hide
		//void setVisibility(bool enable);
		//bool getVisibility();
		//! for priority sorting
		inline bool operator < (GuiWidget const & other) const;
	protected:
		//! @brief called when the enabled state flips (setEnabled). The base is
		//! a no-op; interactive widgets override it to swap a `_disabled` sprite
		//! or dim their decor/text. @param enable the new state.
		virtual void onEnabledChanged(bool enable) { (void)enable; }
		//! opt into the layout system on the first layout setter, seeding the
		//! node from the widget's current on-screen rect so it stays put until
		//! anchors/offsets change
		void ensureLayout();
	private:
	};
	//---------------------------------------------------------------
	inline UiLayer* GuiWidget::getLayer() const
	{
		return this->layer;
	}
	//---------------------------------------------------------------
	inline woptr<GuiView> GuiWidget::getView()
	{
		return this->view;
	}
	//---------------------------------------------------------------
	inline bool GuiWidget::operator < (GuiWidget  const & other) const 
	{
		return this->getLayer()->getIndex() > other.getLayer()->getIndex();
	}
	//---------------------------------------------------------------
	struct GuiWidgetOptrCmp
	{
		inline bool operator()( optr<GuiWidget> const & lhs, optr<GuiWidget> const & rhs)
		{
			return *lhs < *rhs;
		}
	};
}

#endif //__GuiWidget_h__27_10_2010__13_08_39__