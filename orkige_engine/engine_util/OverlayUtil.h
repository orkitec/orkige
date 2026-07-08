/********************************************************************
	created:	Thursday 2010/08/05 at 17:37
	filename: 	OverlayUtil.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
	
	purpose:	Gui/overlay utility methods
*********************************************************************/
#ifndef __OverlayUtil_h__5_8_2010__17_37_27__
#define __OverlayUtil_h__5_8_2010__17_37_27__

#include "engine_module/EnginePrerequisites.h"
#include <math.h>

namespace Orkige
{
	//! utilities for handling overlays
	namespace OverlayUtil
	{
		//! utility method to recursively delete an overlay element plus all of its children from the system.
		inline void nukeOverlayElement(Ogre::OverlayElement* element)
		{
			Ogre::OverlayContainer* container = dynamic_cast<Ogre::OverlayContainer*>(element);
			if (container)
			{
				std::vector<Ogre::OverlayElement*> toDelete;

				Ogre::OverlayContainer::ChildIterator children = container->getChildIterator();
				while (children.hasMoreElements())
				{
					toDelete.push_back(children.getNext());
				}

				for (unsigned int i = 0; i < toDelete.size(); i++)
				{
					nukeOverlayElement(toDelete[i]);
				}
			}
			if (element)
			{
				Ogre::OverlayContainer* parent = element->getParent();
				if (parent) parent->removeChild(element->getName());
				Ogre::OverlayManager::getSingleton().destroyOverlayElement(element);
			}
		}
		//---------------------------------------------------------------
		//! utility method to check if the cursor is over an overlay element.
		inline bool isCursorOver(Ogre::OverlayElement* element, const Ogre::Vector2& cursorPos, Ogre::Real voidBorder = 0)
		{
			Ogre::OverlayManager& om = Ogre::OverlayManager::getSingleton();
			Ogre::Real l = element->_getDerivedLeft() * om.getViewportWidth();
			Ogre::Real t = element->_getDerivedTop() * om.getViewportHeight();
			Ogre::Real r = l + element->getWidth();
			Ogre::Real b = t + element->getHeight();

			return (cursorPos.x >= l + voidBorder && cursorPos.x <= r - voidBorder &&
				cursorPos.y >= t + voidBorder && cursorPos.y <= b - voidBorder);
		}
		//---------------------------------------------------------------
		//! utility method used to get the cursor's offset from the center
		inline Ogre::Vector2 cursorOffset(Ogre::OverlayElement* element, const Ogre::Vector2& cursorPos)
		{
			Ogre::OverlayManager& om = Ogre::OverlayManager::getSingleton();
			return Ogre::Vector2(cursorPos.x - (element->_getDerivedLeft() * om.getViewportWidth() + element->getWidth() / 2),
				cursorPos.y - (element->_getDerivedTop() * om.getViewportHeight() + element->getHeight() / 2));
		}
		//---------------------------------------------------------------
		//! utility method used to get the width of a caption in a text area.
		inline Ogre::Real getCaptionWidth(const Ogre::DisplayString& caption, Ogre::TextAreaOverlayElement* area)
		{
			Ogre::Font* font = (Ogre::Font*)Ogre::FontManager::getSingleton().getByName(area->getFontName()).get();
			Ogre::String current = caption.asUTF8();
			Ogre::Real lineWidth = 0;

			for (unsigned int i = 0; i < current.length(); i++)
			{
				// be sure to provide a line width in the text area
				if (current[i] == ' ')
				{
					if (area->getSpaceWidth() != 0) lineWidth += area->getSpaceWidth();
					else lineWidth += font->getGlyphAspectRatio(' ') * area->getCharHeight();
				}
				else if (current[i] == '\n') break;
				// use glyph information to calculate line width
				else lineWidth += font->getGlyphAspectRatio(current[i]) * area->getCharHeight();
			}

			return (Ogre::Real)((unsigned int)lineWidth);
		}
		//---------------------------------------------------------------
		//! utility method to cut off a string to fit in a text area.
		inline void fitCaptionToArea(const Ogre::DisplayString& caption, Ogre::TextAreaOverlayElement* area, Ogre::Real maxWidth)
		{
			Ogre::Font* f = (Ogre::Font*)Ogre::FontManager::getSingleton().getByName(area->getFontName()).get();
			Ogre::String s = caption.asUTF8();

			int nl = s.find('\n');
			if (nl != -1) s = s.substr(0, nl);

			Ogre::Real width = 0;

			for (unsigned int i = 0; i < s.length(); i++)
			{
				if (s[i] == ' ' && area->getSpaceWidth() != 0) width += area->getSpaceWidth();
				else width += f->getGlyphAspectRatio(s[i]) * area->getCharHeight();
				if (width > maxWidth)
				{
					s = s.substr(0, i);
					break;
				}
			}

			area->setCaption(s);
		}
		//---------------------------------------------------------------
	}
}

#endif //__OverlayUtil_h__5_8_2010__17_37_27__
