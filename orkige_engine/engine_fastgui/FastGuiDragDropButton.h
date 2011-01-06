/********************************************************************
	created:	Monday 2010/11/01
	filename:	FastGuiDragDropButton.h
	author:		philipp.engelhard
	notice:		based on Orkige::FastGuiButton
	copyright:	(c) 2010 kunst-stoff
*********************************************************************/
#ifndef __ORKIGE__FastGuiDragDropButton_h__
#define __ORKIGE__FastGuiDragDropButton_h__

#include "engine_fastgui/FastGuiDecorWidget.h"
#include "engine_fastgui/FastGuiLabel.h"
#include "engine_fastgui/DragEventData.h"

namespace Orkige
{
	class FastGuiDragDropButton : public FastGuiWidget
	{
		//class DragEventData;
		OOBJECT(FastGuiDragDropButton, FastGuiWidget);
		//--- Types -------------------------------------------------
	public:
		//! triggered when dragging occurs
		DECL_EVENTTYPE(DragEndEvent);

		//! enumerator values for button states
		enum DragDropButtonState   
		{
			DDBS_UP,
			DDBS_OVER,
			DDBS_DOWN,
			DDBS_DRAGGING,
		};
	protected:
	private:
		//--- Variables ---------------------------------------------
	public:
	protected:
		optr<FastGuiLabel>					label;			//!< current button text
		optr<FastGuiDecorWidget>			decor;			//!< current button image
		optr<FastGuiDecorWidget>			background;		//!< current button backround image
		DragDropButtonState					state;			//!< current button state
		Orkige::String						baseSpriteName;	//!< base name of the button state sprite;
		Orkige::Event						dragEvent;		//!< drag event
		optr<DragEventData>					dragEventData;	//!< event data
		Ogre::Vector2						initialDecorPosition; //!< initial position of the decor to snap it back after it gets moved around
		Ogre::Vector2						imageToCursorOffset; //!< offset between the image and the cursor position
		bool								isEnabled;
		Ogre::Vector2						initialWidgetPosition; //!< initial position of the decor to snap it back after it gets moved around

	private:
		//--- Methods -----------------------------------------------
	public:
		FastGuiDragDropButton(String const & id, String const & spriteName, unsigned int defaultGlyphIndex, String const & text, Ogre::Vector2 const & position, FastGuiLabel::LabelAlignment textAlignment, Ogre::Vector2 const & size, String const & atlas, unsigned int z);
		virtual ~FastGuiDragDropButton();

		//! get current ButtonState
		const DragDropButtonState& getState();

		virtual void setPosition(Ogre::Real left, Ogre::Real top);
		virtual void setSize(Ogre::Real width, Ogre::Real height);
		virtual Ogre::Vector2 getSize();
		virtual Ogre::Vector2 getPosition();

		virtual void onCursorPressed(Ogre::Vector2 const & cursorPos);
		virtual void onCursorReleased(Ogre::Vector2 const & cursorPos);
		virtual void onCursorMoved(Ogre::Vector2 const & cursorPos);
	protected:
		//! set current ButtonState 
		void setState(const DragDropButtonState& bs);
		void dragging(const Ogre::Vector2& cursorPos);
		void startDragging(const Ogre::Vector2& cursorPos);
	private:
	};
	//---------------------------------------------------------------
}

#endif //__ORKIGE__FastGuiDragDropButton_h__