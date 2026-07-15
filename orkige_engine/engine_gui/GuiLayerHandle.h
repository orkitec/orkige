/********************************************************************
	created:	Wednesday 2026/07/15
	filename: 	GuiLayerHandle.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __GuiLayerHandle_h__15_7_2026__
#define __GuiLayerHandle_h__15_7_2026__

//! A weak Lua handle to a widget's UiLayer. A layer is SCREEN-scoped - owned by
//! its GuiView's screen - so it dies WITH the view: an .oui hot-reload destroys
//! and rebuilds a screen's widgets/layers mid-Play, and a preview surface tears
//! down on a device switch. A script that cached widget:getLayer() across either
//! event would otherwise hold a dangling raw pointer - exactly the class this
//! weak-handle surface closes. The handle keeps the view as the LIVENESS KEY (a
//! woptr<GuiView>, the same relation as GuiWidget::isLayerAlive) plus the
//! UiLayer* it guards; every bound method locks the view for the call and raises
//! an honest, pcall-catchable "layer handle is dead" error once the screen is
//! gone, on the standard reporting channel, while the app keeps running.
//!
//! Backend-neutral (no sol here): the struct + the lock helper are plain C++; the
//! GuiLayer usertype that binds show/hide/isVisible/setVisible onto it is
//! registered in engine_module/module.cpp, and getLayer's bindings (the widget
//! usertype and the WidgetHandle) hand back this handle via makeLayerHandle.

#include <stdexcept>

#include "core_util/optr.h"
#include "engine_gui/GuiView.h"
#include "engine_gui/UiRenderer.h"

namespace Orkige
{
	class GuiWidget;

	namespace MetaLuaDetail
	{
		//! a weak Lua handle to a widget's UiLayer - the view is the liveness key
		struct GuiLayerHandle
		{
			woptr<GuiView>	view;			//!< liveness key (screen owns the layer)
			UiLayer*		layer = NULL;	//!< guarded, valid only while view is alive
		};

		//! @brief validate a layer handle for a call or raise the honest error: the
		//! layer is only touchable while its owning view (screen) is alive.
		inline UiLayer* lockLayerHandle(GuiLayerHandle const & handle)
		{
			if (handle.view.expired() || handle.layer == NULL)
			{
				throw std::runtime_error("layer handle is dead");
			}
			return handle.layer;
		}
	}

	//! @brief build a weak layer handle from a live widget (its view = the
	//! liveness key, its UiLayer = the guarded target). Defined out-of-line where
	//! GuiWidget is complete (engine_gui/GuiWidget.cpp).
	MetaLuaDetail::GuiLayerHandle makeLayerHandle(GuiWidget & widget);
}

#endif
