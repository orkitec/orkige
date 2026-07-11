/********************************************************************
	created:	Wednesday 2010/10/27 at 13:09
	filename: 	GuiFactory.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
*********************************************************************/

#include "engine_gui/GuiFactory.h"
#include "engine_gui/GuiManager.h"
#include "engine_gui/UiAtlas.h"
#include "engine_gui/GuiLayout.h"
#include "engine_util/StringUtil.h"
#include <core_util/PlatformUtil.h>
#include <core_util/StringTable.h>
#include <core_util/foreach.h>
// boost string algorithms replaced by core_util/StringUtil.h + Ogre::StringUtil (no-boost rule)
#include "engine_util/ConfigFileUtil.h"

#include <OgreResourceGroupManager.h>
#include <OgreDataStream.h>
#ifdef ORKIGE_RENDER_CLASSIC
// the Localisation service is still classic-only (Ogre::ConfigFile
// internals + a scene-manager entity probe, no live cross-backend user);
// layout files with localized text keys show the raw key on other
// flavors until Localisation gets its own port
#include "engine_base/Localisation.h"
#endif


namespace Orkige
{
	namespace
	{
		//! @brief scale an authored pixel SIZE to the display density so a
		//! touch target authored at e.g. 160x40 design pixels keeps a stable
		//! physical size on a 2x-3x screen. Driven by the global UiGlyph::scale
		//! (integer-snapped from the content scale at gui boot); a ZERO size
		//! ("use the sprite's own size") passes through untouched. Positions are
		//! left in physical pixels - they already come from getWindowWidth, and
		//! centerHorizontal re-derives centering from the scaled size.
		Ogre::Vector2 scaleAuthoredSize(Ogre::Vector2 const & size)
		{
			if(size == Ogre::Vector2::ZERO)
			{
				return size;
			}
			const float uiScale = UiGlyph::scale.x >= 1.0f
				? UiGlyph::scale.x : 1.0f;
			return Ogre::Vector2(size.x * uiScale, size.y * uiScale);
		}

