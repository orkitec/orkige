/********************************************************************
	created:	Saturday 2026/07/11 at 18:45
	filename: 	GuiToast.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __GuiToast_h__11_7_2026__18_45_00__
#define __GuiToast_h__11_7_2026__18_45_00__

#include "engine_gui/GuiDecorWidget.h"
#include "engine_gui/GuiLabel.h"

namespace Orkige
{
	//! @brief a passive timed notification: a rounded backing panel with a
	//! centered caption, faded in and out by the GuiManager from the pure
	//! ToastQueue (core_util/ToastQueue) that sequences and times toasts. It has
	//! NO input handler, so it never changes input routing. The manager owns a
	//! single toast and repositions/refills it as queued messages surface.
	class ORKIGE_ENGINE_DLL GuiToast : public GuiWidget
	{
		OOBJECT(GuiToast, GuiWidget);
		//--- Variables ---------------------------------------------
	protected:
		optr<GuiDecorWidget>	decor;	//!< backing panel (nine-slice sprite)
		optr<GuiLabel>			label;	//!< the message text
		//--- Methods -----------------------------------------------
	public:
		GuiToast(String const & id, String const & spriteName, uint fontIndex,
			String const & text, Ogre::Vector2 const & position,
			Ogre::Vector2 const & size, String const & atlas, uint z);
		virtual ~GuiToast();

		virtual void setPosition(Ogre::Real left, Ogre::Real top);
		virtual void setSize(Ogre::Real width, Ogre::Real height);
		virtual Ogre::Vector2 getSize();
		virtual Ogre::Vector2 getPosition();

		//! set the message text
		void setText(String const & text);
		//! fade the whole toast (backing + text) to @p alpha (0..1)
		void setToastAlpha(float alpha);
	protected:
	private:
	};
}

#endif //__GuiToast_h__11_7_2026__18_45_00__
