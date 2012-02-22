/**************************************************************
	created:	2010/07/19 at 23:44
	filename: 	MenuState.h
	author:		MorrK
	
	purpose:	
***************************************************************/
#ifndef __MenuState_h__19_7_2010__23_44_36__
#define __MenuState_h__19_7_2010__23_44_36__

#include <core_game/GameState.h>
#include <engine_input/InputManager.h>
#include "MeshMerger.h"
//#include <engine_fastgui/FastGuiTextbox.h>
#include <engine_graphic/IngameConsole.h>



namespace SceneOptimizer
{
	class MenuState : public Orkige::GameState
	{
		//--- Types -------------------------------------------
	public:
	protected:
	private:
		//--- Variables ---------------------------------------
	public:
	protected:
	private:
		Ogre::Vector2 mousePos;
		MeshMerger* merger;
		Ogre::SceneNode* freeCamAttach;
		Ogre::Camera* cam;
		//optr<Orkige::FastGuiTextbox> statsValues;
		//--- Methods -----------------------------------------
	public:
		MenuState(Orkige::String const & id);
		virtual ~MenuState();
	protected:
		//! gets called when this stat is entered
		virtual void onEnter();
		//!gets triggered when state is left
		virtual void onExit();

		bool onButtonHit(Orkige::Event const & event);
		bool onCheckBoxToggled(Orkige::Event const & event);

		bool onFrameStarted(Orkige::Event const & event);

		bool onKeyPressed(Orkige::Event const & event);
		bool onKeyReleased(Orkige::Event const & event);

		bool onMousePressed(Orkige::Event const & event);
		bool onMouseReleased(Orkige::Event const & event);
		bool onMouseMoved(Orkige::Event const & event);

	private:
		void loadAndPrepareScene ();

	};
	//---------------------------------------------------------
}

#endif //__MenuState_h__19_7_2010__23_44_36__