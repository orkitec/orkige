/********************************************************************
	created:	Wednesday 2010/08/04 at 15:09
	filename: 	Slider.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec	
*********************************************************************/
#ifndef __Slider_h__4_8_2010__15_09_05__
#define __Slider_h__4_8_2010__15_09_05__

#include "engine_gui/Widget.h"

namespace Orkige
{
	/** \addtogroup Gui
	*  @{ */
	//! Slider Widget
	class Slider : public Widget
	{
		OOBJECT(Slider, Widget);
		//--- Types -------------------------------------------------
	public:
		//! @brief triggered when slider is moved
		//! @ingroup EngineEvents
		DECL_EVENTTYPE(SliderMovedEvent);
	protected:
	private:
		//--- Variables ---------------------------------------------
	public:
	protected:
		Ogre::TextAreaOverlayElement* textArea;			//!< slider caption
		Ogre::TextAreaOverlayElement* valueTextArea;	//!< slider value
		Ogre::BorderPanelOverlayElement* track;			//!< slide track
		Ogre::PanelOverlayElement* handle;				//!< tracking handle
		bool dragging;									//!< currently dragging
		bool fitToContents;								//!< fit slider size to content
		Ogre::Real dragOffset;							//!< current dragging offset
		Ogre::Real value;								//!< current slider value
		Ogre::Real minValue;							//!< min slider value
		Ogre::Real maxValue;							//!< max slider value
		Ogre::Real interval;							//!< sliding intervalls
	private:
		//--- Methods -----------------------------------------------
	public:
		//! @brief create Slider
		//! @copydoc Widget
		Slider(String const & name, String const & materialGroup, const Ogre::DisplayString& caption, Ogre::Real width, Ogre::Real trackWidth,
			Ogre::Real valueBoxWidth, Ogre::Real minValue, Ogre::Real maxValue, unsigned int snaps);

		//! Sets the minimum value, maximum value, and the number of snapping points.
		void setRange(Ogre::Real minValue, Ogre::Real maxValue, unsigned int snaps, bool notifyListener = true);
		//! get caption of the value
		const Ogre::DisplayString& getValueCaption();

		//! You can use this method to manually format how the value is displayed.
		void setValueCaption(const Ogre::DisplayString& caption);
		//! set value
		void setValue(Ogre::Real value, bool notifyListener = true);
		//! get value
		Ogre::Real getValue();
		//! get caption
		const Ogre::DisplayString& getCaption();
		//! set caption
		void setCaption(const Ogre::DisplayString& caption);

		// widget overloads
		virtual void onCursorPressed(Ogre::Vector2 const & cursorPos);
		virtual void onCursorReleased(Ogre::Vector2 const & cursorPos);
		virtual void onCursorMoved(Ogre::Vector2 const & cursorPos);
		virtual void onFocusLost();
	protected:
		
		//! Internal method - given a percentage (from left to right), gets the value of the nearest marker.
		Ogre::Real getSnappedValue(Ogre::Real percentage);
	private:
	};
	/** @} End of "addtogroup Gui"*/
	//---------------------------------------------------------------
}

#endif //__Slider_h__4_8_2010__15_09_05__