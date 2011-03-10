/**************************************************************
	created:	2010/07/19 at 23:44
	filename: 	PreviewMenuState.h
	author:		MorrK
	
	purpose:	
***************************************************************/
#ifndef __PreviewMenuState_h__19_7_2010__23_44_36__
#define __PreviewMenuState_h__19_7_2010__23_44_36__

#include <core_game/GameState.h>
#include <engine_input/InputManager.h>
#include <engine_gui/GuiManager.h>




namespace CC
{
	class PreviewMenuState : public Orkige::GameState
	{
		//--- Types -------------------------------------------
	public:
	protected:
	private:
		//--- Variables ---------------------------------------
	public:
	protected:
	private:
		//--- Methods -----------------------------------------
	public:
		PreviewMenuState(Orkige::String const & id);
		virtual ~PreviewMenuState();
	protected:
		//! gets called when this stat is entered
		virtual void onEnter();
		//!gets triggered when state is left
		virtual void onExit();

		bool onButtonHit(Orkige::Event const & event);
	
		bool onFrameStarted(Orkige::Event const & event);

		bool onKeyPressed(Orkige::Event const & event);
		bool onKeyReleased(Orkige::Event const & event);

		bool onMousePressed(Orkige::Event const & event);
		bool onMouseReleased(Orkige::Event const & event);
		bool onMouseMoved(Orkige::Event const & event);
		
//		Orkige::String getLastLevel();
//		void setLastLevel( Orkige::String sceneName );
	private:
		std::string DialogBrowseFile(const char* szTitle, const char* szFileType, const char* szFileTypeDesc);
	};
	//---------------------------------------------------------
}

#endif //__PreviewMenuState_h__19_7_2010__23_44_36__