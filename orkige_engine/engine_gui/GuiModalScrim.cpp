/********************************************************************
	created:	Saturday 2026/07/11 at 18:30
	filename: 	GuiModalScrim.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

#include "engine_gui/GuiModalScrim.h"
#include "engine_gui/GuiManager.h"
#include "engine_render/RenderSystem.h"

namespace Orkige
{
	//---------------------------------------------------------
	GuiModalScrim::GuiModalScrim(String const & id, String const & _modalId,
		String const & atlas, uint z, Color const & tint, bool _lightDismiss)
		: GuiDecorWidget(id, "none", Ogre::Vector2::ZERO,
			[]() {
				unsigned int w = 0, h = 0;
				RenderSystem::get()->getWindowSize(w, h);
				return Ogre::Vector2(Real(w > 0 ? w : 1), Real(h > 0 ? h : 1));
			}(), atlas, z),
		modalId(_modalId), lightDismiss(_lightDismiss)
	{
		this->setColour(tint.r, tint.g, tint.b, tint.a);
		// track window size so the backdrop always covers the whole surface
		this->setAnchorPreset("stretchall");
	}
	//---------------------------------------------------------
	GuiModalScrim::~GuiModalScrim()
	{
	}
	//---------------------------------------------------------
	void GuiModalScrim::onCursorPressed(Ogre::Vector2 const & cursorPos)
	{
		(void)cursorPos;
		if(!this->layer->isVisible())
		{
			return;
		}
		// eat every press bound for a lower z layer; the dialog widgets sit on a
		// higher layer and were already dispatched before this scrim
		GuiManager::getSingleton().cancelCurrentInputUpdate();
		if(this->lightDismiss)
		{
			// an outside tap closes the modal - deferred so we never destroy
			// widgets while the dispatch loop is still iterating them
			GuiManager::getSingleton().dismissModal(this->modalId);
		}
	}
	//---------------------------------------------------------
	void GuiModalScrim::onCursorReleased(Ogre::Vector2 const & cursorPos)
	{
		(void)cursorPos;
		if(!this->layer->isVisible())
		{
			return;
		}
		GuiManager::getSingleton().cancelCurrentInputUpdate();
	}
	//---------------------------------------------------------
	void GuiModalScrim::onCursorMoved(Ogre::Vector2 const & cursorPos)
	{
		(void)cursorPos;
		if(!this->layer->isVisible())
		{
			return;
		}
		GuiManager::getSingleton().cancelCurrentInputUpdate();
	}
	//---------------------------------------------------------
	OABSTRACT_IMPL(GuiModalScrim)
	OOBJECT_END
}
