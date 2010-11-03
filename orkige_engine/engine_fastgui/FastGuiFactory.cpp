/********************************************************************
	created:	Wednesday 2010/10/27 at 13:09
	filename: 	FastGuiFactory.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec
*********************************************************************/

#include "engine_fastgui/FastGuiFactory.h"
#include "engine_fastgui/FastGuiManager.h"
#include "engine_util/StringUtil.h"
#include <core_util/PlatformUtil.h>
#include <core_util/foreach.h>
#include <boost/algorithm/string.hpp>
namespace Orkige
{
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	FastGuiFactory::FastGuiFactory()
	{
	}
	//---------------------------------------------------------
	FastGuiFactory::~FastGuiFactory()
	{
	}
	//---------------------------------------------------------
	woptr<FastGuiDecorWidget> FastGuiFactory::createDecorWidget(String const & id, String const & spriteName, Ogre::Vector2 const & position, Ogre::Vector2 const & size, String const & atlas, uint z)
	{
		optr<FastGuiDecorWidget> widget;

		if(FastGuiManager::getSingleton().widgetExists(id))
		{
			oAssertDesc(!FastGuiManager::getSingleton().widgetExists(id), "Widget with id: " << id << "already exists!");
			return widget;
		}
		widget = onew(new FastGuiDecorWidget(id, spriteName, position, size, atlas, z));
		FastGuiManager::getSingleton().addWidget(widget);
		return widget;
	}
	//---------------------------------------------------------
	woptr<FastGuiLabel> FastGuiFactory::createLabel(String const & id, uint defaultGlyphIndex, String const & text, Ogre::Vector2 const & position, String const & atlas, uint z)
	{
		optr<FastGuiLabel> widget;

		if(FastGuiManager::getSingleton().widgetExists(id))
		{
			oAssertDesc(!FastGuiManager::getSingleton().widgetExists(id), "Widget with id: " << id << "already exists!");
			return widget;
		}
		widget = onew(new FastGuiLabel(id, defaultGlyphIndex, text, position, atlas, z));
		FastGuiManager::getSingleton().addWidget(widget);
		return widget;
	}
	//---------------------------------------------------------
	woptr<FastGuiTextbox> FastGuiFactory::createTextbox(String const & id, uint defaultGlyphIndex, String const & text, Ogre::Vector2 const & position, String const & atlas, uint z)
	{
		optr<FastGuiTextbox> widget;

		if(FastGuiManager::getSingleton().widgetExists(id))
		{
			oAssertDesc(!FastGuiManager::getSingleton().widgetExists(id), "Widget with id: " << id << "already exists!");
			return widget;
		}
		widget = onew(new FastGuiTextbox(id, defaultGlyphIndex, text, position, atlas, z));
		FastGuiManager::getSingleton().addWidget(widget);
		return widget;
	}
	//---------------------------------------------------------
	woptr<FastGuiButton> FastGuiFactory::createButton(String const & id, String const & spriteName, uint defaultGlyphIndex, String const & text, Ogre::Vector2 const & position, FastGuiLabel::LabelAlignment textAlignment, Ogre::Vector2 const & size, String const & atlas, uint z)
	{
		optr<FastGuiButton> widget;

		if(FastGuiManager::getSingleton().widgetExists(id))
		{
			oAssertDesc(!FastGuiManager::getSingleton().widgetExists(id), "Widget with id: " << id << "already exists!");
			return widget;
		}
		widget = onew(new FastGuiButton(id, spriteName, defaultGlyphIndex, text, position, textAlignment, size, atlas, z));
		FastGuiManager::getSingleton().addWidget(widget);
		return widget;
	}
	//---------------------------------------------------------
	void FastGuiFactory::load(String const filename)
	{
		Ogre::ConfigFile::load(Orkige::PlatformUtil::getResourceDirectory() + "data/" + filename);
		FastGuiFactory::SectionIterator it = this->getSectionIterator();
		while(it.hasMoreElements())
		{
			String const & widgetType = it.peekNextKey();
			SettingsMultiMap* settings = it.peekNextValue();
			if(widgetType.empty())
			{
				this->onLoadGlobalSettings(settings);
			}
			else
			{
				this->onLoadWidget(widgetType, settings);
			}
			it.moveNext();
		}
		FastGuiManager::getSingleton().reorderViews();
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------
	void FastGuiFactory::onLoadGlobalSettings(SettingsMultiMap* settings)
	{
		//each global setting is consiederd a view/atlas name with its z order
		foreach(SettingsMultiMap::value_type const & vt, *settings)
		{
			String key = boost::to_lower_copy(vt.first);
			String value = boost::to_lower_copy(vt.second);
			optr<FastGuiView> view = FastGuiManager::getSingleton().getCreateView(key).lock();
			oAssert(view);
			uint z = StringUtil::Converter::fromString<uint>(value);
			view->setZ(z);
		}
	}
	//---------------------------------------------------------
	void FastGuiFactory::onLoadWidget(String const & widgetType, SettingsMultiMap* settings)
	{
		Ogre::vector<String>::type widgetSpecifier = Ogre::StringUtil::split(widgetType);
		oAssertDesc(widgetSpecifier.size() == 2, "Invalid Widget Specifier: " << widgetType);
		String widgetTypeName = widgetSpecifier[0];
		boost::to_lower(widgetTypeName);
		String widgetId = widgetSpecifier[1];
		oAssertDesc(!widgetId.empty(), "[" << widgetTypeName << "] Empty id is not allowed!")
		oAssertDesc(!FastGuiManager::getSingleton().widgetExists(widgetId), "Widget with given id already exists! id: " << widgetId);
		if(widgetTypeName == "decorwidget")
		{
			this->onLoadDecorWidget(widgetId, settings);
		}
		else if(widgetTypeName == "label")
		{
			this->onLoadLabel(widgetId, settings);
		}
		else if(widgetTypeName == "textbox")
		{
			this->onLoadTextbox(widgetId, settings);
		}
		else if(widgetTypeName == "button")
		{
			this->onLoadButton(widgetId, settings);
		}
	}
	//---------------------------------------------------------
	FastGuiFactory::BasicWidgetSettings FastGuiFactory::getBaseWidgetSettings(SettingsMultiMap* settings)
	{
		FastGuiFactory::BasicWidgetSettings baseSettings;
		baseSettings.atlas = StringUtil::BLANK;
		baseSettings.sprite = StringUtil::BLANK;
		baseSettings.defaultGlyphIndex = 9;
		baseSettings.position = Ogre::Vector2::ZERO;
		baseSettings.size = Ogre::Vector2::ZERO;
		baseSettings.alignment = FastGuiView::VA_TOPLEFT;
		baseSettings.z = 0;

		Ogre::Vector2 relSize = Ogre::Vector2::ZERO;
		Ogre::Vector2 relPosition = Ogre::Vector2::ZERO;
		foreach(SettingsMultiMap::value_type const & vt, *settings)
		{
			String key = boost::to_lower_copy(vt.first);
			String value = boost::to_lower_copy(vt.second);

			if(key == "atlas")
			{
				baseSettings.atlas = value;
			}
			else if(key == "sprite")
			{
				baseSettings.sprite = value;
			}
			else if(key == "text")
			{
				baseSettings.text = value;
			}
			else if(key == "font")
			{
				baseSettings.defaultGlyphIndex = StringUtil::Converter::fromString<uint>(value);
			}
			else if(key == "z")
			{
				baseSettings.z = StringUtil::Converter::fromString<uint>(value);
			}
			else if(key == "position")
			{
				if(value.find('%') != String::npos)
				{
					// Split on space
					Ogre::vector<String>::type vec = Ogre::StringUtil::split(value);
					Ogre::Real w = StringUtil::Converter::parseReal(vec[0]);
					Ogre::Real h = StringUtil::Converter::parseReal(vec[1]);
					relPosition.x = w;
					relPosition.y = h;
				}
				else
				{
					baseSettings.position = StringUtil::Converter::fromString<Ogre::Vector2>(value);
				}
			}
			else if(key == "size")
			{
				if(value.find('%') != String::npos)
				{
					// Split on space
					Ogre::vector<String>::type vec = Ogre::StringUtil::split(value);
					Ogre::Real w = StringUtil::Converter::parseReal(vec[0]);
					Ogre::Real h = StringUtil::Converter::parseReal(vec[1]);
					relSize.x = w;
					relSize.y = h;
				}
				else
				{
					baseSettings.size = StringUtil::Converter::fromString<Ogre::Vector2>(value);
				}
				
			}
			else if(key == "alignment")
			{
				baseSettings.alignment = FastGuiView::VA_TOPLEFT;
				if(value == "topleft")
				{
					baseSettings.alignment = FastGuiView::VA_TOPLEFT;
				}
				else if(value == "top")
				{
					baseSettings.alignment = FastGuiView::VA_TOP;
				}
				else if(value == "topright")
				{
					baseSettings.alignment = FastGuiView::VA_TOPRIGHT;
				}
				else if(value == "left")
				{
					baseSettings.alignment = FastGuiView::VA_LEFT;
				}
				else if(value == "center")
				{
					baseSettings.alignment = FastGuiView::VA_CENTER;
				}
				else if(value == "right")
				{
					baseSettings.alignment = FastGuiView::VA_RIGHT;
				}
				else if(value == "bottomleft")
				{
					baseSettings.alignment = FastGuiView::VA_BOTTOMLEFT;
				}
				else if(value == "bottom")
				{
					baseSettings.alignment = FastGuiView::VA_BOTTOM;
				}
				else if(value == "bottomright")
				{
					baseSettings.alignment = FastGuiView::VA_BOTTOMRIGHT;
				}
				else
				{
					oAssertDesc(!"Unknown Alignment", "Unknown Alignment: " << value);
				}
			}
		}
		optr<FastGuiView> view = FastGuiManager::getSingleton().getCreateView(baseSettings.atlas).lock();
		oAssert(view);
		if(relPosition != Ogre::Vector2::ZERO)
		{
			baseSettings.position.x = (relPosition.x * view->getScreen()->getWidth()) / 100.f;
			baseSettings.position.y = (relPosition.y * view->getScreen()->getHeight()) / 100.f;
		}
		baseSettings.position += view->getPosition(baseSettings.alignment);
		if(relSize != Ogre::Vector2::ZERO)
		{
			baseSettings.size.x = (relSize.x * view->getScreen()->getWidth()) / 100.f;
			baseSettings.size.y = (relSize.y * view->getScreen()->getHeight()) / 100.f;
		}
		return baseSettings;
	}
	//---------------------------------------------------------
	void FastGuiFactory::onLoadDecorWidget(String const & id, SettingsMultiMap* settings)
	{
		BasicWidgetSettings baseSettings = this->getBaseWidgetSettings(settings);

		this->createDecorWidget(id, baseSettings.sprite, baseSettings.position, baseSettings.size, baseSettings.atlas, baseSettings.z);
	}
	//---------------------------------------------------------
	void FastGuiFactory::onLoadLabel(String const & id, SettingsMultiMap* settings)
	{
		BasicWidgetSettings baseSettings = this->getBaseWidgetSettings(settings);

		this->createLabel(id, baseSettings.defaultGlyphIndex, baseSettings.text, baseSettings.position, baseSettings.atlas, baseSettings.z);
	}
	//---------------------------------------------------------
	void FastGuiFactory::onLoadTextbox(String const & id, SettingsMultiMap* settings)
	{
		BasicWidgetSettings baseSettings = this->getBaseWidgetSettings(settings);

		this->createTextbox(id, baseSettings.defaultGlyphIndex, baseSettings.text, baseSettings.position, baseSettings.atlas, baseSettings.z);
	}
	//---------------------------------------------------------
	void FastGuiFactory::onLoadButton(String const & id, SettingsMultiMap* settings)
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
			else if(key == "textcolor")
			{
				if(value == "black")
				{
					color = Ogre::ColourValue::Black;
				}
				else if(value == "white")
				{
					color = Ogre::ColourValue::White;
				}
				else if(value == "red")
				{
					color = Ogre::ColourValue::Red;
				}
				else if(value == "green")
				{
					color = Ogre::ColourValue::Green;
				}
				else if(value == "blue")
				{
					color = Ogre::ColourValue::Blue;
				}
				else
				{
					color = StringUtil::Converter::parseColourValue(value);
				}
			}
		}

		woptr<FastGuiButton> button = this->createButton(id, baseSettings.sprite, baseSettings.defaultGlyphIndex, baseSettings.text, baseSettings.position, alignment, baseSettings.size, baseSettings.atlas, baseSettings.z);
		oAssert(button.lock());
		button.lock()->getLabel().lock()->getCaption()->colour(color);
	}
	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
}