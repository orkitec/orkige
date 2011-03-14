/********************************************************************
	created:	2010/11/02
	filename: 	CcFastGuiFactory.cpp
	author:		philipp.engelhard
	notice:		
	copyright:	(c) 2010 kunst-stoff
*********************************************************************/

#include "CcFastGuiFactory.h"
#include <engine_fastgui/FastGuiManager.h>
#include <engine_graphic/Engine.h>

#include <core_util/PlatformUtil.h>
#include <core_util/foreach.h>
#include <boost/algorithm/string.hpp>
#include "engine_util/StringUtil.h"
#include "engine_base/Localisation.h"

using namespace Orkige;

namespace CC
{
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	CcFastGuiFactory::CcFastGuiFactory()
	{
		this->registerEvent(Orkige::Engine::FrameStartedEvent, &CcFastGuiFactory::onFrameStarted, this);
		this->registerEvent(Orkige::Engine::FrameStartedEvent, &CcFastGuiFactory::onFrameStarted, this);
		this->registerEvent(CC::CcFastGuiCoolDownButton::FreezDragDropEvent, &CcFastGuiFactory::onFreezDragDrop, this);
		this->registerEvent(CC::CcFastGuiCoolDownButton::UnfreezDragDropEvent, &CcFastGuiFactory::onUnfreezDragDrop, this);

		coolDownButtons.clear();
	}
	//---------------------------------------------------------
	CcFastGuiFactory::~CcFastGuiFactory()
	{

	}
//---------------------------------------------------------
	woptr<CcFastGuiCoolDownButton> CcFastGuiFactory::createCoolDownButton( String const & id, String const & spriteName, unsigned char defaultGlyphIndex, String const & text, Ogre::Vector2 const & position, FastGuiLabel::LabelAlignment textAlignment /*= FastGuiLabel::LA_CENTER*/, Ogre::Vector2 const & size /*= Ogre::Vector2::ZERO*/, String const & atlas /*= StringUtil::BLANK*/, unsigned char z /*= 0*/ )
	{
		optr<CcFastGuiCoolDownButton> widget;

		if(FastGuiManager::getSingleton().widgetExists(id))
		{
			oAssertDesc(!FastGuiManager::getSingleton().widgetExists(id), "Widget with id: " << id << " already exists!");
			return widget;
		}
		widget = onew(new CcFastGuiCoolDownButton(id, spriteName, defaultGlyphIndex, text, position, textAlignment, size, atlas, z));
		FastGuiManager::getSingleton().addWidget(widget);

		this->coolDownButtons.push_back(widget);
		return widget;
	}
//---------------------------------------------------------
	bool CcFastGuiFactory::onFrameStarted( Orkige::Event const & event )
	{
		optr<Orkige::FrameEventData> data = event.getDataPtr<Orkige::FrameEventData>();
		float timeSinceLastFrame = data->timeSinceLastFrame;

		std::vector<woptr<CcFastGuiCoolDownButton> >::iterator iter;
		std::vector<woptr<CcFastGuiCoolDownButton> >::iterator iterEnd = this->coolDownButtons.end();
		for (iter = this->coolDownButtons.begin();iter < iterEnd;++iter)
		{
			optr<CcFastGuiCoolDownButton> coolDownBTN = iter->lock();
			if(coolDownBTN)
			{
				if (coolDownBTN->isNeedUpdate())
				{
					coolDownBTN->update(timeSinceLastFrame);
				}
			}
			else
			{
				iter = this->coolDownButtons.erase(iter);
				iterEnd = this->coolDownButtons.end();
				if(iter == iterEnd) break;
			}

			
		}

		return false;
	}

//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------

