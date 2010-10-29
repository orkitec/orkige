/********************************************************************
created:	Wednesday 2010/08/04 at 15:10
filename: 	GuiManager.cpp
author:		steffen.roemer
notice:		This source file is part of orkige (orkitec Game engine)
For the latest info, see http://www.orkitec.com/
copyright:	(c) 2009-2010 orkitec
*********************************************************************/

#include "engine_gui/GuiManager.h"
#include "engine_graphic/Engine.h"
#include "engine_input/InputManager.h"
#include "engine_util/CameraUtil.h"

namespace Orkige
{
	IMPL_OSINGLETON(GuiManager);
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	GuiManager::GuiManager(String const & guiName, String const & _materialGroup, GuiFactory*	factory) 
		: name(guiName), materialGroup(_materialGroup), guiFactory(factory), widgetsToDelete(),
		widgetSpacing(2), trayPadding(0), trayDrag(false), expandedMenu(0), dialog(0),
		cursorWasVisible(false), fpsLabel(0), statsPanel(0), loadingBar(0),
		groupInitProportion(0.0f), groupLoadProportion(0.0f), loadingIncrement(0.0f)
	{
		oAssert(this->guiFactory);

		Ogre::ResourceGroupManager::getSingleton().initialiseResourceGroup("Essential");

		Ogre::OverlayManager& om = Ogre::OverlayManager::getSingleton();

		Ogre::String nameBase = this->name + "/";
		std::replace(nameBase.begin(), nameBase.end(), ' ', '_');

		// create overlay layers for everything
		this->backgroundLayer = om.create(nameBase + "BackdropLayer");
		this->traysLayer = om.create(nameBase + "WidgetsLayer");
		this->priorityLayer = om.create(nameBase + "PriorityLayer");
		this->cursorLayer = om.create(nameBase + "CursorLayer");
		this->backgroundLayer->setZOrder(100);
		this->traysLayer->setZOrder(200);
		this->priorityLayer->setZOrder(300);
		this->cursorLayer->setZOrder(400);

		// make backdrop and cursor overlay containers

		this->cursor = (Ogre::OverlayContainer*)om.createOverlayElementFromTemplate(this->materialGroup + "/Cursor", "Panel", nameBase + "Cursor");
		this->cursorLayer->add2D(this->cursor);
		this->backdrop = (Ogre::OverlayContainer*)om.createOverlayElement("Panel", nameBase + "Backdrop");
		this->backgroundLayer->add2D(this->backdrop);
		this->dialogShade = (Ogre::OverlayContainer*)om.createOverlayElement("Panel", nameBase + "DialogShade");
		this->dialogShade->setMaterialName(this->materialGroup + "/Shade");
		this->dialogShade->hide();
		this->priorityLayer->add2D(this->dialogShade);

		Ogre::String trayNames[] = { "TopLeft", "Top", "TopRight", "Left", "Center", "Right", "BottomLeft", "Bottom", "BottomRight" };

		for (unsigned int i = 0; i < Widget::TL_COUNT; i++)    // make the real trays
		{
			this->widgetPadding[i] = 8;
		}
		for (unsigned int i = 0; i < Widget::TL_NONE; i++)    // make the real trays
		{
			this->trays[i] = (Ogre::OverlayContainer*)om.createOverlayElementFromTemplate(this->materialGroup + "/Tray", "BorderPanel", nameBase + trayNames[i] + "Tray");
			this->traysLayer->add2D(this->trays[i]);

			this->trayWidgetAlign[i] = Ogre::GHA_CENTER;

			// align trays based on location
			if (i == Widget::TL_TOP || i == Widget::TL_CENTER || i == Widget::TL_BOTTOM) 
			{
				this->trays[i]->setHorizontalAlignment(Ogre::GHA_CENTER);
			}
			if (i == Widget::TL_LEFT || i == Widget::TL_CENTER || i == Widget::TL_RIGHT) 
			{
				this->trays[i]->setVerticalAlignment(Ogre::GVA_CENTER);
			}
			if (i == Widget::TL_TOPRIGHT || i == Widget::TL_RIGHT || i == Widget::TL_BOTTOMRIGHT) 
			{
				this->trays[i]->setHorizontalAlignment(Ogre::GHA_RIGHT);
			}
			if (i == Widget::TL_BOTTOMLEFT || i == Widget::TL_BOTTOM || i == Widget::TL_BOTTOMRIGHT) 
			{
				this->trays[i]->setVerticalAlignment(Ogre::GVA_BOTTOM);
			}
		}

		// create the null tray for free-floating widgets
		this->trays[Widget::TL_NONE] = (Ogre::OverlayContainer*)om.createOverlayElement("Panel", nameBase + "NullTray");
		this->trayWidgetAlign[Widget::TL_NONE] = Ogre::GHA_LEFT;
		this->traysLayer->add2D(this->trays[Widget::TL_NONE]);

		this->adjustTrays();

		this->showTrays();
		
		this->registerEvent(Orkige::Engine::FrameRenderingQueuedEvent,	&GuiManager::onFrameRenderingQueued,	this);
	}
	//---------------------------------------------------------
	GuiManager::~GuiManager()
	{
		delete this->guiFactory;
		this->guiFactory = NULL;

		Ogre::OverlayManager& om = Ogre::OverlayManager::getSingleton();

		this->destroyAllWidgets();

		for (unsigned int i = 0; i < this->widgetsToDelete.size(); i++)   // delete widgets queued for destruction
		{
			delete this->widgetsToDelete[i];
		}
		this->widgetsToDelete.clear();

		om.destroy(this->backgroundLayer);
		om.destroy(this->traysLayer);
		om.destroy(this->priorityLayer);
		om.destroy(this->cursorLayer);

		this->closeDialog();
		this->hideLoadingBar();

		OverlayUtil::nukeOverlayElement(this->backdrop);
		OverlayUtil::nukeOverlayElement(this->cursor);
		OverlayUtil::nukeOverlayElement(this->dialogShade);

		for (unsigned int i = 0; i < Widget::TL_COUNT; i++)
		{
			OverlayUtil::nukeOverlayElement(this->trays[i]);
		}
	}
	//---------------------------------------------------------
	GuiFactory*	GuiManager::getFactory()
	{
		return this->guiFactory;
	}
	//---------------------------------------------------------
	void GuiManager::enableInputEvents()
	{
		this->registerEvent(Orkige::InputManager::KeyPressedEvent,				&GuiManager::onKeyPressed,				this);
		this->registerEvent(Orkige::InputManager::KeyReleasedEvent,				&GuiManager::onKeyReleased,				this);
		this->registerEvent(Orkige::InputManager::MousePressedEvent,				&GuiManager::onMousePressed,			this);
		this->registerEvent(Orkige::InputManager::MouseReleasedEvent,			&GuiManager::onMouseReleased,			this);
		this->registerEvent(Orkige::InputManager::MouseMovedEvent,				&GuiManager::onMouseMoved,				this);
		this->refreshCursor();
	}
	//---------------------------------------------------------
	void GuiManager::disableInputEvents()
	{
		this->unregisterEvent(Orkige::InputManager::KeyPressedEvent);
		this->unregisterEvent(Orkige::InputManager::KeyReleasedEvent);
		this->unregisterEvent(Orkige::InputManager::MousePressedEvent);
		this->unregisterEvent(Orkige::InputManager::MouseReleasedEvent);
		this->unregisterEvent(Orkige::InputManager::MouseMovedEvent);
	}
	//---------------------------------------------------------
	String const & GuiManager::getName()
	{
		return this->name;
	}
	//---------------------------------------------------------
	Ogre::OverlayContainer* GuiManager::getTrayContainer(Widget::TrayLocation trayLoc) 
	{ 
		return this->trays[trayLoc];
	}
	//---------------------------------------------------------
	Ogre::Overlay* GuiManager::getBackgroundLayer() 
	{ 
		return this->backgroundLayer; 
	}
	//---------------------------------------------------------
	Ogre::Overlay* GuiManager::getTraysLayer() 
	{ 
		return this->traysLayer; 
	}
	//---------------------------------------------------------
	Ogre::Overlay* GuiManager::getCursorLayer() 
	{ 
		return this->cursorLayer; 
	}
	//---------------------------------------------------------
	Ogre::OverlayContainer* GuiManager::getBackgroundContainer() 
	{ 
		return this->backdrop; 
	}
	//---------------------------------------------------------
	Ogre::OverlayContainer* GuiManager::getCursorContainer() 
	{ 
		return this->cursor; 
	}
	//---------------------------------------------------------
	Ogre::OverlayElement* GuiManager::getCursorImage() 
	{ 
		return this->cursor->getChild(this->cursor->getName() + "/CursorImage"); 
	}
	//---------------------------------------------------------
	Ogre::OverlayContainer* GuiManager::getDialogShade()
	{
		return this->dialogShade;
	}
	//---------------------------------------------------------
	void GuiManager::showAll()
	{
		this->showBackground();
		this->showTrays();
		this->showCursor();
	}
	//---------------------------------------------------------
	void GuiManager::hideAll()
	{
		this->hideBackground();
		this->hideTrays();
		this->hideCursor();
	}
	//---------------------------------------------------------
	void GuiManager::showBackground(String const & materialName)
	{
		if (materialName != Ogre::StringUtil::BLANK) this->backdrop->setMaterialName(materialName);
		this->backgroundLayer->show();
	}
	//---------------------------------------------------------
	void GuiManager::hideBackground()
	{
		this->backgroundLayer->hide();
	}
	//---------------------------------------------------------
	void GuiManager::showCursor(String const & materialName)
	{
		if (materialName != Ogre::StringUtil::BLANK) getCursorImage()->setMaterialName(materialName);

		if (!this->cursorLayer->isVisible())
		{
			this->cursorLayer->show();
			this->refreshCursor();
		}
	}
	//---------------------------------------------------------
	void GuiManager::hideCursor()
	{
		this->cursorLayer->hide();

		// give widgets a chance to reset in case they're in the middle of something
		for (unsigned int i = 0; i < Widget::TL_COUNT; i++)
		{
			for (unsigned int j = 0; j < this->widgets[i].size(); j++)
			{
				this->widgets[i][j]->onFocusLost();
			}
		}

		this->setExpandedMenu(0);
	}
	//---------------------------------------------------------
	void GuiManager::refreshCursor()
	{
#if OGRE_NO_VIEWPORT_ORIENTATIONMODE == 0
		// TODO:
		// the position should be based on the orientation, for now simply return
		return;
#endif
#if OGRE_PLATFORM == OGRE_PLATFORM_IPHONE
/*		std::vector<OIS::MultiTouchState> states = InputManager::getSingleton().getMouse()->getMultiTouchStates();
		if(states.size() > 0)
			this->cursor->setPosition(states[0].X.abs, states[0].Y.abs);
 */
#else
		this->cursor->setPosition((Ogre::Real)InputManager::getSingleton().getMouseData()->absX, (Ogre::Real)InputManager::getSingleton().getMouseData()->absY);
#endif
	}
	//---------------------------------------------------------
	void GuiManager::showTrays()
	{
		this->traysLayer->show();
		this->priorityLayer->show();
	}
	//---------------------------------------------------------
	void GuiManager::hideTrays()
	{
		this->traysLayer->hide();
		this->priorityLayer->hide();

		// give widgets a chance to reset in case they're in the middle of something
		for (unsigned int i = 0; i < Widget::TL_COUNT; i++)
		{
			for (unsigned int j = 0; j < this->widgets[i].size(); j++)
			{
				this->widgets[i][j]->onFocusLost();
			}
		}

		this->setExpandedMenu(0);
	}
	//---------------------------------------------------------
	bool GuiManager::isCursorVisible() 
	{ 
		return this->cursorLayer->isVisible(); 
	}
	//---------------------------------------------------------
	bool GuiManager::isBackgroundVisible() 
	{ 
		return this->backgroundLayer->isVisible(); 
	}
	//---------------------------------------------------------
	bool GuiManager::areTraysVisible() 
	{ 
		return this->traysLayer->isVisible(); 
	}
	//---------------------------------------------------------
	void GuiManager::setTrayWidgetAlignment(Widget::TrayLocation trayLoc, Ogre::GuiHorizontalAlignment gha)
	{
		this->trayWidgetAlign[trayLoc] = gha;

		for (unsigned int i = 0; i < this->widgets[trayLoc].size(); i++)
		{
			this->widgets[trayLoc][i]->getOverlayElement()->setHorizontalAlignment(gha);
		}
	}
	//---------------------------------------------------------
	void GuiManager::setWidgetPadding(Ogre::Real padding, Widget::TrayLocation tl)
	{
		this->widgetPadding[tl] = (Ogre::Real)std::max<int>((int)padding, 0);
		this->adjustTrays();
	}
	//---------------------------------------------------------
	void GuiManager::setWidgetSpacing(Ogre::Real spacing)
	{
		this->widgetSpacing = (Ogre::Real)std::max<int>((int)spacing, 0);
		this->adjustTrays();
	}
	//---------------------------------------------------------
	void GuiManager::setTrayPadding(Ogre::Real padding)
	{
		this->trayPadding = (Ogre::Real)std::max<int>((int)padding, 0);
		this->adjustTrays();
	}
	//---------------------------------------------------------
	Ogre::Real GuiManager::getWidgetPadding(Widget::TrayLocation tl) const 
	{ 
		return  this->widgetPadding[tl]; 
	}
	//---------------------------------------------------------
	Ogre::Real GuiManager::getWidgetSpacing() const 
	{ 
		return this->widgetSpacing; 
	}
	//---------------------------------------------------------
	Ogre::Real GuiManager::getTrayPadding() const 
	{ 
		return this->trayPadding; 
	}
	//---------------------------------------------------------
	void GuiManager::adjustTrays()
	{
		for (unsigned int i = 0; i < Widget::TL_NONE; i++)   // resizes and hides trays if necessary
		{
			Ogre::Real trayWidth = 0;
			Ogre::Real trayHeight =  this->widgetPadding[i];
			std::vector<Ogre::OverlayElement*> labelsAndSeps;

			if (this->widgets[i].empty())   // hide tray if empty
			{
				this->trays[i]->hide();
				continue;
			}
			else this->trays[i]->show();

			// arrange widgets and calculate final tray size and position
			for (unsigned int j = 0; j < this->widgets[i].size(); j++)
			{
				Ogre::OverlayElement* e = this->widgets[i][j]->getOverlayElement();

				if (j != 0) trayHeight += this->widgetSpacing;   // don't space first widget

				e->setVerticalAlignment(Ogre::GVA_TOP);
				e->setTop(trayHeight);

				switch (e->getHorizontalAlignment())
				{
				case Ogre::GHA_LEFT:
					e->setLeft(this->widgetPadding[i]);
					break;
				case Ogre::GHA_RIGHT:
					e->setLeft(-(e->getWidth() +  this->widgetPadding[i]));
					break;
				default:
					e->setLeft(-(e->getWidth() / 2));
				}

				// prevents some weird texture filtering problems (just some)
				e->setPosition((Ogre::Real)(int)e->getLeft(), (Ogre::Real)(int)e->getTop());
				e->setDimensions((Ogre::Real)(int)e->getWidth(), (Ogre::Real)(int)e->getHeight());

				trayHeight += e->getHeight();

				Label* l = dynamic_cast<Label*>(this->widgets[i][j]);
				if (l && l->_isFitToTray())
				{
					labelsAndSeps.push_back(e);
					continue;
				}
				Separator* s = dynamic_cast<Separator*>(this->widgets[i][j]);
				if (s && s->_isFitToTray()) 
				{
					labelsAndSeps.push_back(e);
					continue;
				}

				if (e->getWidth() > trayWidth) trayWidth = e->getWidth();
			}

			// add paddings and resize trays
			this->trays[i]->setWidth(trayWidth + 2 *  this->widgetPadding[i]);
			this->trays[i]->setHeight(trayHeight +  this->widgetPadding[i]);

			for (unsigned int i = 0; i < labelsAndSeps.size(); i++)
			{
				labelsAndSeps[i]->setWidth((Ogre::Real)(int)trayWidth);
				labelsAndSeps[i]->setLeft((Ogre::Real)-(int)(trayWidth / 2));
			}
		}

		for (unsigned int i = 0; i < Widget::TL_NONE; i++)    // snap trays to anchors
		{
			if (i == Widget::TL_TOPLEFT || i == Widget::TL_LEFT || i == Widget::TL_BOTTOMLEFT)
				this->trays[i]->setLeft(this->trayPadding);
			if (i == Widget::TL_TOP || i == Widget::TL_CENTER || i == Widget::TL_BOTTOM)
				this->trays[i]->setLeft(-this->trays[i]->getWidth() / 2);
			if (i == Widget::TL_TOPRIGHT || i == Widget::TL_RIGHT || i == Widget::TL_BOTTOMRIGHT)
				this->trays[i]->setLeft(-(this->trays[i]->getWidth() + this->trayPadding));

			if (i == Widget::TL_TOPLEFT || i == Widget::TL_TOP || i == Widget::TL_TOPRIGHT)
				this->trays[i]->setTop(this->trayPadding);
			if (i == Widget::TL_LEFT || i == Widget::TL_CENTER || i == Widget::TL_RIGHT)
				this->trays[i]->setTop(-this->trays[i]->getHeight() / 2);
			if (i == Widget::TL_BOTTOMLEFT || i == Widget::TL_BOTTOM || i == Widget::TL_BOTTOMRIGHT)
				this->trays[i]->setTop(-this->trays[i]->getHeight() - this->trayPadding);

			// prevents some weird texture filtering problems (just some)
			this->trays[i]->setPosition((Ogre::Real)(int)this->trays[i]->getLeft(), (Ogre::Real)(int)this->trays[i]->getTop());
			this->trays[i]->setDimensions((Ogre::Real)(int)this->trays[i]->getWidth(), (Ogre::Real)(int)this->trays[i]->getHeight());
		}
	}
	//---------------------------------------------------------
	Ogre::Ray GuiManager::getCursorRay(Ogre::Camera* cam)
	{
		return CameraUtil::screenToScene(cam, Ogre::Vector2(this->cursor->_getLeft(), this->cursor->_getTop()));
	}
	//---------------------------------------------------------
	void GuiManager::showFrameStats(Widget::TrayLocation trayLoc, int place)
	{
		if (!this->areFrameStatsVisible())
		{
			Ogre::StringVector stats;
			stats.push_back("Average FPS");
			stats.push_back("Best FPS");
			stats.push_back("Worst FPS");
			stats.push_back("Triangles");
			stats.push_back("Batches");

			this->fpsLabel = this->getFactory()->createLabel(Widget::TL_NONE, this->name + "/FpsLabel", this->materialGroup, "FPS:", 180);
			this->registerEvent(Label::LabelHitEvent, &GuiManager::onLabelHit, this);
			this->statsPanel = this->getFactory()->createParamsPanel(Widget::TL_NONE, this->name + "/StatsPanel", this->materialGroup, 180, stats);
		}

		this->moveWidgetToTray(this->fpsLabel, trayLoc, place);
		this->moveWidgetToTray(this->statsPanel, trayLoc, this->locateWidgetInTray(this->fpsLabel) + 1);
	}
	//---------------------------------------------------------
	void GuiManager::hideFrameStats()
	{
		if (this->areFrameStatsVisible())
		{
			this->destroyWidget(this->fpsLabel);
			this->destroyWidget(this->statsPanel);
			this->fpsLabel = 0;
			this->statsPanel = 0;
			this->unregisterEvent(Label::LabelHitEvent);
		}
	}
	//---------------------------------------------------------
	bool GuiManager::areFrameStatsVisible()
	{
		return this->fpsLabel != 0;
	}
	//---------------------------------------------------------
	void GuiManager::toggleAdvancedFrameStats()
	{
		if (this->fpsLabel) 
		{
			this->getEventManager()->trigger(Event(Label::LabelHitEvent, oBadPointer(this->fpsLabel)));
		}
	}
	//---------------------------------------------------------
	void GuiManager::showLoadingBar(unsigned int numGroupsInit, unsigned int numGroupsLoad, Ogre::Real initProportion)
	{
		if (this->dialog) this->closeDialog();
		if (this->loadingBar) this->hideLoadingBar();

		this->loadingBar = new ProgressBar(this->name + "/LoadingBar", this->materialGroup, "Loading...", 400, 308);
		Ogre::OverlayElement* e = this->loadingBar->getOverlayElement();
		this->dialogShade->addChild(e);
		e->setVerticalAlignment(Ogre::GVA_CENTER);
		e->setLeft(-(e->getWidth() / 2));
		e->setTop(-(e->getHeight() / 2));

		Ogre::ResourceGroupManager::getSingleton().addResourceGroupListener(this);
		this->cursorWasVisible = this->isCursorVisible();
		this->hideCursor();
		this->dialogShade->show();

		// calculate the proportion of job required to init/load one group

		if (numGroupsInit == 0 && numGroupsLoad != 0)
		{
			this->groupInitProportion = 0;
			this->groupLoadProportion = 1;
		}
		else if (numGroupsLoad == 0 && numGroupsInit != 0)
		{
			this->groupLoadProportion = 0;
			if (numGroupsInit != 0) this->groupInitProportion = 1;
		}
		else if (numGroupsInit == 0 && numGroupsLoad == 0)
		{
			this->groupInitProportion = 0;
			this->groupLoadProportion = 0;
		}
		else
		{
			this->groupInitProportion = initProportion / numGroupsInit;
			this->groupLoadProportion = (1 - initProportion) / numGroupsLoad;
		}
	}
	//---------------------------------------------------------
	void GuiManager::setLoadingBarText(String const & text)
	{
		this->loadingBar->setComment(text);
		Engine::getSingleton().getRenderWindow()->update();
	}
	//---------------------------------------------------------
	void GuiManager::hideLoadingBar()
	{
		if (this->loadingBar)
		{
			this->loadingBar->cleanup();
			delete this->loadingBar;
			this->loadingBar = 0;

			Ogre::ResourceGroupManager::getSingleton().removeResourceGroupListener(this);
			if (this->cursorWasVisible) 
			{
				this->showCursor();
			}
			this->dialogShade->hide();
		}
	}
	//---------------------------------------------------------
	bool GuiManager::isLoadingBarVisible()
	{
		return this->loadingBar != 0;
	}
	//---------------------------------------------------------
	void GuiManager::showDialog(Dialog::DialogType type, String const & name, const Ogre::DisplayString& caption, const Ogre::DisplayString& message, Ogre::DisplayString const & button1Text, Ogre::DisplayString const & button2Text)
	{
		if (this->loadingBar) this->hideLoadingBar();

		this->closeDialog();

		// give widgets a chance to reset in case they're in the middle of something
		for (unsigned int i = 0; i < Widget::TL_COUNT; i++)
		{
			for (unsigned int j = 0; j < this->widgets[i].size(); j++)
			{
				this->widgets[i][j]->onFocusLost();
			}
		}

		this->dialogShade->show();
		this->dialog = this->getFactory()->createDialog(type, name, this->materialGroup, caption, message, button1Text, button2Text);
		oAssert(this->dialog);
		this->cursorWasVisible = this->isCursorVisible();	
	}
	//---------------------------------------------------------
	void GuiManager::showOkDialog(const Ogre::DisplayString& caption, const Ogre::DisplayString& message, Ogre::DisplayString const & okButtonText)
	{
		this->showDialog(GuiFactory::DT_OK, "OkDialog", caption, message, okButtonText, "");
	}
	//---------------------------------------------------------
	void GuiManager::showYesNoDialog(const Ogre::DisplayString& caption, const Ogre::DisplayString& question, Ogre::DisplayString const & yesButtonText, Ogre::DisplayString const & noButtonText)
	{
		this->showDialog(GuiFactory::DT_YESNO, "YesNoDialog", caption, question, yesButtonText, noButtonText);
	}
	//---------------------------------------------------------
	void GuiManager::closeDialog()
	{
		if (this->dialog)
		{
			this->unregisterEvent(Button::ButtonHitEvent);
			
			delete this->dialog;
			this->dialog = 0;

			this->dialogShade->hide();

			if (!this->cursorWasVisible) 
			{
				this->hideCursor();
			}
		}
	}
	//---------------------------------------------------------
	bool GuiManager::isDialogVisible()
	{
		return this->dialog != 0;
	}
	//---------------------------------------------------------
	Widget* GuiManager::getWidget(Widget::TrayLocation trayLoc, unsigned int place)
	{
		if (place >= 0 && place < this->widgets[trayLoc].size()) 
		{
			return this->widgets[trayLoc][place];
		}

		return 0;
	}
	//---------------------------------------------------------
	Widget* GuiManager::getWidget(Widget::TrayLocation trayLoc, String const & name)
	{
		for (unsigned int i = 0; i < this->widgets[trayLoc].size(); i++)
		{
			if (this->widgets[trayLoc][i]->getName() == name) 
			{
				return this->widgets[trayLoc][i];
			}
		}
		return 0;
	}
	//---------------------------------------------------------
	Widget* GuiManager::getWidget(String const & name)
	{
		for (unsigned int i = 0; i < Widget::TL_COUNT; i++)
		{
			for (unsigned int j = 0; j < this->widgets[i].size(); j++)
			{
				if (this->widgets[i][j]->getName() == name) 
				{
					return this->widgets[i][j];
				}
			}
		}
		return 0;
	}
	//---------------------------------------------------------
	unsigned int GuiManager::getNumWidgets()
	{
		unsigned int total = 0;

		for (unsigned int i = 0; i < Widget::TL_COUNT; i++)
		{
			total += this->widgets[i].size();
		}

		return total;
	}
	//---------------------------------------------------------
	unsigned int GuiManager::getNumWidgets(Widget::TrayLocation trayLoc)
	{
		return this->widgets[trayLoc].size();
	}
	//---------------------------------------------------------
	WidgetIterator GuiManager::getWidgetIterator(Widget::TrayLocation trayLoc)
	{
		return WidgetIterator(this->widgets[trayLoc].begin(), this->widgets[trayLoc].end());
	}
	//---------------------------------------------------------
	int GuiManager::locateWidgetInTray(Widget* widget)
	{
		for (unsigned int i = 0; i < this->widgets[widget->getTrayLocation()].size(); i++)
		{
			if (this->widgets[widget->getTrayLocation()][i] == widget) 
			{
				return i;
			}
		}
		return -1;
	}
	//---------------------------------------------------------
	void GuiManager::destroyWidget(Widget* widget)
	{
		if (!widget) OGRE_EXCEPT(Ogre::Exception::ERR_ITEM_NOT_FOUND, "Widget does not exist.", "TrayManager::destroyWidget");

		// in case special widgets are destroyed manually, set them to 0
		if (widget == this->statsPanel) this->statsPanel = 0;
		else if (widget == this->fpsLabel) this->fpsLabel = 0;

		this->trays[widget->getTrayLocation()]->removeChild(widget->getName());

		WidgetCollection& wList = this->widgets[widget->getTrayLocation()];
		wList.erase(std::find(wList.begin(), wList.end(), widget));
		if (widget == this->expandedMenu) 
		{
			this->setExpandedMenu(0);
		}

		widget->cleanup();

		this->widgetsToDelete.push_back(widget);

		this->adjustTrays();
	}
	//---------------------------------------------------------
	void GuiManager::destroyWidget(Widget::TrayLocation trayLoc, unsigned int place)
	{
		this->destroyWidget(this->getWidget(trayLoc, place));
	}
	//---------------------------------------------------------
	void GuiManager::destroyWidget(Widget::TrayLocation trayLoc, String const & name)
	{
		this->destroyWidget(this->getWidget(trayLoc, name));
	}
	//---------------------------------------------------------
	void GuiManager::destroyWidget(String const & name)
	{
		this->destroyWidget(this->getWidget(name));
	}
	//---------------------------------------------------------
	void GuiManager::destroyAllWidgetsInTray(Widget::TrayLocation trayLoc)
	{
		while (!this->widgets[trayLoc].empty()) 
		{
			this->destroyWidget(this->widgets[trayLoc][0]);
		}
	}
	//---------------------------------------------------------
	void GuiManager::destroyAllWidgets()
	{
		for (unsigned int i = 0; i < Widget::TL_COUNT; i++)  // destroy every widget in every tray (including null tray)
		{
			this->destroyAllWidgetsInTray((Widget::TrayLocation)i);
		}
	}
	//---------------------------------------------------------
	void GuiManager::moveWidgetToTray(Widget* widget, Widget::TrayLocation trayLoc, int place)
	{
		if (!widget) OGRE_EXCEPT(Ogre::Exception::ERR_ITEM_NOT_FOUND, "Widget does not exist.", "TrayManager::moveWidgetToTray");

		// remove widget from old tray
		WidgetCollection& wList = this->widgets[widget->getTrayLocation()];
		WidgetCollection::iterator it = std::find(wList.begin(), wList.end(), widget);
		if (it != wList.end())
		{
			wList.erase(it);
			this->trays[widget->getTrayLocation()]->removeChild(widget->getName());
		}

		// insert widget into new tray at given position, or at the end if unspecified or invalid
		if (place == -1 || place > (int)this->widgets[trayLoc].size()) place = this->widgets[trayLoc].size();
		this->widgets[trayLoc].insert(this->widgets[trayLoc].begin() + place, widget);
		this->trays[trayLoc]->addChild(widget->getOverlayElement());

		widget->getOverlayElement()->setHorizontalAlignment(this->trayWidgetAlign[trayLoc]);

		// adjust trays if necessary
		if (widget->getTrayLocation() != Widget::TL_NONE || trayLoc != Widget::TL_NONE) 
		{
			this->adjustTrays();
		}

		widget->_assignToTray(trayLoc);
	}
	//---------------------------------------------------------
	void GuiManager::moveWidgetToTray(String const & name, Widget::TrayLocation trayLoc, unsigned int place)
	{
		this->moveWidgetToTray(this->getWidget(name), trayLoc, place);
	}
	//---------------------------------------------------------
	void GuiManager::moveWidgetToTray(Widget::TrayLocation currentTrayLoc, String const & name, Widget::TrayLocation targetTrayLoc, int place)
	{
		this->moveWidgetToTray(this->getWidget(currentTrayLoc, name), targetTrayLoc, place);
	}
	//---------------------------------------------------------
	void GuiManager::moveWidgetToTray(Widget::TrayLocation currentTrayLoc, unsigned int currentPlace, Widget::TrayLocation targetTrayLoc, int targetPlace)
	{
		this->moveWidgetToTray(this->getWidget(currentTrayLoc, currentPlace), targetTrayLoc, targetPlace);
	}
	//---------------------------------------------------------
	void GuiManager::removeWidgetFromTray(Widget* widget)
	{
		this->moveWidgetToTray(widget, Widget::TL_NONE);
	}
	//---------------------------------------------------------
	void GuiManager::removeWidgetFromTray(String const & name)
	{
		this->removeWidgetFromTray(this->getWidget(name));
	}
	//---------------------------------------------------------
	void GuiManager::removeWidgetFromTray(Widget::TrayLocation trayLoc, String const & name)
	{
		this->removeWidgetFromTray(this->getWidget(trayLoc, name));
	}
	//---------------------------------------------------------
	void GuiManager::removeWidgetFromTray(Widget::TrayLocation trayLoc, int place)
	{
		this->removeWidgetFromTray(this->getWidget(trayLoc, place));
	}
	//---------------------------------------------------------
	void GuiManager::clearTray(Widget::TrayLocation trayLoc)
	{
		if (trayLoc == Widget::TL_NONE) return;      // can't clear the null tray

		while (!this->widgets[trayLoc].empty())   // remove every widget from given tray
		{
			this->removeWidgetFromTray(this->widgets[trayLoc][0]);
		}
	}
	//---------------------------------------------------------
	void GuiManager::clearAllTrays()
	{
		for (unsigned int i = 0; i < Widget::TL_NONE; i++)
		{
			this->clearTray((Widget::TrayLocation)i);
		}
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------
	bool GuiManager::onLabelHit(Orkige::Event const & event)
	{
		optr<Label> label = event.getDataPtr<Label>();
		bool eventCatched = false;

		if(label.get() == this->fpsLabel)
		{
			eventCatched = true;
			if (this->statsPanel->isVisible())
			{
				this->statsPanel->getOverlayElement()->hide();
				this->fpsLabel->getOverlayElement()->setWidth(150);
				this->removeWidgetFromTray(this->statsPanel);
			}
			else
			{
				this->statsPanel->getOverlayElement()->show();
				this->fpsLabel->getOverlayElement()->setWidth(180);
				this->moveWidgetToTray(this->statsPanel, this->fpsLabel->getTrayLocation(), this->locateWidgetInTray(this->fpsLabel) + 1);
			}
		}
		return eventCatched;
	}
	//---------------------------------------------------------
	bool GuiManager::onFrameRenderingQueued(Orkige::Event const & event)
	{
		for (unsigned int i = 0; i < this->widgetsToDelete.size(); i++)
		{
			delete this->widgetsToDelete[i];
		}
		this->widgetsToDelete.clear();

		Ogre::RenderTarget::FrameStats stats = Engine::getSingleton().getRenderWindow()->getStatistics();

		if (this->areFrameStatsVisible())
		{
			std::ostringstream oss;
			Ogre::String s;

			oss << "FPS: " << std::fixed << std::setprecision(1) << stats.lastFPS;
			s = oss.str();
			for (int i = s.length() - 5; i > 5; i -= 3) { s.insert(i, 1, ','); }
			this->fpsLabel->setCaption(s);

			if (this->statsPanel->isVisible())
			{
				Ogre::StringVector values;

				oss.str("");
				oss << std::fixed << std::setprecision(1) << stats.avgFPS;
				Ogre::String s = oss.str();
				for (int i = s.length() - 5; i > 0; i -= 3) { s.insert(i, 1, ','); }
				values.push_back(s);

				oss.str("");
				oss << std::fixed << std::setprecision(1) << stats.bestFPS;
				s = oss.str();
				for (int i = s.length() - 5; i > 0; i -= 3) { s.insert(i, 1, ','); }
				values.push_back(s);

				oss.str("");
				oss << std::fixed << std::setprecision(1) << stats.worstFPS;
				s = oss.str();
				for (int i = s.length() - 5; i > 0; i -= 3) { s.insert(i, 1, ','); }
				values.push_back(s);

				s = Ogre::StringConverter::toString(stats.triangleCount);
				for (int i = s.length() - 3; i > 0; i -= 3) { s.insert(i, 1, ','); }
				values.push_back(s);

				s = Ogre::StringConverter::toString(stats.batchCount);
				for (int i = s.length() - 3; i > 0; i -= 3) { s.insert(i, 1, ','); }
				values.push_back(s);

				this->statsPanel->setAllParamValues(values);
			}
		}

		return false;
	}
	//---------------------------------------------------------
	bool GuiManager::onKeyPressed(Orkige::Event const & event)
	{
		optr<KeyEventData> data = event.getDataPtr<KeyEventData>();

		if (this->expandedMenu)   // only check top priority widget until it passes on
		{
			return this->expandedMenu->onKeyPressed(*data);
		}

		if (this->dialog)   // only check top priority widget until it passes on
		{
			return this->dialog->onKeyPressed(*data);
		}

		bool eventCatched = false;
		for (unsigned int i = 0; i < Widget::TL_COUNT; i++)
		{
			if (!this->trays[i]->isVisible()) 
			{
				continue;
			}

			for (unsigned int j = 0; j < this->widgets[i].size(); j++)
			{
				Widget* w = this->widgets[i][j];
				if (!w->isVisible()) 
				{
					continue;
				}
				eventCatched = w->onKeyPressed(*data);
				if(eventCatched)
					break;
			}
			if(eventCatched)
				break;
		}

		return eventCatched;
	}
	//---------------------------------------------------------
	bool GuiManager::onKeyReleased(Orkige::Event const & event)
	{
		optr<KeyEventData> data = event.getDataPtr<KeyEventData>();

		if (this->expandedMenu)   // only check top priority widget until it passes on
		{
			this->expandedMenu->onKeyReleased(*data);
			return false;
		}

		if (this->dialog)   // only check top priority widget until it passes on
		{
			this->dialog->onKeyReleased(*data);
			return false;
		}

		bool eventCatched = false;
		for (unsigned int i = 0; i < Widget::TL_COUNT; i++)
		{
			if (!this->trays[i]->isVisible()) 
			{
				continue;
			}

			for (unsigned int j = 0; j < this->widgets[i].size(); j++)
			{
				Widget* w = this->widgets[i][j];
				if (!w->isVisible()) 
				{
					continue;
				}
				eventCatched = w->onKeyReleased(*data);
				if(eventCatched)
					break;
			}
			if(eventCatched)
				break;
		}

		return eventCatched;
	}
	//---------------------------------------------------------
	bool GuiManager::onMousePressed(Orkige::Event const & event)
	{
		optr<MouseEventData> data = event.getDataPtr<MouseEventData>();

#if OGRE_PLATFORM != OGRE_PLATFORM_IPHONE
		// only process left button when stuff is visible
		if (!this->cursorLayer->isVisible() || data->button != MouseEventData::MB_Left) return false;
#else
		// only process touches when stuff is visible
		if (!this->cursorLayer->isVisible()) return false;
#endif
		Ogre::Vector2 cursorPos((Ogre::Real)data->absX, (Ogre::Real)data->absY);
		this->cursor->setPosition(cursorPos.x, cursorPos.y);

		this->trayDrag = false;

		if (this->expandedMenu)   // only check top priority widget until it passes on
		{
			this->expandedMenu->onCursorPressed(cursorPos);
			if (!this->expandedMenu->isExpanded()) 
			{
				this->setExpandedMenu(0);
			}
			return true;
		}

		if (this->dialog)   // only check top priority widget until it passes on
		{
			this->dialog->onCursorPressed(cursorPos);
			return true;
		}

		for (unsigned int i = 0; i < Widget::TL_NONE; i++)   // check if mouse is over a non-null tray
		{
			if (this->trays[i]->isVisible() && OverlayUtil::isCursorOver(this->trays[i], cursorPos, 2))
			{
				this->trayDrag = true;   // initiate a drag that originates in a tray
				break;
			}
		}

		for (unsigned int i = 0; i < this->widgets[Widget::TL_NONE].size(); i++)  // check if mouse is over a non-null tray's widgets
		{
			if (this->widgets[Widget::TL_NONE][i]->isVisible() && OverlayUtil::isCursorOver(this->widgets[Widget::TL_NONE][i]->getOverlayElement(), cursorPos))
			{
				this->trayDrag = true;   // initiate a drag that originates in a tray
				break;
			}
		}

		if (!this->trayDrag) return false;   // don't process if mouse press is not in tray

		for (unsigned int i = 0; i < Widget::TL_COUNT; i++)
		{
			if (!this->trays[i]->isVisible()) 
			{
				continue;
			}

			for (unsigned int j = 0; j < this->widgets[i].size(); j++)
			{
				Widget* w = this->widgets[i][j];
				if (!w->isVisible()) 
				{
					continue;
				}
				w->onCursorPressed(cursorPos);    // send event to widget

				SelectMenu* m = dynamic_cast<SelectMenu*>(w);
				if (m && m->isExpanded())       // a menu has begun a top priority session
				{
					this->setExpandedMenu(m);
					return true;
				}
			}
		}

		return true;   // a tray click is not to be handled by another party
	}
	//---------------------------------------------------------
	bool GuiManager::onMouseReleased(Orkige::Event const & event)
	{
		optr<MouseEventData> data = event.getDataPtr<MouseEventData>();

#if OGRE_PLATFORM != OGRE_PLATFORM_IPHONE
		// only process left button when stuff is visible
		if (!this->cursorLayer->isVisible() || data->button != MouseEventData::MB_Left) return false;
#else
		// only process touches when stuff is visible
		if (!this->cursorLayer->isVisible()) return false;
#endif

		Ogre::Vector2 cursorPos((Ogre::Real)data->absX, (Ogre::Real)data->absY);
		this->cursor->setPosition(cursorPos.x, cursorPos.y);

		if (this->expandedMenu)   // only check top priority widget until it passes on
		{
			this->expandedMenu->onCursorReleased(cursorPos);
			return true;
		}

		if (this->dialog)   // only check top priority widget until it passes on
		{
			this->dialog->onCursorReleased(cursorPos);
			return true;
		}

		if (!this->trayDrag) return false;    // this click did not originate in a tray, so don't process

		Widget* w;

		for (unsigned int i = 0; i < Widget::TL_COUNT; i++)
		{
			if (!this->trays[i]->isVisible()) 
			{
				continue;
			}

			for (unsigned int j = 0; j < this->widgets[i].size(); j++)
			{
				w = this->widgets[i][j];
				if (!w->isVisible()) 
				{
					continue;
				}
				w->onCursorReleased(cursorPos);    // send event to widget
			}
		}

		this->trayDrag = false;		// stop this drag
		return true;				// this click did originate in this tray, so don't pass it on
	}
	//---------------------------------------------------------
	bool GuiManager::onMouseMoved(Orkige::Event const & event)
	{
		if (!this->cursorLayer->isVisible()) return false;   // don't process if cursor layer is invisible

		optr<MouseEventData> data = event.getDataPtr<MouseEventData>();

		Ogre::Vector2 cursorPos((Ogre::Real)data->absX, (Ogre::Real)data->absY);
		this->cursor->setPosition(cursorPos.x, cursorPos.y);

		if (this->expandedMenu)   // only check top priority widget until it passes on
		{
			this->expandedMenu->onCursorMoved(cursorPos);
			return true;
		}

		if (this->dialog)   // only check top priority widget until it passes on
		{
			this->dialog->onCursorMoved(cursorPos);
			return true;
		}

		Widget* w;

		for (unsigned int i = 0; i < Widget::TL_COUNT; i++)
		{
			if (!this->trays[i]->isVisible()) 
			{
				continue;
			}

			for (unsigned int j = 0; j < this->widgets[i].size(); j++)
			{
				w = this->widgets[i][j];
				if (!w->isVisible()) 
				{
					continue;
				}
				w->onCursorMoved(cursorPos);    // send event to widget
			}
		}

		return this->trayDrag; // don't pass this event on if we're in the middle of a drag
	}
	//---------------------------------------------------------
	void GuiManager::setExpandedMenu(SelectMenu* m)
	{
		if (!this->expandedMenu && m)
		{
			Ogre::OverlayContainer* c = (Ogre::OverlayContainer*)m->getOverlayElement();
			Ogre::OverlayContainer* eb = (Ogre::OverlayContainer*)c->getChild(m->getName() + "/MenuExpandedBox");
			eb->_update();
			eb->setPosition((Ogre::Real)(unsigned int)(eb->_getDerivedLeft() * Ogre::OverlayManager::getSingleton().getViewportWidth()), (Ogre::Real)(unsigned int)(eb->_getDerivedTop() * Ogre::OverlayManager::getSingleton().getViewportHeight()));
			c->removeChild(eb->getName());
			this->priorityLayer->add2D(eb);
		}
		else if(this->expandedMenu && !m)
		{
			Ogre::OverlayContainer* eb = this->priorityLayer->getChild(this->expandedMenu->getName() + "/MenuExpandedBox");
			this->priorityLayer->remove2D(eb);
			((Ogre::OverlayContainer*)this->expandedMenu->getOverlayElement())->addChild(eb);
		}

		this->expandedMenu = m;
	}
	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	void GuiManager::resourceGroupScriptingStarted(String const & groupName, size_t scriptCount)
	{
		this->loadingIncrement = this->groupInitProportion / scriptCount;
		this->loadingBar->setCaption("Parsing...");
		Engine::getSingleton().getRenderWindow()->update();
	}
	//---------------------------------------------------------
	void GuiManager::scriptParseStarted(String const & scriptName, bool& skipThisScript)
	{
		this->loadingBar->setComment(scriptName);
		Engine::getSingleton().getRenderWindow()->update();
	}
	//---------------------------------------------------------
	void GuiManager::scriptParseEnded(String const & scriptName, bool skipped)
	{
		this->loadingBar->setProgress(this->loadingBar->getProgress() + this->loadingIncrement);
		Engine::getSingleton().getRenderWindow()->update();
	}
	//---------------------------------------------------------
	void GuiManager::resourceGroupScriptingEnded(String const & groupName) 
	{
	}
	//---------------------------------------------------------
	void GuiManager::resourceGroupLoadStarted(String const & groupName, size_t resourceCount)
	{
		this->loadingIncrement = this->groupLoadProportion / resourceCount;
		this->loadingBar->setCaption("Loading...");
		Engine::getSingleton().getRenderWindow()->update();
	}
	//---------------------------------------------------------
	void GuiManager::resourceLoadStarted(const Ogre::ResourcePtr& resource)
	{
		this->loadingBar->setComment(resource->getName());
		Engine::getSingleton().getRenderWindow()->update();
	}
	//---------------------------------------------------------
	void GuiManager::resourceLoadEnded()
	{
		this->loadingBar->setProgress(this->loadingBar->getProgress() + this->loadingIncrement);
		Engine::getSingleton().getRenderWindow()->update();
	}
	//---------------------------------------------------------
	void GuiManager::worldGeometryStageStarted(String const & description)
	{
		this->loadingBar->setComment(description);
		Engine::getSingleton().getRenderWindow()->update();
	}
	//---------------------------------------------------------
	void GuiManager::worldGeometryStageEnded()
	{
		this->loadingBar->setProgress(this->loadingBar->getProgress() + this->loadingIncrement);
		Engine::getSingleton().getRenderWindow()->update();
	}
	//---------------------------------------------------------
	void GuiManager::resourceGroupLoadEnded(String const & groupName) 
	{
	}
	//---------------------------------------------------------
	OABSTRACT_IMPL(GuiManager)
	OOBJECT_END
}