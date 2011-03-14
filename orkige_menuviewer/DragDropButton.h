/********************************************************************
	created:	Monday 2010/10/11
	filename:	DragDropButton.h
	author:		philipp.engelhard
	notice:		based on Orkige::Button
	copyright:	(c) 2010 kunst-stoff
*********************************************************************/

#include "engine_gui/Widget.h"

#ifndef __DragDropButton_h__
#define __DragDropButton_h__

namespace CC
{
	/** \addtogroup Gui
	*  @{ */
	//! Drag and Drop Button Widget
	class DragDropButton : public Orkige::Widget
	{
		OOBJECT(DragDropButton, Widget);
		//--- Types -------------------------------------------------
	public:
		//! @brief triggered when a button is released
		//! @ingroup EngineEvents
		DECL_EVENTTYPE(ButtonHitEvent);

		//! enumerator values for button states
		enum ButtonState   
		{
			BS_UP,
			BS_OVER,
			BS_DOWN,
			BS_DRAGGING,
		};
	protected:
	private:
		//--- Variables ---------------------------------------------
	public:
	protected:
		ButtonState							state;			//!< current button state
		Ogre::BorderPanelOverlayElement*	borderPanel;	//!< button border
		Ogre::TextAreaOverlayElement*		textArea;		//!< button text
		Ogre::OverlayElement*				imageOverlay;	//!< image overlay
		bool								fitToContents;	//!< if true button size depends on Textlength
		bool								isDragging;		//!< are we currently in draggin state?
		Ogre::MovableObject*				pickedObject;	//!< poiter to last picked object
		Ogre::Entity*						dragee;			//!< the dragged object
		Ogre::OverlayContainer*				imageOverlayContainer; //!< container of the image overlay
		Ogre::Vector2						imageToCursorOffset; //!< offset between the image and the cursor position
		Orkige::Event						pickEvent;		//!< 


	private:
		//--- Methods -----------------------------------------------
	public:
		//! constructor
		DragDropButton(const Orkige::String& name, const Orkige::String& materialGroup, const Orkige::String& templateName, const Ogre::DisplayString& caption, Ogre::Real width);
		//! destructor
		virtual ~DragDropButton();

		//! get button caption
		const Ogre::DisplayString& getCaption();
		//! set button caption
		void setCaption(const Ogre::DisplayString& caption);
		//! get current ButtonState
		const ButtonState& getState();

		virtual void onCursorPressed(const Ogre::Vector2& cursorPos);
		virtual void onCursorReleased(const Ogre::Vector2& cursorPos);
		virtual void onCursorMoved(const Ogre::Vector2& cursorPos);
		virtual void onFocusLost();

	protected:
		//! set current ButtonState 
		void	setState		(const ButtonState& bs);

	private:
	};
}

#endif //__DragDropButton_h__