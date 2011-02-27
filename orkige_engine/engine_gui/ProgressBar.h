/********************************************************************
	created:	Wednesday 2010/08/04 at 15:08
	filename: 	ProgressBar.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
*********************************************************************/
#ifndef __ProgressBar_h__4_8_2010__15_08_28__
#define __ProgressBar_h__4_8_2010__15_08_28__

#include "engine_gui/Widget.h"

namespace Orkige
{
	/** \addtogroup Gui
	*  @{ */
	//! Bar Widget that shows progress (for loading etc)
	class ProgressBar : public Widget
	{
		OOBJECT(ProgressBar, Widget);
		//--- Types -------------------------------------------------
	public:
	protected:
	private:
		//--- Variables ---------------------------------------------
	public:
	protected:
		Ogre::TextAreaOverlayElement* textArea;			//!< loading bar text
		Ogre::TextAreaOverlayElement* commentTextArea;	//!< comments
		Ogre::OverlayElement* meter;					//!< the actual "bar"
		Ogre::OverlayElement* fill;						//!< shows bar progress
		Ogre::Real progress;							//!< the progress in percent
	private:
		//--- Methods -----------------------------------------------
	public:
		//! @brief create progressbar
		//! @copydoc Widget
		ProgressBar(String const & name, String const & materialGroup, const Ogre::DisplayString& caption, Ogre::Real width, Ogre::Real commentBoxWidth);

		//! Sets the progress as a percentage.
		void setProgress(Ogre::Real progress);

		//! Gets the progress as a percentage.
		Ogre::Real getProgress();
		//! set bar caption
		const Ogre::DisplayString& getCaption();
		//! get bar caption
		void setCaption(const Ogre::DisplayString& caption);
		//! get bar comment
		const Ogre::DisplayString& getComment();
		//! set bar comment
		void setComment(const Ogre::DisplayString& comment);
	protected:
	private:
	};
	/** @} End of "addtogroup Gui"*/
	//---------------------------------------------------------------
}

#endif //__ProgressBar_h__4_8_2010__15_08_28__