	void CcFastGuiFactory::onLoadCoolDownButton( String const & id, SettingsMultiMap* settings )
	{
		BasicWidgetSettings baseSettings = this->getBaseWidgetSettings(settings);

		FastGuiLabel::LabelAlignment alignment = FastGuiLabel::LA_CENTER;
		Ogre::ColourValue color = Ogre::ColourValue::Black;

		foreach(SettingsMultiMap::value_type const & vt, *settings)
		{
			String key = boost::to_lower_copy(vt.first);
			String value = boost::to_lower_copy(vt.second);
			if(key == "textalignment")
			{
				if(value == "topleft")
				{
					alignment = FastGuiLabel::LA_TOPLEFT;
				}
				else if(value == "top")
				{
					alignment = FastGuiLabel::LA_TOP;
				}
				else if(value == "topright")
				{
					alignment = FastGuiLabel::LA_TOPRIGHT;
				}
				else if(value == "left")
				{
					alignment = FastGuiLabel::LA_LEFT;
				}
				else if(value == "center")
				{
					alignment = FastGuiLabel::LA_CENTER;
				}
				else if(value == "right")
				{
					alignment = FastGuiLabel::LA_RIGHT;
				}
				else if(value == "bottomleft")
				{
					alignment = FastGuiLabel::LA_BOTTOMLEFT;
				}
				else if(value == "bottom")
				{
					alignment = FastGuiLabel::LA_BOTTOM;
				}
				else if(value == "bottomright")
				{
					alignment = FastGuiLabel::LA_BOTTOMRIGHT;
				}
				else
				{
					oAssertDesc(!"Unknown Alignment", "Unknown Alignment: " << value);
				}
			}
		}

		woptr<CcFastGuiCoolDownButton> button = this->createCoolDownButton(id, baseSettings.sprite, baseSettings.defaultGlyphIndex, baseSettings.text, baseSettings.position, alignment, baseSettings.size, baseSettings.atlas, baseSettings.z);
		oAssert(button.lock());
	}

	void CcFastGuiFactory::onLoadWidget( String const & widgetType, SettingsMultiMap* settings )
	{
		super::onLoadWidget(widgetType,settings);

		Ogre::vector<String>::type widgetSpecifier = Ogre::StringUtil::split(widgetType);
		oAssertDesc(widgetSpecifier.size() == 2, "Invalid Widget Specifier: " << widgetType);
		String widgetTypeName = widgetSpecifier[0];
		boost::to_lower(widgetTypeName);
		String widgetId = widgetSpecifier[1];
		oAssertDesc(!widgetId.empty(), "[" << widgetTypeName << "] Empty id is not allowed!")
	//	oAssertDesc(!FastGuiManager::getSingleton().widgetExists(widgetId), "Widget with given id already exists! id: " << widgetId);
		
		if(widgetTypeName == "cooldownbutton")
		{
			this->onLoadCoolDownButton(widgetId, settings);
		}
	}
//---------------------------------------------------------
	bool CcFastGuiFactory::onFreezDragDrop( Orkige::Event const & event )
	{
		std::vector<woptr<CcFastGuiCoolDownButton> >::iterator iter;
		std::vector<woptr<CcFastGuiCoolDownButton> >::iterator iterEnd = this->coolDownButtons.end();
		for (iter = this->coolDownButtons.begin();iter < iterEnd;++iter)
		{
			optr<CcFastGuiCoolDownButton> coolDownBTN = iter->lock();
			if(coolDownBTN)
			{
				coolDownBTN->isFreezed = true;
			}
			else
			{
				iter = this->coolDownButtons.erase(iter);
				iterEnd = this->coolDownButtons.end();
				if(iter == iterEnd) break;
			}


		}
		return false;
	}
//---------------------------------------------------------
	bool CcFastGuiFactory::onUnfreezDragDrop( Orkige::Event const & event )
	{
		std::vector<woptr<CcFastGuiCoolDownButton> >::iterator iter;
		std::vector<woptr<CcFastGuiCoolDownButton> >::iterator iterEnd = this->coolDownButtons.end();
		for (iter = this->coolDownButtons.begin();iter < iterEnd;++iter)
		{
			optr<CcFastGuiCoolDownButton> coolDownBTN = iter->lock();
			if(coolDownBTN)
			{
				coolDownBTN->isFreezed = false;
			}
			else
			{
				iter = this->coolDownButtons.erase(iter);
				iterEnd = this->coolDownButtons.end();
				if(iter == iterEnd) break;
			}


		}
		return false;

	}
	


	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
}