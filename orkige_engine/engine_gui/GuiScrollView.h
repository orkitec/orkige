/********************************************************************
	created:	Saturday 2026/07/11 at 15:00
	filename: 	GuiScrollView.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __GuiScrollView_h__11_7_2026__15_00_00__
#define __GuiScrollView_h__11_7_2026__15_00_00__

#include "engine_gui/GuiWidget.h"
#include <core_util/ScrollMomentum.h>

namespace Orkige
{
	//! @brief a scroll viewport: a clipping container whose layout child (a
	//! content subtree, usually a vertical group) can be taller/wider than the
	//! viewport and is scrolled by drag or the mouse wheel. The content is
	//! clipped to the viewport via the layer scissor (analytic + backend-
	//! identical, @see DrawLayer2D) and offset by the scroll amount through the
	//! pure layout resolver (so children stay hit-testable at their shifted
	//! rects). The scroll view draws nothing itself; author its content widgets
	//! with the SAME z (the clip layer) and parent them under it. The clamp
	//! (offset pinned to [min(0, viewport-content), 0]) lives in core_util/
	//! UiLayout::clampScroll.
	class ORKIGE_ENGINE_DLL GuiScrollView : public GuiWidget
	{
		OOBJECT(GuiScrollView, GuiWidget);
	public:
		//! @param position/size the viewport rect (the layout resolver overwrites
		//! it when the view opts into anchors); @param z the clip/content layer
		GuiScrollView(String const & id, Ogre::Vector2 const & position,
			Ogre::Vector2 const & size, String const & atlas, uint z);
		virtual ~GuiScrollView();

		virtual void setPosition(Ogre::Real left, Ogre::Real top);
		virtual void setSize(Ogre::Real width, Ogre::Real height);
		virtual Ogre::Vector2 getSize();
		virtual Ogre::Vector2 getPosition();

		//! the scroll offset (window pixels; <= 0, the resolver shifts the
		//! content by it)
		virtual LayoutVec2 getScrollOffset() const;
		//! the manager hands over the resolved viewport + content extent so the
		//! next drag/wheel clamps correctly and the clip rect follows the viewport
		virtual void onLayoutResolved(Ogre::Vector2 const & viewportSize,
			Ogre::Vector2 const & contentExtent);
		virtual void onMouseWheel(Ogre::Vector2 const & cursorPos, int wheelDelta);

		virtual void onCursorPressed(Ogre::Vector2 const & cursorPos);
		virtual void onCursorReleased(Ogre::Vector2 const & cursorPos);
		virtual void onCursorMoved(Ogre::Vector2 const & cursorPos);
		//! @brief advance the flick inertia + rubber-band spring-back each frame
		virtual bool onFrameStarted(FrameEventData const & data);

		//! @brief scroll to an absolute offset (clamped); +/- moves the content
		void setScroll(float offsetY);
		inline float getScroll() const { return this->scrollY; }
		//! how far the content can scroll (content extent - viewport, >= 0)
		float getMaxScroll() const;

		//! does a point fall inside the viewport rect?
		bool containsPoint(Ogre::Vector2 const & point) const;
	protected:
		Real left, top, width, height;		//!< the viewport rect (window pixels)
		float scrollY;						//!< current vertical scroll (cache of the momentum offset; <= 0 inside bounds, transiently > 0 while rubber-banding)
		Real viewportExtent;				//!< viewport height (clamp input)
		Real contentExtent;					//!< content preferred height (clamp input)
		bool dragging;						//!< a press claimed the viewport
		Real dragLastY;						//!< last drag cursor y
		//! @brief the flick-inertia + rubber-band physics (@see ScrollMomentum);
		//! the widget forwards drags/wheel/frame ticks and reads back the offset
		ScrollMomentum momentum;
	private:
	};
}

#endif //__GuiScrollView_h__11_7_2026__15_00_00__
