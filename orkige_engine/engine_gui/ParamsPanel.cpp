/********************************************************************
	created:	Wednesday 2010/08/04 at 15:08
	filename: 	ParamsPanel.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
*********************************************************************/

#include "engine_gui/ParamsPanel.h"

namespace Orkige
{
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	ParamsPanel::ParamsPanel(String const & name, String const & materialGroup, Ogre::Real width, unsigned int lines) : Widget(name, materialGroup)
	{
		this->overlayElement = Ogre::OverlayManager::getSingleton().createOverlayElementFromTemplate(this->materialGroup + "/ParamsPanel", "BorderPanel", name);
		Ogre::OverlayContainer* c = (Ogre::OverlayContainer*)this->overlayElement;
		this->namesArea = (Ogre::TextAreaOverlayElement*)c->getChild(this->getName() + "/ParamsPanelNames");
		this->valuesArea = (Ogre::TextAreaOverlayElement*)c->getChild(this->getName() + "/ParamsPanelValues");
#if OGRE_PLATFORM == OGRE_PLATFORM_IPHONE
		this->namesArea->setCharHeight(this->namesArea->getCharHeight() - 3);
		this->valuesArea->setCharHeight(this->valuesArea->getCharHeight() - 3);
#endif
		this->overlayElement->setWidth(width);
		this->overlayElement->setHeight(this->namesArea->getTop() * 2 + lines * this->namesArea->getCharHeight());
	}
	//---------------------------------------------------------
	void ParamsPanel::setAllParamNames(const Ogre::StringVector& paramNames)
	{
		this->names = paramNames;
		this->values.clear();
		this->values.resize(this->names.size(), "");
		this->overlayElement->setHeight(this->namesArea->getTop() * 2 + this->names.size() * this->namesArea->getCharHeight());
		this->updateText();
	}
	//---------------------------------------------------------
	const Ogre::StringVector& ParamsPanel::getAllParamNames()
	{
		return this->names;
	}
	//---------------------------------------------------------
	void ParamsPanel::setAllParamValues(const Ogre::StringVector& paramValues)
	{
		this->values = paramValues;
		this->values.resize(this->names.size(), "");
		this->updateText();
	}
	//---------------------------------------------------------
	void ParamsPanel::setParamValue(const Ogre::DisplayString& paramName, const Ogre::DisplayString& paramValue)
	{
		for (unsigned int i = 0; i < this->names.size(); i++)
		{
			if (this->names[i] == paramName.asUTF8())
			{
				this->values[i] = paramValue.asUTF8();
				this->updateText();
				return;
			}
		}

		Ogre::String desc = "ParamsPanel \"" + getName() + "\" has no parameter \"" + paramName.asUTF8() + "\".";
		OGRE_EXCEPT(Ogre::Exception::ERR_ITEM_NOT_FOUND, desc, "ParamsPanel::setParamValue");
	}
	//---------------------------------------------------------
	void ParamsPanel::setParamValue(unsigned int index, const Ogre::DisplayString& paramValue)
	{
		if (index < 0 || index >= this->names.size())
		{
			Ogre::String desc = "ParamsPanel \"" + getName() + "\" has no parameter at position " +
				Ogre::StringConverter::toString(index) + ".";
			OGRE_EXCEPT(Ogre::Exception::ERR_ITEM_NOT_FOUND, desc, "ParamsPanel::setParamValue");
		}

		this->values[index] = paramValue.asUTF8();
		this->updateText();
	}
	//---------------------------------------------------------
	Ogre::DisplayString ParamsPanel::getParamValue(const Ogre::DisplayString& paramName)
	{
		for (unsigned int i = 0; i < this->names.size(); i++)
		{
			if (this->names[i] == paramName.asUTF8()) return this->values[i];
		}

		Ogre::String desc = "ParamsPanel \"" + getName() + "\" has no parameter \"" + paramName.asUTF8() + "\".";
		OGRE_EXCEPT(Ogre::Exception::ERR_ITEM_NOT_FOUND, desc, "ParamsPanel::getParamValue");
		return "";
	}
	//---------------------------------------------------------
	Ogre::DisplayString ParamsPanel::getParamValue(unsigned int index)
	{
		if (index < 0 || index >= this->names.size())
		{
			Ogre::String desc = "ParamsPanel \"" + this->getName() + "\" has no parameter at position " + Ogre::StringConverter::toString(index) + ".";
			OGRE_EXCEPT(Ogre::Exception::ERR_ITEM_NOT_FOUND, desc, "ParamsPanel::getParamValue");
		}

		return this->values[index];
	}
	//---------------------------------------------------------
	const Ogre::StringVector& ParamsPanel::getAllParamValues()
	{
		return this->values;
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------
	void ParamsPanel::updateText()
	{
		Ogre::DisplayString namesDS;
		Ogre::DisplayString valuesDS;

		for (unsigned int i = 0; i < this->names.size(); i++)
		{
			namesDS.append(this->names[i] + ":\n");
			valuesDS.append(this->values[i] + "\n");
		}

		this->namesArea->setCaption(namesDS);
		this->valuesArea->setCaption(valuesDS);
	}

	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	OABSTRACT_IMPL(ParamsPanel)
	OOBJECT_END
}