/********************************************************************
	created:	Saturday 2026/07/11 at 03:30
	filename: 	FontPacker.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	purpose:	the pure shelf allocator behind the runtime font atlas
				(@see FontPacker.h). No render system, no image data.
*********************************************************************/

#include "engine_gui/FontPacker.h"

namespace Orkige
{
	//---------------------------------------------------------
	FontPacker::FontPacker()
		: mWidth(0), mHeight(0), mPadding(1), mBottom(0)
	{
	}
	//---------------------------------------------------------
	FontPacker::FontPacker(uint width, uint height, uint padding)
		: mWidth(width), mHeight(height), mPadding(padding), mBottom(0)
	{
	}
	//---------------------------------------------------------
	void FontPacker::configure(uint width, uint height, uint padding)
	{
		this->mWidth = width;
		this->mHeight = height;
		this->mPadding = padding;
		this->mShelves.clear();
		this->mBottom = 0;
	}
	//---------------------------------------------------------
	bool FontPacker::allocate(uint w, uint h, Rect & out)
	{
		if(w == 0 || h == 0 || w > this->mWidth || h > this->mHeight)
		{
			return false;
		}
		// footprint including the transparent gutter reserved around the box
		const uint boxW = w + this->mPadding;
		const uint boxH = h + this->mPadding;

		// best fit: the shelf with the smallest band that still holds the box
		// and has horizontal room left. Deterministic (index order breaks ties)
		Shelf* chosen = NULL;
		for(Shelf & shelf : this->mShelves)
		{
			if(shelf.height >= boxH &&
				shelf.cursorX + boxW <= this->mWidth &&
				(chosen == NULL || shelf.height < chosen->height))
			{
				chosen = &shelf;
			}
		}

		if(chosen == NULL)
		{
			// open a new shelf at the bottom if the page has vertical room
			if(this->mBottom + boxH > this->mHeight)
			{
				return false;
			}
			Shelf shelf;
			shelf.y = this->mBottom;
			shelf.height = boxH;
			shelf.cursorX = 0;
			this->mShelves.push_back(shelf);
			this->mBottom += boxH;
			chosen = &this->mShelves.back();
		}

		out.x = chosen->cursorX;
		out.y = chosen->y;
		out.w = w;
		out.h = h;
		chosen->cursorX += boxW;
		return true;
	}
	//---------------------------------------------------------
	void FontPacker::reset()
	{
		this->mShelves.clear();
		this->mBottom = 0;
	}
}
