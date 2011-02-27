/********************************************************************
	created:	Wednesday 2010/08/04 at 15:09
	filename: 	TextBox.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec	
*********************************************************************/

#include "engine_gui/TextBox.h"

namespace Orkige
{
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	TextBox::TextBox(String const & name, String const & materialGroup, const Ogre::DisplayString& caption, Ogre::Real width, Ogre::Real height) : Widget(name, materialGroup)
	{
		this->overlayElement = Ogre::OverlayManager::getSingleton().createOverlayElementFromTemplate(this->materialGroup + "/TextBox", "BorderPanel", name);
		this->overlayElement->setWidth(width);
		this->overlayElement->setHeight(height);
		Ogre::OverlayContainer* container = (Ogre::OverlayContainer*)this->overlayElement;
		this->textArea = (Ogre::TextAreaOverlayElement*)container->getChild(this->getName() + "/TextBoxText");
		this->captionBar = (Ogre::BorderPanelOverlayElement*)container->getChild(this->getName() + "/TextBoxCaptionBar");
		this->captionBar->setWidth(width - 4);
		this->captionTextArea = (Ogre::TextAreaOverlayElement*)this->captionBar->getChild(this->captionBar->getName() + "/TextBoxCaption");
		this->setCaption(caption);
		this->scrollTrack = (Ogre::BorderPanelOverlayElement*)container->getChild(this->getName() + "/TextBoxScrollTrack");
		this->scrollHandle = (Ogre::PanelOverlayElement*)this->scrollTrack->getChild(this->scrollTrack->getName() + "/TextBoxScrollHandle");
		this->scrollHandle->hide();
		this->dragging = false;
		this->scrollPercentage = 0;
		this->startingLine = 0;
		this->padding = 15;
		this->text = "";
#if OGRE_PLATFORM == OGRE_PLATFORM_IPHONE
		this->textArea->setCharHeight(this->textArea->getCharHeight() - 3);
		this->captionTextArea->setCharHeight(this->captionTextArea->getCharHeight() - 3);
#endif
		this->refitContents();
	}
	//---------------------------------------------------------
	void TextBox::setPadding(Ogre::Real padding)
	{
		this->padding = padding;
		this->refitContents();
	}
	//---------------------------------------------------------
	Ogre::Real TextBox::getPadding()
	{
		return this->padding;
	}
	//---------------------------------------------------------
	const Ogre::DisplayString& TextBox::getCaption()
	{
		return this->captionTextArea->getCaption();
	}
	//---------------------------------------------------------
	void TextBox::setCaption(const Ogre::DisplayString& caption)
	{
		this->captionTextArea->setCaption(caption);
	}
	//---------------------------------------------------------
	const Ogre::DisplayString& TextBox::getText()
	{
		return this->text;
	}
	//---------------------------------------------------------
	void TextBox::setText(const Ogre::DisplayString& text)
	{
		this->text = text;
		this->lines.clear();

		Ogre::Font* font = (Ogre::Font*)Ogre::FontManager::getSingleton().getByName(this->textArea->getFontName()).getPointer();

		Ogre::String current = text.asUTF8();
		bool firstWord = true;
		unsigned int lastSpace = 0;
		unsigned int lineBegin = 0;
		Ogre::Real lineWidth = 0;
		Ogre::Real rightBoundary = this->overlayElement->getWidth() - 2 * this->padding + this->scrollTrack->getLeft() + 10;

		for (unsigned int i = 0; i < current.length(); i++)
		{
			if (current[i] == ' ')
			{
				if (this->textArea->getSpaceWidth() != 0)
				{
					lineWidth += this->textArea->getSpaceWidth();
				}
				else 
				{
					lineWidth += font->getGlyphAspectRatio(' ') * this->textArea->getCharHeight();
				}
				firstWord = false;
				lastSpace = i;
			}
			else if (current[i] == '\n')
			{
				firstWord = true;
				lineWidth = 0;
				this->lines.push_back(current.substr(lineBegin, i - lineBegin));
				lineBegin = i + 1;
			}
			else
			{
				// use glyph information to calculate line width
				lineWidth += font->getGlyphAspectRatio(current[i]) * this->textArea->getCharHeight();
				if (lineWidth > rightBoundary)
				{
					if (firstWord)
					{
						current.insert(i, "\n");
						i = i - 1;
					}
					else
					{
						current[lastSpace] = '\n';
						i = lastSpace - 1;
					}
				}
			}
		}

		this->lines.push_back(current.substr(lineBegin));

		unsigned int maxLines = this->getHeightInLines();

		if (this->lines.size() > maxLines)     // if too much text, filter based on scroll percentage
		{
			this->scrollHandle->show();
			this->filterLines();
		}
		else       // otherwise just show all the text
		{
			this->textArea->setCaption(current);
			this->scrollHandle->hide();
			this->scrollPercentage = 0;
			this->scrollHandle->setTop(0);
		}
	}
	//---------------------------------------------------------
	void TextBox::setTextAlignment(Ogre::TextAreaOverlayElement::Alignment ta)
	{
		if (ta == Ogre::TextAreaOverlayElement::Left) this->textArea->setHorizontalAlignment(Ogre::GHA_LEFT);
		else if (ta == Ogre::TextAreaOverlayElement::Center) this->textArea->setHorizontalAlignment(Ogre::GHA_CENTER);
		else this->textArea->setHorizontalAlignment(Ogre::GHA_RIGHT);
		refitContents();
	}
	//---------------------------------------------------------
	void TextBox::clearText()
	{
		this->setText("");
	}
	//---------------------------------------------------------
	void TextBox::appendText(const Ogre::DisplayString& text)
	{
		this->setText(this->getText() + text);
	}
	//---------------------------------------------------------
	void TextBox::refitContents()
	{
		this->scrollTrack->setHeight(this->overlayElement->getHeight() - this->captionBar->getHeight() - 20);
		this->scrollTrack->setTop(this->captionBar->getHeight() + 10);

		this->textArea->setTop(this->captionBar->getHeight() + this->padding - 5);
		if (this->textArea->getHorizontalAlignment() == Ogre::GHA_RIGHT)
		{
			this->textArea->setLeft(-this->padding + this->scrollTrack->getLeft());
		}
		else if (this->textArea->getHorizontalAlignment() == Ogre::GHA_LEFT)
		{
			this->textArea->setLeft(this->padding);
		}
		else
		{
			this->textArea->setLeft(this->scrollTrack->getLeft() / 2);
		}

		this->setText(this->getText());
	}
	//---------------------------------------------------------
	void TextBox::setScrollPercentage(Ogre::Real percentage)
	{
		this->scrollPercentage = Ogre::Math::Clamp<Ogre::Real>(percentage, 0, 1);
		this->scrollHandle->setTop((Ogre::Real)(int)(percentage * (this->scrollTrack->getHeight() - this->scrollHandle->getHeight())));
		this->filterLines();
	}
	//---------------------------------------------------------
	Ogre::Real TextBox::getScrollPercentage()
	{
		return this->scrollPercentage;
	}
	//---------------------------------------------------------
	unsigned int TextBox::getHeightInLines()
	{
		unsigned int ret = (unsigned int) ((this->overlayElement->getHeight() - 2 * this->padding - this->captionBar->getHeight() + 5) / this->textArea->getCharHeight());
		return ret;
	}
	//---------------------------------------------------------
	void TextBox::onCursorPressed(const Ogre::Vector2& cursorPos)
	{
		if (!this->scrollHandle->isVisible()) 
		{
			return;   // don't care about clicks if text not scrollable
		}

		Ogre::Vector2 co = OverlayUtil::cursorOffset(this->scrollHandle, cursorPos);

		if (co.squaredLength() <= 81)
		{
			this->dragging = true;
			this->dragOffset = co.y;
		}
		else if (OverlayUtil::isCursorOver(this->scrollTrack, cursorPos))
		{
			Ogre::Real newTop = this->scrollHandle->getTop() + co.y;
			Ogre::Real lowerBoundary = this->scrollTrack->getHeight() - this->scrollHandle->getHeight();
			this->scrollHandle->setTop((Ogre::Real)Ogre::Math::Clamp<int>((int)newTop, 0, (int)lowerBoundary));

			// update text area contents based on new scroll percentage
			this->scrollPercentage = Ogre::Math::Clamp<Ogre::Real>(newTop / lowerBoundary, 0, 1);
			this->filterLines();
		}
	}
	//---------------------------------------------------------
	void TextBox::onCursorReleased(const Ogre::Vector2& cursorPos)
	{
		this->dragging = false;
	}
	//---------------------------------------------------------
	void TextBox::onCursorMoved(const Ogre::Vector2& cursorPos)
	{
		if (this->dragging)
		{
			Ogre::Vector2 co = OverlayUtil::cursorOffset(this->scrollHandle, cursorPos);
			Ogre::Real newTop = this->scrollHandle->getTop() + co.y - this->dragOffset;
			Ogre::Real lowerBoundary = this->scrollTrack->getHeight() - this->scrollHandle->getHeight();
			this->scrollHandle->setTop((Ogre::Real)Ogre::Math::Clamp<int>((int)newTop, 0, (int)lowerBoundary));

			// update text area contents based on new scroll percentage
			this->scrollPercentage = Ogre::Math::Clamp<Ogre::Real>(newTop / lowerBoundary, 0, 1);
			this->filterLines();
		}
	}
	//---------------------------------------------------------
	void TextBox::onFocusLost()
	{
		this->dragging = false;  // stop dragging if cursor was lost
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------
	void TextBox::filterLines()
	{
		Ogre::String shown = "";
		unsigned int maxLines = this->getHeightInLines();
		unsigned int newStart = (unsigned int) (this->scrollPercentage * (this->lines.size() - maxLines) + 0.5);

		this->startingLine = newStart;

		for (unsigned int i = 0; i < maxLines; i++)
		{
			shown += this->lines[this->startingLine + i] + "\n";
		}

		this->textArea->setCaption(shown);    // show just the filtered lines
	}
	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	OABSTRACT_IMPL(TextBox)
	OOBJECT_END
}