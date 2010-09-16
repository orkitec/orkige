/********************************************************************
	created:	Wednesday 2010/08/04 at 15:07
	filename: 	Label.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec
*********************************************************************/
#ifndef __Label_h__4_8_2010__15_07_59__
#define __Label_h__4_8_2010__15_07_59__

#include "engine_gui/Widget.h"

namespace Orkige
{
	/** \addtogroup Gui
	*  @{ */
	//! Label Widget
	class Label : public Widget
	{
		OOBJECT(Label, Widget);
		//--- Types -------------------------------------------------
	public:
		//! @brief triggered when Label is clicked
		//! @ingroup EngineEvents
		DECL_EVENTTYPE(LabelHitEvent);
	protected:
	private:
		//--- Variables ---------------------------------------------
	public:
	protected:
		Ogre::TextAreaOverlayElement* textArea;	//!< Label text
		bool fitToTray;							//!< if true size depends on content
	private:
		//--- Methods -----------------------------------------------
	public:
		//! @brief create a Label
		//! @copydoc Widget
		Label(String const & name, String const & materialGroup, const Ogre::DisplayString& caption, Ogre::Real width);
		//! get label caption
		const Ogre::DisplayString& getCaption();
		//! set label caption
		void setCaption(const Ogre::DisplayString& caption);
		//! @see Label::fitToTray
		bool _isFitToTray();

		virtual void onCursorPressed(Ogre::Vector2 const & cursorPos);
	protected:
	private:
	};
	/** @} End of "addtogroup Gui"*/
	//---------------------------------------------------------------
}

#endif //__Label_h__4_8_2010__15_07_59__