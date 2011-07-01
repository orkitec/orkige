/**************************************************************
	created:	2011/06/26 at 19:35
	filename: 	SwfMovieManager.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec
***************************************************************/
#ifndef __SwfMovieManager_h__26_6_2011__19_35_29__
#define __SwfMovieManager_h__26_6_2011__19_35_29__

#include "engine_swf/SwfApiDefs.h"
#include "engine_swf/SwfBaseMovie.h"
#include "engine_input/InputManager.h"

namespace Orkige
{
	struct SwfBitmapInfo;
	struct SwfRenderHandler;

	class SwfMovieManager : public Singleton<SwfMovieManager>, public EventHandler
	{
		DECL_OSINGLETON(SwfMovieManager);
		friend class SwfBaseMovie;
		friend struct SwfRenderHandler;
		friend struct SwfBitmapInfo;
		friend void	fs_callback(gameswf::character*, const char*, const char*);
		//--- Types -------------------------------------------
	public:
		typedef std::vector<SwfBaseMovie*> MovieVector;
		typedef std::map<SwfBaseMovie::SwfMovieType, MovieVector> MovieRegistry;
	protected:
	private:
		//--- Variables ---------------------------------------
	public:
	protected:
	private:
		SwfBaseMovie* currentProcessingMovie;
		Ogre::SceneManager* sceneManager;
		gameswf::render_handler* renderHandler;
		gameswf::player* swfPlayer;
		MovieRegistry managedMovies;

		int	mouse_buttons;
		//--- Methods -----------------------------------------
	public:
		SwfMovieManager();
		~SwfMovieManager(void);

		/**update all TextureMovies, HudMovies, ...
		@param: unique time factor for updating everything*/
		void update(Ogre::Real timeSinceLastFrame);
	protected:
	private:
		//some helpers for our friends
		Ogre::SceneManager* getSceneManager();
		gameswf::player* getPlayer();

		//before rendering translate the necessary bitmaps (to the current Movie)
		void stream_bitmap(int width, int height, char* data, unsigned int &index);
		void stream_bitmapAlpha(int width, int height, char* data, unsigned int &index);

		//redirect this calls from gameSWF to the current Movie
		void begin_display(float red, float green, float blue);
		void draw_mesh_strip(const void* coords, int vertex_count, float transform[16], float xOffset, float yOffset);
		void draw_line_strip();
		void fill_style_color(float red, float green, float blue, float alpha);
		void fill_style_bitmap(int index);
		void end_display();
		//end

		//movies register and deregister themselves in the SwfBaseMovie constructor/destructor
		bool addMovie(SwfBaseMovie::SwfMovieType type, SwfBaseMovie* movie);
		bool removeMovie(SwfBaseMovie::SwfMovieType type, SwfBaseMovie* movie);


		void OnMovieCallback(gameswf::character* movie, const char* command, const char* args);

		//! Processes mouse button down events. Returns true if the event was consumed and should not be passed on to other handlers.
		bool onMouseEvent(Orkige::Event const & event);
	};
	//---------------------------------------------------------
}

#endif //__SwfMovieManager_h__26_6_2011__19_35_29__
