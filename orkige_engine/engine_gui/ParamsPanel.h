/********************************************************************
	created:	Wednesday 2010/08/04 at 15:08
	filename: 	ParamsPanel.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
*********************************************************************/
#ifndef __ParamsPanel_h__4_8_2010__15_08_17__
#define __ParamsPanel_h__4_8_2010__15_08_17__

#include "engine_gui/Widget.h"

namespace Orkige
{
	/** \addtogroup Gui
	*  @{ */
	//! Panel which can show lists of parameters with values and their names
	class ParamsPanel : public Widget
	{
		OOBJECT(ParamsPanel, Widget);
		//--- Types -------------------------------------------------
	public:
	protected:
	private:
		//--- Variables ---------------------------------------------
	public:
	protected:
		Ogre::TextAreaOverlayElement* namesArea;	//!< area for param names
		Ogre::TextAreaOverlayElement* valuesArea;	//!< area for param values
		Ogre::StringVector names;					//!< param names
		Ogre::StringVector values;					//!< param values
	private:
		//--- Methods -----------------------------------------------
	public:
		//! @brief create empty params panel with given number of lines
		//! @copydoc Widget
		ParamsPanel(String const & name, String const & materialGroup, Ogre::Real width, unsigned int lines);

		//! set all param names
		void setAllParamNames(const Ogre::StringVector& paramNames);
		//! get all param names
		const Ogre::StringVector& getAllParamNames();

		//! set all param values
		void setAllParamValues(const Ogre::StringVector& paramValues);
		//! get all param values
		const Ogre::StringVector& getAllParamValues();

		//! set single param value for given name
		void setParamValue(const Ogre::DisplayString& paramName, const Ogre::DisplayString& paramValue);
		//! set single param value at given index
		void setParamValue(unsigned int index, const Ogre::DisplayString& paramValue);

		//! get param value for given name
		Ogre::DisplayString getParamValue(const Ogre::DisplayString& paramName);
		//! get param value at given index
		Ogre::DisplayString getParamValue(unsigned int index);

	protected:
		//! Internal method - updates text areas based on name and value lists.
		void updateText();
	private:
	};
	/** @} End of "addtogroup Gui"*/
	//---------------------------------------------------------------
}

#endif //__ParamsPanel_h__4_8_2010__15_08_17__