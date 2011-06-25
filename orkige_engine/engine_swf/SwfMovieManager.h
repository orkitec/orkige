//created 2008/09/21
#ifndef __OGRESWFMOVIEMANAGER_H__
#define __OGRESWFMOVIEMANAGER_H__


#include "engine_swf/SwfApiDefs.h"

#include <base/utility.h>
#include <base/container.h>
#include <base/tu_file.h>

#include <OgreSingleton.h>

#include "engine_swf/SwfBaseMovie.h"
#include "engine_input/InputManager.h"

namespace Orkige
{
	struct SwfBitmapInfo;
	struct SwfRenderHandler;
	class SwfMovieManager : public Ogre::Singleton<SwfMovieManager>, public EventHandler
	{
		friend class SwfBaseMovie;
		friend struct SwfRenderHandler;
		friend struct SwfBitmapInfo;
		friend void	fs_callback(gameswf::character*, const char*, const char*);
		
		typedef std::vector<SwfBaseMovie*> MovieVector;
		typedef std::map<SwfBaseMovie::SwfMovieType, MovieVector> MovieRegistry;

		// Attributes --------------------------------------------------------------
	public:
	protected:
	private:
		SwfBaseMovie* currentProcessingMovie;
		Ogre::SceneManager* sceneManager;
		gameswf::render_handler* renderHandler;
		gameswf::player* swfPlayer;
		MovieRegistry managedMovies;

		int	mouse_buttons;
		// Methods -----------------------------------------------------------------
	public:
		SwfMovieManager(const Ogre::String & sceneMgrName);
		~SwfMovieManager(void);

		static SwfMovieManager& getSingleton(void);
		static SwfMovieManager* getSingletonPtr(void);

		/**update all TextureMovies, HudMovies, ...
		@param: unique time factor for updating everything*/
		void update(Ogre::Real timeSinceLastFrame);
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
}

#endif //__OGRESWFMOVIEMANAGER_H__