		//--- .oui declarative-layout helpers (backend-neutral) ---
		//! a section value, or a default if the key is absent
		String ouiValue(GuiLayoutSection const & section, String const & key,
			String const & fallback = StringUtil::BLANK)
		{
			String const * v = section.find(key);
			return v != NULL ? *v : fallback;
		}
		//! parse "x y" into a Vector2 (missing components stay 0)
		Ogre::Vector2 parseVec2(String const & value)
		{
			Ogre::Vector2 out(0.0f, 0.0f);
			std::istringstream stream(value);
			stream >> out.x >> out.y;
			return out;
		}
		bool parseBool(String const & value, bool fallback)
		{
			return Ogre::StringConverter::parseBool(value, fallback);
		}
		//! resolve authored text: a leading '@' looks the rest up in the
		//! StringTable (backend-neutral localisation), else it is literal; "\n"
		//! escapes become real newlines either way
		String resolveText(String const & value)
		{
			String text = value;
			if(!text.empty() && text[0] == '@')
			{
				const String key = text.substr(1);
				if(StringTable::getSingletonPtr() != NULL)
				{
					text = StringTable::getSingleton().get(key);
				}
				else
				{
					text = key;
				}
			}
			return Ogre::StringUtil::replaceAll(text, "\\n", "\n");
		}
		GuiLabel::LabelAlignment parseLabelAlignment(String const & value,
			GuiLabel::LabelAlignment fallback)
		{
			String v = value;
			Ogre::StringUtil::toLowerCase(v);
			if(v == "topleft")		return GuiLabel::LA_TOPLEFT;
			if(v == "top")			return GuiLabel::LA_TOP;
			if(v == "topright")		return GuiLabel::LA_TOPRIGHT;
			if(v == "left")			return GuiLabel::LA_LEFT;
			if(v == "center" || v == "centre")	return GuiLabel::LA_CENTER;
			if(v == "right")		return GuiLabel::LA_RIGHT;
			if(v == "bottomleft")	return GuiLabel::LA_BOTTOMLEFT;
			if(v == "bottom")		return GuiLabel::LA_BOTTOM;
			if(v == "bottomright")	return GuiLabel::LA_BOTTOMRIGHT;
			return fallback;
		}
		//! apply the shared rect-anchor / group / content-fit / draw-mode keys
		//! to an already-created widget (the .oui pass-2 layout application)
		void applyLayoutKeys(GuiWidget* widget, GuiLayoutSection const & s)
		{
			if(String const * v = s.find("parent"))
			{
				if(GuiManager::getSingleton().widgetExists(*v))
				{
					optr<GuiWidget> parent =
						GuiManager::getSingleton().getWidget(*v).lock();
					if(parent)
					{
						widget->setParent(parent);
					}
				}
			}
			if(String const * v = s.find("anchor"))
			{
				widget->setAnchorPreset(*v);
			}
			if(String const * v = s.find("anchorMin"))
			{
				const Ogre::Vector2 mn = parseVec2(*v);
				const Ogre::Vector2 mx = parseVec2(ouiValue(s, "anchorMax", *v));
				widget->setAnchors(mn.x, mn.y, mx.x, mx.y);
			}
			if(String const * v = s.find("pivot"))
			{
				const Ogre::Vector2 p = parseVec2(*v);
				widget->setPivot(p.x, p.y);
			}
			if(String const * v = s.find("offsets"))
			{
				std::istringstream stream(*v);
				float l = 0, t = 0, r = 0, b = 0;
				stream >> l >> t >> r >> b;
				widget->setOffsets(l, t, r, b);
			}
			if(String const * v = s.find("anchoredPos"))
			{
				const Ogre::Vector2 p = parseVec2(*v);
				widget->setAnchoredPosition(p.x, p.y);
			}
			if(String const * v = s.find("sizeDelta"))
			{
				const Ogre::Vector2 sz = parseVec2(*v);
				widget->setSizeDelta(sz.x, sz.y);
			}
			if(String const * v = s.find("useSafeArea"))
			{
				widget->setUseSafeArea(parseBool(*v, false));
			}
			if(String const * v = s.find("group"))
			{
				widget->setLayoutGroup(*v);
			}
			if(String const * v = s.find("padding"))
			{
				std::istringstream stream(*v);
				float l = 0, t = 0, r = 0, b = 0;
				stream >> l >> t >> r >> b;
				widget->setGroupPadding(l, t, r, b);
			}
			if(String const * v = s.find("spacing"))
			{
				std::istringstream stream(*v);
				float sp = 0, spy = 0;
				stream >> sp >> spy;
				widget->setGroupSpacing(sp, spy);
			}
			if(String const * v = s.find("childAlign"))
			{
				widget->setChildAlignment(*v);
			}
			if(String const * v = s.find("childExpand"))
			{
				widget->setChildForceExpand(parseBool(*v, false));
			}
			if(String const * v = s.find("cellSize"))
			{
				const Ogre::Vector2 sz = parseVec2(*v);
				widget->setGridCellSize(sz.x, sz.y);
			}
			if(String const * v = s.find("gridConstraint"))
			{
				std::istringstream stream(*v);
				String constraint;
				int count = 0;
				stream >> constraint >> count;
				widget->setGridConstraint(constraint, count);
			}
			if(String const * v = s.find("fit"))
			{
				std::istringstream stream(*v);
				String h = "none", vert = "none";
				stream >> h >> vert;
				widget->setContentSizeFit(h, vert);
			}
			// draw modes live on the decor widget (panels); a button/other widget
			// keeps its default stretched fill
			if(GuiDecorWidget* decor = dynamic_cast<GuiDecorWidget*>(widget))
			{
				if(String const * v = s.find("nineSlice"))
				{
					decor->setNineSlice(parseBool(*v, false));
				}
				if(String const * v = s.find("tiled"))
				{
					decor->setTiled(parseBool(*v, false));
				}
				if(String const * v = s.find("color"))
				{
					std::istringstream stream(*v);
					float r = 1, g = 1, b = 1, a = 1;
					stream >> r >> g >> b >> a;
					decor->setColour(r, g, b, a);
				}
			}
			if(GuiScrollView* scroll = dynamic_cast<GuiScrollView*>(widget))
			{
				if(String const * v = s.find("scroll"))
				{
					scroll->setScroll(Ogre::StringConverter::parseReal(*v));
				}
			}
		}
	}
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	GuiFactory::GuiFactory() : resourceGroup(Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME)
	{
	}
	//---------------------------------------------------------
	GuiFactory::~GuiFactory()
	{
	}
	//---------------------------------------------------------
	woptr<GuiDecorWidget> GuiFactory::createDecorWidget(String const & id, String const & spriteName, Ogre::Vector2 const & position, Ogre::Vector2 const & size, String const & atlas, uint z)
	{
		optr<GuiDecorWidget> widget;

		if(GuiManager::getSingleton().widgetExists(id))
		{
			oAssertDesc(!GuiManager::getSingleton().widgetExists(id), "Widget with id: " << id << "already exists!");
			return widget;
		}
		widget = onew(new GuiDecorWidget(id, spriteName, position, size, atlas, z));
		GuiManager::getSingleton().addWidget(widget);
		return widget;
	}
	//---------------------------------------------------------
	woptr<GuiLabel> GuiFactory::createLabel(String const & id, uint defaultGlyphIndex, String const & text, Ogre::Vector2 const & position, String const & atlas, uint z, bool scaled)
	{
		optr<GuiLabel> widget;

		if(GuiManager::getSingleton().widgetExists(id))
		{
			oAssertDesc(!GuiManager::getSingleton().widgetExists(id), "Widget with id: " << id << "already exists!");
			return widget;
		}
		widget = onew(new GuiLabel(id, defaultGlyphIndex, text, position, atlas, z, scaled));
		GuiManager::getSingleton().addWidget(widget);
		return widget;
	}
	//---------------------------------------------------------
	woptr<GuiTextbox> GuiFactory::createTextbox(String const & id, uint defaultGlyphIndex, String const & text, Ogre::Vector2 const & position, String const & atlas, uint z, bool scaled)
	{
		optr<GuiTextbox> widget;

		if(GuiManager::getSingleton().widgetExists(id))
		{
			oAssertDesc(!GuiManager::getSingleton().widgetExists(id), "Widget with id: " << id << "already exists!");
			return widget;
		}
		widget = onew(new GuiTextbox(id, defaultGlyphIndex, text, position, atlas, z, scaled));
		GuiManager::getSingleton().addWidget(widget);
		return widget;
	}
	//---------------------------------------------------------
	woptr<GuiButton> GuiFactory::createButton(String const & id, String const & spriteName, uint defaultGlyphIndex, String const & text, Ogre::Vector2 const & position, GuiLabel::LabelAlignment textAlignment, Ogre::Vector2 const & size, String const & atlas, uint z, bool _nostate, GuiButtonBlink::ButtonBlinkState blinkState)
	{
		optr<GuiButton> widget;

		if(GuiManager::getSingleton().widgetExists(id))
		{
			oAssertDesc(!GuiManager::getSingleton().widgetExists(id), "Widget with id: " << id << "already exists!");
			return widget;
		}
		const Ogre::Vector2 scaledSize = scaleAuthoredSize(size);
		if (blinkState != GuiButtonBlink::BBLINK_NONE)
			widget = onew(new GuiButtonBlink(id, spriteName, defaultGlyphIndex, text, position, textAlignment, scaledSize, atlas, z, _nostate, blinkState));
		else
			widget = onew(new GuiButton(id, spriteName, defaultGlyphIndex, text, position, textAlignment, scaledSize, atlas, z, _nostate));
		GuiManager::getSingleton().addWidget(widget);
		return widget;
	}
	//---------------------------------------------------------
	woptr<GuiCheckBox> GuiFactory::createCheckBox( String const & id, String const & spriteName, uint defaultGlyphIndex, String const & text, Ogre::Vector2 const & position, GuiLabel::LabelAlignment textAlignment /*= GuiLabel::LA_CENTER*/, Ogre::Vector2 const & size /*= Ogre::Vector2::ZERO*/, String const & atlas /*= StringUtil::BLANK*/, uint z /*= 0*/, bool useCheckbox /* = false */)
	{
 		optr<GuiCheckBox> widget;
 
 		if(GuiManager::getSingleton().widgetExists(id))
 		{
 			oAssertDesc(!GuiManager::getSingleton().widgetExists(id), "Widget with id: " << id << "already exists!");
 			return widget;
 		}
 		widget = onew(new GuiCheckBox(id, spriteName, defaultGlyphIndex, text, position, textAlignment, scaleAuthoredSize(size), atlas, z, useCheckbox));
 		GuiManager::getSingleton().addWidget(widget);
 		return widget;
 	}
	//---------------------------------------------------------
	woptr<GuiSelectMenu> GuiFactory::createSelectMenu( String const & id,String const & buttonId, String const & spriteName, uint defaultGlyphIndex, String const & text, Ogre::Vector2 const & position, GuiLabel::LabelAlignment textAlignment /*= GuiLabel::LA_CENTER*/, Ogre::Vector2 const & size /*= Ogre::Vector2::ZERO*/, String const & atlas /*= StringUtil::BLANK*/, uint z /*= 0*/ )
	{
		optr<GuiSelectMenu> widget;

		if(GuiManager::getSingleton().widgetExists(id))
		{
			oAssertDesc(!GuiManager::getSingleton().widgetExists(id), "Widget with id: " << id << "already exists!");
			return widget;
		}
		widget = onew(new GuiSelectMenu(id,buttonId, spriteName, defaultGlyphIndex, text, position, textAlignment, scaleAuthoredSize(size), atlas, z));
		GuiManager::getSingleton().addWidget(widget);
		return widget;
	}
	//---------------------------------------------------------
	woptr<GuiDragDropButton> GuiFactory::createDragDropButton(String const & id, String const & spriteName, unsigned char defaultGlyphIndex, String const & text, Ogre::Vector2 const & position, GuiLabel::LabelAlignment textAlignment, Ogre::Vector2 const & size, String const & atlas, unsigned char z)
	{
		optr<GuiDragDropButton> widget;

		if(GuiManager::getSingleton().widgetExists(id))
		{
			oAssertDesc(!GuiManager::getSingleton().widgetExists(id), "Widget with id: " << id << " already exists!");
			return widget;
		}
		widget = onew(new GuiDragDropButton(id, spriteName, defaultGlyphIndex, text, position, textAlignment, scaleAuthoredSize(size), atlas, z));
		GuiManager::getSingleton().addWidget(widget);
		return widget;
	}
	//---------------------------------------------------------
	woptr<GuiProgressBar> GuiFactory::createProgressBar( String const & id, String const & spriteName, uint defaultGlyphIndex, String const & text, Ogre::Vector2 const & position, GuiLabel::LabelAlignment textAlignment /*= GuiLabel::LA_CENTER*/, Ogre::Vector2 const & size /*= Ogre::Vector2::ZERO*/, String const & atlas /*= StringUtil::BLANK*/, uint z /*= 0*/ )
	{
		optr<GuiProgressBar> widget;

		if(GuiManager::getSingleton().widgetExists(id))
		{
			oAssertDesc(!GuiManager::getSingleton().widgetExists(id), "Widget with id: " << id << "already exists!");
			return widget;
		}
		widget = onew(new GuiProgressBar(id, spriteName, defaultGlyphIndex, text, position, textAlignment, scaleAuthoredSize(size), atlas, z));
		GuiManager::getSingleton().addWidget(widget);
		return widget;
	}
	//---------------------------------------------------------
	woptr<GuiSlider> GuiFactory::createSlider( String const & id,String const & buttonId, String const & spriteName, uint defaultGlyphIndex, String const & text, Ogre::Vector2 const & position, GuiLabel::LabelAlignment textAlignment /*= GuiLabel::LA_CENTER*/, Ogre::Vector2 const & size /*= Ogre::Vector2::ZERO*/, String const & atlas /*= StringUtil::BLANK*/, uint z /*= 0*/ )
	{
		optr<GuiSlider> widget;

		if(GuiManager::getSingleton().widgetExists(id))
		{
			oAssertDesc(!GuiManager::getSingleton().widgetExists(id), "Widget with id: " << id << "already exists!");
			return widget;
		}
		widget = onew(new GuiSlider(id,buttonId, spriteName, defaultGlyphIndex, text, position, textAlignment, scaleAuthoredSize(size), atlas, z));
		GuiManager::getSingleton().addWidget(widget);
		return widget;
	}
	//---------------------------------------------------------
	woptr<GuiTextEntry> GuiFactory::createTextEntry(String const & id, String const & spriteName, uint defaultGlyphIndex, String const & placeholder, Ogre::Vector2 const & position, Ogre::Vector2 const & size /*= Ogre::Vector2::ZERO*/, String const & atlas /*= StringUtil::BLANK*/, uint z /*= 0*/, uint maxLength /*= 0*/)
	{
		optr<GuiTextEntry> widget;

		if(GuiManager::getSingleton().widgetExists(id))
		{
			oAssertDesc(!GuiManager::getSingleton().widgetExists(id), "Widget with id: " << id << "already exists!");
			return widget;
		}
		widget = onew(new GuiTextEntry(id, spriteName, defaultGlyphIndex, placeholder, position, scaleAuthoredSize(size), atlas, z, maxLength));
		GuiManager::getSingleton().addWidget(widget);
		return widget;
	}
	//---------------------------------------------------------
	woptr<GuiScrollView> GuiFactory::createScrollView(String const & id, Ogre::Vector2 const & position, Ogre::Vector2 const & size, String const & atlas, uint z)
	{
		optr<GuiScrollView> widget;

		if(GuiManager::getSingleton().widgetExists(id))
		{
			oAssertDesc(!GuiManager::getSingleton().widgetExists(id), "Widget with id: " << id << "already exists!");
			return widget;
		}
		widget = onew(new GuiScrollView(id, position, scaleAuthoredSize(size), atlas, z));
		GuiManager::getSingleton().addWidget(widget);
		return widget;
	}
	//---------------------------------------------------------
	void GuiFactory::loadLayout(String const & filename)
	{
		// read the .oui text through the resource system (cross-backend; the
		// classic load() went through Ogre::ConfigFile, this does not)
		String group = Ogre::ResourceGroupManager::getSingleton()
			.findGroupContainingResource(filename);
		Ogre::DataStreamPtr stream = Ogre::ResourceGroupManager::getSingleton()
			.openResource(filename, group);
		const String text = stream->getAsString();
		this->resourceGroup = group;

		GuiLayoutDoc doc;
		String error;
		if(!GuiLayoutDoc::parse(text, doc, error))
		{
			oAssertDesc(!"invalid .oui", "loadLayout(" << filename << "): "
				<< error);
			return;
		}

		// the optional global [Layout] section: default atlas + design/root policy
		GuiManager & manager = GuiManager::getSingleton();
		String defaultAtlas = StringUtil::BLANK;
		if(GuiLayoutSection const * layout = doc.findSection("Layout"))
		{
			defaultAtlas = ouiValue(*layout, "atlas");
			if(String const * design = layout->find("design"))
			{
				std::istringstream ds(*design);
				float dw = 0, dh = 0, match = 0;
				ds >> dw >> dh >> match;
				manager.setDesignResolution(dw, dh, match);
			}
			if(String const * root = layout->find("root"))
			{
				manager.setRootSpace(*root);
			}
		}

		// pass 1: create every widget (so a parent exists before pass 2 links it)
		for(GuiLayoutSection const & s : doc.sections)
		{
			String type = s.type;
			Ogre::StringUtil::toLowerCase(type);
			if(type == "layout" || s.id.empty())
			{
				continue;	// the global section / an id-less section is not a widget
			}
			const String atlas = ouiValue(s, "atlas", defaultAtlas);
			const uint z = Ogre::StringConverter::parseUnsignedInt(
				ouiValue(s, "z", "0"), 0);
			const String sprite = ouiValue(s, "sprite");
			const uint font = Ogre::StringConverter::parseUnsignedInt(
				ouiValue(s, "font", "9"), 9);
			const String text = resolveText(ouiValue(s, "text"));
			const Ogre::Vector2 position = parseVec2(ouiValue(s, "position", "0 0"));
			const Ogre::Vector2 size = parseVec2(ouiValue(s, "size", "0 0"));
			const GuiLabel::LabelAlignment textAlign = parseLabelAlignment(
				ouiValue(s, "textAlignment"), GuiLabel::LA_CENTER);

			if(type == "label")
			{
				const bool scaled = parseBool(ouiValue(s, "scaled", "true"), true);
				woptr<GuiLabel> label = this->createLabel(s.id, font, text,
					position, atlas, z, scaled);
				if(label.lock())
				{
					label.lock()->setAlignment(textAlign);
					if(String const * c = s.find("textColor"))
					{
						label.lock()->getCaption()->colour(
							StringUtil::Converter::parseColourValue(*c));
					}
				}
			}
			else if(type == "textbox")
			{
				this->createTextbox(s.id, font, text, position, atlas, z, true);
			}
			else if(type == "button")
			{
				this->createButton(s.id, sprite, font, text, position, textAlign,
					size, atlas, z, false, GuiButtonBlink::BBLINK_NONE);
			}
			else if(type == "checkbox")
			{
				const bool useCheckbox = parseBool(ouiValue(s, "checkbox", "false"), false);
				this->createCheckBox(s.id, sprite, font, text, position, textAlign,
					size, atlas, z, useCheckbox);
			}
			else if(type == "selectmenu")
			{
				this->createSelectMenu(s.id, "select_menu_button", sprite, font,
					text, position, textAlign, size, atlas, z);
			}
			else if(type == "slider")
			{
				this->createSlider(s.id, "slider_menu_button", sprite, font, text,
					position, textAlign, size, atlas, z);
			}
			else if(type == "progressbar")
			{
				this->createProgressBar(s.id, sprite, font, text, position,
					textAlign, size, atlas, z);
			}
			else if(type == "textentry")
			{
				const uint maxLength = Ogre::StringConverter::parseUnsignedInt(
					ouiValue(s, "maxLength", "0"), 0);
				this->createTextEntry(s.id, sprite, font, text, position, size,
					atlas, z, maxLength);
			}
			else if(type == "decorwidget" || type == "panel")
			{
				this->createDecorWidget(s.id, sprite, position, size, atlas, z);
			}
			else if(type == "scrollview")
			{
				this->createScrollView(s.id, position, size, atlas, z);
			}
			else
			{
				oAssertDesc(!"unknown .oui widget type", "loadLayout: unknown "
					"widget type '" << s.type << "' for id '" << s.id << "'");
			}
		}

		// pass 2: apply the rect-anchor / group / content-fit / draw-mode keys
		// (parenting now resolves - every widget exists)
		for(GuiLayoutSection const & s : doc.sections)
		{
			String type = s.type;
			Ogre::StringUtil::toLowerCase(type);
			if(type == "layout" || s.id.empty())
			{
				continue;
			}
			if(!manager.widgetExists(s.id))
			{
				continue;
			}
			optr<GuiWidget> widget = manager.getWidget(s.id).lock();
			if(widget)
			{
				applyLayoutKeys(widget.get(), s);
			}
		}

		manager.reorderViews();
	}
	//---------------------------------------------------------
	void GuiFactory::load(String const & filename)
	{
		Ogre::ConfigFile::load(filename, Ogre::ResourceGroupManager::getSingleton().findGroupContainingResource(filename), "\t:=", true);
		this->resourceGroup = this->getSetting("ResourceGroup", StringUtil::BLANK, Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);
		// section iteration through the per-flavor seam (classic 14 and
		// Ogre-Next drifted apart here, see engine_util/ConfigFileUtil.h);
		// the per-section copy keeps the historical mutable-pointer
		// handler signatures untouched
		for (auto const & section : Orkige::ConfigFileUtil::getSections(*this))
		{
			String const & widgetType = section.first;
			SettingsMultiMap sectionSettings = section.second;
			if(widgetType.empty())
			{
				this->onLoadGlobalSettings(&sectionSettings);
			}
			else
			{
				this->onLoadWidget(widgetType, &sectionSettings);
			}
		}
		GuiManager::getSingleton().reorderViews();
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------
	void GuiFactory::onLoadGlobalSettings(SettingsMultiMap* settings)
	{
		//each global setting is consiederd a view/atlas name with its z order
		foreach(SettingsMultiMap::value_type const & vt, *settings)
		{
			
			String key = StringUtil::to_lower_copy(vt.first);
			if(key != "resourcegroup")
			{
				String value = StringUtil::to_lower_copy(vt.second);
				optr<GuiView> view = GuiManager::getSingleton().getCreateView(vt.first, this->resourceGroup).lock();
				if(!view)
				{
					view = GuiManager::getSingleton().getCreateView(key, this->resourceGroup).lock();
				}
				oAssert(view);
				uint z = StringUtil::Converter::fromString<uint>(value);
				view->setZ(z);
			}

		}
	}
	//---------------------------------------------------------
	void GuiFactory::onLoadWidget(String const & widgetType, SettingsMultiMap* settings)
	{
		Ogre::StringVector widgetSpecifier = Ogre::StringUtil::split(widgetType);
		oAssertDesc(widgetSpecifier.size() == 2, "Invalid Widget Specifier: " << widgetType);
		String widgetTypeName = widgetSpecifier[0];
		StringUtil::to_lower(widgetTypeName);
		String widgetId = widgetSpecifier[1];
		oAssertDesc(!widgetId.empty(), "[" << widgetTypeName << "] Empty id is not allowed!")
		oAssertDesc(!GuiManager::getSingleton().widgetExists(widgetId), "Widget with given id already exists! id: " << widgetId);
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
		else if(widgetTypeName == "dragdropbutton")
		{
			this->onLoadDragDropButton(widgetId, settings);
		}
		else if(widgetTypeName == "checkbox")
		{
			this->onLoadCheckBox(widgetId, settings);
		}
		else if(widgetTypeName == "selectmenu")
		{
			this->onLoadSelectMenu(widgetId, settings);
		}
		else if(widgetTypeName == "progressbar")
		{
			this->onLoadProgressBar(widgetId, settings);
		}
		else if(widgetTypeName == "slider")
		{
			this->onLoadSlider(widgetId, settings);
		}
		else if(widgetTypeName == "textentry")
		{
			this->onLoadTextEntry(widgetId, settings);
		}
	}
	//---------------------------------------------------------
	GuiFactory::BasicWidgetSettings GuiFactory::getBaseWidgetSettings(SettingsMultiMap* settings)
	{
		GuiFactory::BasicWidgetSettings baseSettings;
		baseSettings.atlas = StringUtil::BLANK;
		baseSettings.sprite = StringUtil::BLANK;
		baseSettings.defaultGlyphIndex = 9;
		baseSettings.position = Ogre::Vector2::ZERO;
		baseSettings.size = Ogre::Vector2::ZERO;
		baseSettings.alignment = GuiView::VA_TOPLEFT;
		baseSettings.z = 0;

		Ogre::Vector2 relSize = Ogre::Vector2::ZERO;
		Ogre::Vector2 relPosition = Ogre::Vector2::ZERO;
		Ogre::Vector2 baseSize = Ogre::Vector2::ZERO;
		bool snapSubpixel = false;
		foreach(SettingsMultiMap::value_type const & vt, *settings)
		{
			String key = StringUtil::to_lower_copy(vt.first);
			String value = StringUtil::to_lower_copy(vt.second);

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
#ifdef ORKIGE_RENDER_CLASSIC
				baseSettings.text = Localisation::getSingleton().getLocalized(vt.second);
#else
				baseSettings.text = vt.second;	// Localisation is classic-only (see include note)
#endif
				baseSettings.text = Ogre::StringUtil::replaceAll(baseSettings.text, "\\n", "\n");
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
					Ogre::StringVector vec = Ogre::StringUtil::split(value);
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
					Ogre::StringVector vec = Ogre::StringUtil::split(value);
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
			else if(key == "basesize")
			{
				baseSize = StringUtil::Converter::fromString<Ogre::Vector2>(value);
			}
			else if(key == "alignment")
			{
				baseSettings.alignment = GuiView::VA_TOPLEFT;
				if(value == "topleft")
				{
					baseSettings.alignment = GuiView::VA_TOPLEFT;
				}
				else if(value == "top")
				{
					baseSettings.alignment = GuiView::VA_TOP;
				}
				else if(value == "topright")
				{
					baseSettings.alignment = GuiView::VA_TOPRIGHT;
				}
				else if(value == "left")
				{
					baseSettings.alignment = GuiView::VA_LEFT;
				}
				else if(value == "center")
				{
					baseSettings.alignment = GuiView::VA_CENTER;
				}
				else if(value == "right")
				{
					baseSettings.alignment = GuiView::VA_RIGHT;
				}
				else if(value == "bottomleft")
				{
					baseSettings.alignment = GuiView::VA_BOTTOMLEFT;
				}
				else if(value == "bottom")
				{
					baseSettings.alignment = GuiView::VA_BOTTOM;
				}
				else if(value == "bottomright")
				{
					baseSettings.alignment = GuiView::VA_BOTTOMRIGHT;
				}
				else
				{
					oAssertDesc(!"Unknown Alignment", "Unknown Alignment: " << value);
				}
			}
			else if(key == "snap")
			{
				snapSubpixel = Ogre::StringConverter::parseBool(value, snapSubpixel);
			}
		}
		optr<GuiView> view = GuiManager::getSingleton().getCreateView(baseSettings.atlas).lock();
		oAssert(view);
		if(baseSize != Ogre::Vector2::ZERO)
		{
			if(relPosition == Ogre::Vector2::ZERO && baseSettings.position != Ogre::Vector2::ZERO)
			{
				relPosition.x = (baseSettings.position.x / baseSize.x) * 100.f;
				relPosition.y = (baseSettings.position.y / baseSize.y) * 100.f;
			}
			if(relSize == Ogre::Vector2::ZERO && baseSettings.size != Ogre::Vector2::ZERO)
			{
				relSize.x = (baseSettings.size.x / baseSize.x) * 100.f;
				relSize.y = (baseSettings.size.y / baseSize.y) * 100.f;
			}
		}
		if(relPosition != Ogre::Vector2::ZERO)
		{
			baseSettings.position.x = (relPosition.x * view->getScreen()->getWidth()) / 100.f;
			baseSettings.position.y = (relPosition.y * view->getScreen()->getHeight()) / 100.f;
			if (snapSubpixel)
			{
				// limit position to half integer values for better visible result
				baseSettings.position.x = static_cast<int>(baseSettings.position.x + 1e-4f) + 0.5f;
				baseSettings.position.y = static_cast<int>(baseSettings.position.y + 1e-4f) + 0.5f;
			}
		}
		baseSettings.position += view->getPosition(baseSettings.alignment);
		if(relSize != Ogre::Vector2::ZERO)
		{
			baseSettings.size.x = (relSize.x * view->getScreen()->getWidth()) / 100.f;
			baseSettings.size.y = (relSize.y * view->getScreen()->getHeight()) / 100.f;
			if (snapSubpixel)
			{
				// limit size to full integer values for better visible result
				baseSettings.size.x = static_cast<int>(baseSettings.size.x + 1e-4f);
				baseSettings.size.y = static_cast<int>(baseSettings.size.y + 1e-4f);
			}
		}
		return baseSettings;
	}
	//---------------------------------------------------------
	void GuiFactory::onLoadDecorWidget(String const & id, SettingsMultiMap* settings)
	{
		BasicWidgetSettings baseSettings = this->getBaseWidgetSettings(settings);

		this->createDecorWidget(id, baseSettings.sprite, baseSettings.position, baseSettings.size, baseSettings.atlas, baseSettings.z);
	}
	//---------------------------------------------------------
	void GuiFactory::onLoadLabel(String const & id, SettingsMultiMap* settings)
	{
		BasicWidgetSettings baseSettings = this->getBaseWidgetSettings(settings);

		Ogre::ColourValue color = Ogre::ColourValue::White;
		GuiLabel::LabelAlignment alignment = GuiLabel::LA_CENTER;
		bool scaled = true;

		foreach(SettingsMultiMap::value_type const & vt, *settings)
		{
			String key = StringUtil::to_lower_copy(vt.first);
			String value = StringUtil::to_lower_copy(vt.second);
			if(key == "textcolor")
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
			else if(key == "textalignment")
			{
				if(value == "topleft")
				{
					alignment = GuiLabel::LA_TOPLEFT;
				}
				else if(value == "top")
				{
					alignment = GuiLabel::LA_TOP;
				}
				else if(value == "topright")
				{
					alignment = GuiLabel::LA_TOPRIGHT;
				}
				else if(value == "left")
				{
					alignment = GuiLabel::LA_LEFT;
				}
				else if(value == "center")
				{
					alignment = GuiLabel::LA_CENTER;
				}
				else if(value == "right")
				{
					alignment = GuiLabel::LA_RIGHT;
				}
				else if(value == "bottomleft")
				{
					alignment = GuiLabel::LA_BOTTOMLEFT;
				}
				else if(value == "bottom")
				{
					alignment = GuiLabel::LA_BOTTOM;
				}
				else if(value == "bottomright")
				{
					alignment = GuiLabel::LA_BOTTOMRIGHT;
				}
				else
				{
					oAssertDesc(!"Unknown Alignment", "Unknown Alignment: " << value);
				}
			}
			else if (key == "scaled")
			{
				scaled = Ogre::StringConverter::parseBool(value, scaled);
			}		
		}

		woptr<GuiLabel> label = this->createLabel(id, baseSettings.defaultGlyphIndex, baseSettings.text, baseSettings.position, baseSettings.atlas, baseSettings.z, scaled);
		label.lock()->getCaption()->colour(color);
		label.lock()->setAlignment(alignment);
	}
	//---------------------------------------------------------
	void GuiFactory::onLoadTextbox(String const & id, SettingsMultiMap* settings)
	{
		BasicWidgetSettings baseSettings = this->getBaseWidgetSettings(settings);
	
		bool scaled = true;
		foreach(SettingsMultiMap::value_type const & vt, *settings)
		{
			String key = StringUtil::to_lower_copy(vt.first);
			String value = StringUtil::to_lower_copy(vt.second);
			if (key == "scaled")
			{
				scaled = Ogre::StringConverter::parseBool(value, scaled);
			}
		}
		this->createTextbox(id, baseSettings.defaultGlyphIndex, baseSettings.text, baseSettings.position, baseSettings.atlas, baseSettings.z, scaled);
	}
	//---------------------------------------------------------
	void GuiFactory::onLoadButton(String const & id, SettingsMultiMap* settings)
	{
		BasicWidgetSettings baseSettings = this->getBaseWidgetSettings(settings);

		GuiLabel::LabelAlignment alignment = GuiLabel::LA_CENTER;
		Ogre::ColourValue color = Ogre::ColourValue::Black;
		GuiButtonBlink::ButtonBlinkState blinkState = GuiButtonBlink::BBLINK_NONE;

		foreach(SettingsMultiMap::value_type const & vt, *settings)
		{
			String key = StringUtil::to_lower_copy(vt.first);
			String value = StringUtil::to_lower_copy(vt.second);
			if(key == "textalignment")
			{
				if(value == "topleft")
				{
					alignment = GuiLabel::LA_TOPLEFT;
				}
				else if(value == "top")
				{
					alignment = GuiLabel::LA_TOP;
				}
				else if(value == "topright")
				{
					alignment = GuiLabel::LA_TOPRIGHT;
				}
				else if(value == "left")
				{
					alignment = GuiLabel::LA_LEFT;
				}
				else if(value == "center")
				{
					alignment = GuiLabel::LA_CENTER;
				}
				else if(value == "right")
				{
					alignment = GuiLabel::LA_RIGHT;
				}
				else if(value == "bottomleft")
				{
					alignment = GuiLabel::LA_BOTTOMLEFT;
				}
				else if(value == "bottom")
				{
					alignment = GuiLabel::LA_BOTTOM;
				}
				else if(value == "bottomright")
				{
					alignment = GuiLabel::LA_BOTTOMRIGHT;
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
			else if(key == "blink")
			{
				if(value == "base")
				{
					blinkState = GuiButtonBlink::BBLINK_BASE;
				}
				else if(value == "highlight")
				{
					blinkState = GuiButtonBlink::BBLINK_HIGHLIGHT;
				}
				else if(value == "base_and_highlight")
				{
					blinkState = GuiButtonBlink::BBLINK_BASE_AND_HIGHLIGHT;
				}
				else
				{
					oAssertDesc(!"Unknown blink mode", "Unknown blink mode: " << value);
				}
			}
		}

		woptr<GuiButton> button = this->createButton(id, baseSettings.sprite, baseSettings.defaultGlyphIndex, baseSettings.text, baseSettings.position, alignment, baseSettings.size, baseSettings.atlas, baseSettings.z, false, blinkState);
		oAssert(button.lock());
		if (button.lock()->getLabel().lock())
		{
			button.lock()->getLabel().lock()->getCaption()->colour(color);
		}
	}
	//---------------------------------------------------------
 	void GuiFactory::onLoadCheckBox( String const & id, SettingsMultiMap* settings )
 	{
		BasicWidgetSettings baseSettings = this->getBaseWidgetSettings(settings);

		//GuiLabel::LabelAlignment alignment = GuiLabel::LA_CENTER;
		Ogre::ColourValue color = Ogre::ColourValue::Black;
		bool useCheckbox = false;

		foreach(SettingsMultiMap::value_type const & vt, *settings)
		{
			String key = StringUtil::to_lower_copy(vt.first);
			String value = StringUtil::to_lower_copy(vt.second);
			if(key == "textcolor")
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
			else if (key == "checkbox")
			{
				useCheckbox = Ogre::StringConverter::parseBool(value, false);
			}
		}
		woptr<GuiCheckBox> checkBox = this->createCheckBox(id, baseSettings.sprite, baseSettings.defaultGlyphIndex, baseSettings.text, baseSettings.position, GuiLabel::LA_CENTER, baseSettings.size, baseSettings.atlas, baseSettings.z, useCheckbox);
		oAssert(checkBox.lock());
		//checkBox.lock()->getLabel().lock()->getCaption()->colour(color);
 	}
	//---------------------------------------------------------
	void GuiFactory::onLoadTextEntry(String const & id, SettingsMultiMap* settings)
	{
		// the base settings carry sprite/glyph/position/size/atlas/z; the widget's
		// `text` doubles as the placeholder, plus an optional "maxlength"
		BasicWidgetSettings baseSettings = this->getBaseWidgetSettings(settings);
		uint maxLength = 0;
		foreach(SettingsMultiMap::value_type const & vt, *settings)
		{
			String key = StringUtil::to_lower_copy(vt.first);
			if(key == "maxlength")
			{
				maxLength = Ogre::StringConverter::parseUnsignedInt(vt.second, 0);
			}
		}
		woptr<GuiTextEntry> entry = this->createTextEntry(id, baseSettings.sprite, baseSettings.defaultGlyphIndex, baseSettings.text, baseSettings.position, baseSettings.size, baseSettings.atlas, baseSettings.z, maxLength);
		oAssert(entry.lock());
	}
	//---------------------------------------------------------
	void GuiFactory::onLoadDragDropButton(String const & id, SettingsMultiMap* settings)
	{
		BasicWidgetSettings baseSettings = this->getBaseWidgetSettings(settings);

		GuiLabel::LabelAlignment alignment = GuiLabel::LA_CENTER;
		Ogre::ColourValue color = Ogre::ColourValue::Black;

		foreach(SettingsMultiMap::value_type const & vt, *settings)
		{
			String key = StringUtil::to_lower_copy(vt.first);
			String value = StringUtil::to_lower_copy(vt.second);
			if(key == "textalignment")
			{
				if(value == "topleft")
				{
					alignment = GuiLabel::LA_TOPLEFT;
				}
				else if(value == "top")
				{
					alignment = GuiLabel::LA_TOP;
				}
				else if(value == "topright")
				{
					alignment = GuiLabel::LA_TOPRIGHT;
				}
				else if(value == "left")
				{
					alignment = GuiLabel::LA_LEFT;
				}
				else if(value == "center")
				{
					alignment = GuiLabel::LA_CENTER;
				}
				else if(value == "right")
				{
					alignment = GuiLabel::LA_RIGHT;
				}
				else if(value == "bottomleft")
				{
					alignment = GuiLabel::LA_BOTTOMLEFT;
				}
				else if(value == "bottom")
				{
					alignment = GuiLabel::LA_BOTTOM;
				}
				else if(value == "bottomright")
				{
					alignment = GuiLabel::LA_BOTTOMRIGHT;
				}
				else
				{
					oAssertDesc(!"Unknown Alignment", "Unknown Alignment: " << value);
				}
			}
		}

		woptr<GuiDragDropButton> button = this->createDragDropButton(id, baseSettings.sprite, baseSettings.defaultGlyphIndex, baseSettings.text, baseSettings.position, alignment, baseSettings.size, baseSettings.atlas, baseSettings.z);
		oAssert(button.lock());
	}

	//---------------------------------------------------------
	void GuiFactory::onLoadSelectMenu( String const & id, SettingsMultiMap* settings )
	{
		BasicWidgetSettings baseSettings = this->getBaseWidgetSettings(settings);

		GuiLabel::LabelAlignment alignment = GuiLabel::LA_CENTER;
		Ogre::ColourValue color = Ogre::ColourValue::Black;

		foreach(SettingsMultiMap::value_type const & vt, *settings)
		{
			String key = StringUtil::to_lower_copy(vt.first);
			String value = StringUtil::to_lower_copy(vt.second);
			if(key == "textcolor")
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
			else if(key == "textalignment")
			{
				if(value == "topleft")
				{
					alignment = GuiLabel::LA_TOPLEFT;
				}
				else if(value == "top")
				{
					alignment = GuiLabel::LA_TOP;
				}
				else if(value == "topright")
				{
					alignment = GuiLabel::LA_TOPRIGHT;
				}
				else if(value == "left")
				{
					alignment = GuiLabel::LA_LEFT;
				}
				else if(value == "center")
				{
					alignment = GuiLabel::LA_CENTER;
				}
				else if(value == "right")
				{
					alignment = GuiLabel::LA_RIGHT;
				}
				else if(value == "bottomleft")
				{
					alignment = GuiLabel::LA_BOTTOMLEFT;
				}
				else if(value == "bottom")
				{
					alignment = GuiLabel::LA_BOTTOM;
				}
				else if(value == "bottomright")
				{
					alignment = GuiLabel::LA_BOTTOMRIGHT;
				}
				else
				{
					oAssertDesc(!"Unknown Alignment", "Unknown Alignment: " << value);
				}
			}
		}
																	
		woptr<GuiSelectMenu> selectMenu = this->createSelectMenu(id, "select_menu_button", baseSettings.sprite, baseSettings.defaultGlyphIndex, baseSettings.text, baseSettings.position, alignment, baseSettings.size, baseSettings.atlas, baseSettings.z);
		oAssert(selectMenu.lock());
		selectMenu.lock()->getLabel().lock()->getCaption()->colour(color);
	}

	void GuiFactory::onLoadProgressBar( String const & id, SettingsMultiMap* settings )
	{
		BasicWidgetSettings baseSettings = this->getBaseWidgetSettings(settings);

		GuiLabel::LabelAlignment alignment = GuiLabel::LA_CENTER;
		Ogre::ColourValue color = Ogre::ColourValue::Black;

		foreach(SettingsMultiMap::value_type const & vt, *settings)
		{
			String key = StringUtil::to_lower_copy(vt.first);
			String value = StringUtil::to_lower_copy(vt.second);
			if(key == "textcolor")
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
			else if(key == "textalignment")
			{
				if(value == "topleft")
				{
					alignment = GuiLabel::LA_TOPLEFT;
				}
				else if(value == "top")
				{
					alignment = GuiLabel::LA_TOP;
				}
				else if(value == "topright")
				{
					alignment = GuiLabel::LA_TOPRIGHT;
				}
				else if(value == "left")
				{
					alignment = GuiLabel::LA_LEFT;
				}
				else if(value == "center")
				{
					alignment = GuiLabel::LA_CENTER;
				}
				else if(value == "right")
				{
					alignment = GuiLabel::LA_RIGHT;
				}
				else if(value == "bottomleft")
				{
					alignment = GuiLabel::LA_BOTTOMLEFT;
				}
				else if(value == "bottom")
				{
					alignment = GuiLabel::LA_BOTTOM;
				}
				else if(value == "bottomright")
				{
					alignment = GuiLabel::LA_BOTTOMRIGHT;
				}
				else
				{
					oAssertDesc(!"Unknown Alignment", "Unknown Alignment: " << value);
				}
			}
		}

		woptr<GuiProgressBar> progressBar = this->createProgressBar(id, baseSettings.sprite, baseSettings.defaultGlyphIndex, baseSettings.text, baseSettings.position, alignment, baseSettings.size, baseSettings.atlas, baseSettings.z);
		oAssert(progressBar.lock());
		progressBar.lock()->getLabel().lock()->getCaption()->colour(color);
	}

	//---------------------------------------------------------
	void GuiFactory::onLoadSlider( String const & id, SettingsMultiMap* settings )
	{
		BasicWidgetSettings baseSettings = this->getBaseWidgetSettings(settings);

		GuiLabel::LabelAlignment alignment = GuiLabel::LA_CENTER;
		Ogre::ColourValue color = Ogre::ColourValue::Black;

		foreach(SettingsMultiMap::value_type const & vt, *settings)
		{
			String key = StringUtil::to_lower_copy(vt.first);
			String value = StringUtil::to_lower_copy(vt.second);
			if(key == "textcolor")
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
			else if(key == "textalignment")
			{
				if(value == "topleft")
				{
					alignment = GuiLabel::LA_TOPLEFT;
				}
				else if(value == "top")
				{
					alignment = GuiLabel::LA_TOP;
				}
				else if(value == "topright")
				{
					alignment = GuiLabel::LA_TOPRIGHT;
				}
				else if(value == "left")
				{
					alignment = GuiLabel::LA_LEFT;
				}
				else if(value == "center")
				{
					alignment = GuiLabel::LA_CENTER;
				}
				else if(value == "right")
				{
					alignment = GuiLabel::LA_RIGHT;
				}
				else if(value == "bottomleft")
				{
					alignment = GuiLabel::LA_BOTTOMLEFT;
				}
				else if(value == "bottom")
				{
					alignment = GuiLabel::LA_BOTTOM;
				}
				else if(value == "bottomright")
				{
					alignment = GuiLabel::LA_BOTTOMRIGHT;
				}
				else
				{
					oAssertDesc(!"Unknown Alignment", "Unknown Alignment: " << value);
				}
			}
		}

		woptr<GuiSlider> slider = this->createSlider(id, "slider_menu_button", baseSettings.sprite, baseSettings.defaultGlyphIndex, baseSettings.text, baseSettings.position, alignment, baseSettings.size, baseSettings.atlas, baseSettings.z);
		oAssert(slider.lock());
		slider.lock()->getLabel().lock()->getCaption()->colour(color);
	}

	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
}