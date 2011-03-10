/********************************************************************
	created:	Monday 2010/10/11
	filename:	CcGuiFactory.h
	author:		philipp.engelhard
	notice:		
	copyright:	(c) 2010 kunst-stoff
*********************************************************************/

#include "engine_gui/GuiFactory.h"
#include "cc_gui/DragDropButton.h"

#ifndef __CcGuiFactory_h__
#define __CcGuiFactory_h__

namespace CC
{

	class CcGuiFactory : public Orkige::GuiFactory
	{
		//--- Types -------------------------------------------------
	public:
	protected:
	private:
		//--- Variables ---------------------------------------------
	public:
	protected:
	private:
		//--- Methods -----------------------------------------------
	public:
		//! constructor
		CcGuiFactory();
		//! destructor
		virtual ~CcGuiFactory();

		//! create a Button
		virtual DragDropButton* createDragDropButton(Orkige::Widget::TrayLocation trayLoc, Orkige::String const & name, Orkige::String const & materialGroup, Orkige::String const & templateName, Orkige::String const & caption, Ogre::Real width = 0);
	protected:
	private:
	};
}

#endif //__CcGuiFactory_h__