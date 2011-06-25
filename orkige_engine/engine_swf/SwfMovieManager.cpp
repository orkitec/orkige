
#include "engine_swf/SwfMovieManager.h"


#include <gameswf/gameswf_root.h>
#include <gameswf/gameswf_movie_def.h>
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#include <Ogre.h>
#include <OgreWireBoundingBox.h>

#include "engine_swf/SwfFileHelper.h"
#include "engine_swf/SwfUtil.h"
#include "engine_swf/SwfRenderHandler.h"

#include "engine_swf/SwfHUDMovie.h"


gameswf::render_handler*  create_render_handler_ogre_d3d();
void updateOgreRenderer();

template<> Orkige::SwfMovieManager* Ogre::Singleton<Orkige::SwfMovieManager>::ms_Singleton = 0;

namespace Orkige
{
	SwfMovieManager::SwfMovieManager   (const Ogre::String & sceneMgrName)
	{
		this->sceneManager = Ogre::Root::getSingletonPtr()->getSceneManager(sceneMgrName);

		this->sceneManager->setVisibilityMask(1);
		Ogre::MovableObject::setDefaultVisibilityFlags(1);
		Ogre::WireBoundingBox::setDefaultVisibilityFlags(1);

		gameswf::set_use_cache_files(false);

		gameswf::register_file_opener_callback(Orkige::ogre_file_opener);
		gameswf::register_fscommand_callback(Orkige::fs_callback);
		gameswf::register_log_callback(Orkige::log_callback);

/*		this->renderHandler = create_render_handler_ogre_d3d();*/
/*		this->renderHandler = gameswf::create_render_handler_ogl();*/
		this->renderHandler = new SwfRenderHandler();
		this->swfPlayer = new gameswf::player();
		this->swfPlayer->set_force_realtime_framerate(true);
		gameswf::set_render_handler(this->renderHandler);
		this->currentProcessingMovie = NULL;
		this->mouse_buttons = 0;

		this->registerEvent(Orkige::InputManager::MousePressedEvent,			&SwfMovieManager::onMouseEvent,			this);
		this->registerEvent(Orkige::InputManager::MouseReleasedEvent,			&SwfMovieManager::onMouseEvent,			this);
		this->registerEvent(Orkige::InputManager::MouseMovedEvent,				&SwfMovieManager::onMouseEvent,				this);
	}

	// -------------------------------------------------------------------------------
	SwfMovieManager::~SwfMovieManager()
	{
		this->unregisterEvent(Orkige::InputManager::MousePressedEvent);
		this->unregisterEvent(Orkige::InputManager::MouseReleasedEvent);
		this->unregisterEvent(Orkige::InputManager::MouseMovedEvent);

		if (this->renderHandler) delete this->renderHandler;
		delete this->swfPlayer;
		/*gameswf::clear();*/
	}

	// -------------------------------------------------------------------------------
	SwfMovieManager* SwfMovieManager::getSingletonPtr(void)
	{
		return ms_Singleton;
	}

	// -------------------------------------------------------------------------------
	SwfMovieManager& SwfMovieManager::getSingleton(void)
	{
		assert(ms_Singleton);
		return *ms_Singleton;
	}

	// -------------------------------------------------------------------------------
	bool SwfMovieManager::addMovie(SwfBaseMovie::SwfMovieType type, SwfBaseMovie* movie)
	{
		MovieVector & movies = this->managedMovies[type];
		//Check for doubled names in Movie List
		if(std::find(movies.begin(), movies.end(), movie) == movies.end())
		{
			//to stream the bitmaps (stream_bitmap/Alpha) to the right instance
			this->currentProcessingMovie = movie;

			movies.push_back(movie);

			return true;
		}

		return false;
	}

	// -------------------------------------------------------------------------------
	bool SwfMovieManager::removeMovie(SwfBaseMovie::SwfMovieType type, SwfBaseMovie* movie)
	{
		//Check for doubled names in Movie List
		MovieVector & movies = this->managedMovies[type];
		MovieVector::iterator it = std::find(movies.begin(), movies.end(), movie);
		if(it != movies.end())
		{
			movies.erase(it);
			if(this->currentProcessingMovie == movie)
				this->currentProcessingMovie = NULL;
			return true;
		}
		return false;
	}

	// -------------------------------------------------------------------------------
	void SwfMovieManager::update(Ogre::Real timeSinceLastFrame)
	{
		for(MovieRegistry::iterator it = this->managedMovies.begin(), itend = this->managedMovies.end(); it != itend; ++it)
		{
			SwfBaseMovie::SwfMovieType movieType = it->first;
			MovieVector & movies = it->second;
			
			if(movieType == SwfBaseMovie::TEXTURE)
				this->sceneManager->setVisibilityMask(2);

			for(MovieVector::iterator jit = movies.begin(), jitend = movies.end(); jit != jitend; ++jit)
			{
				this->currentProcessingMovie = (*jit);
				this->currentProcessingMovie->update(timeSinceLastFrame);
			}

			if(movieType == SwfBaseMovie::TEXTURE)
				this->sceneManager->setVisibilityMask(1);

			this->currentProcessingMovie = NULL;
		}
	}

	// -------------------------------------------------------------------------------
	Ogre::SceneManager* SwfMovieManager::getSceneManager()
	{
		return this->sceneManager;
	}

	// -------------------------------------------------------------------------------
	void SwfMovieManager::begin_display(float red, float green, float blue)
	{
		//redirect call to current...
		if(this->currentProcessingMovie)
			this->currentProcessingMovie->begin_display(red, green, blue);
	}

	// -------------------------------------------------------------------------------
	void SwfMovieManager::end_display()
	{
		//redirect call to current...
		if(this->currentProcessingMovie)
			this->currentProcessingMovie->end_display();
	}

	// -------------------------------------------------------------------------------
	void SwfMovieManager::draw_mesh_strip(const void* coords, int vertex_count, float transform[16], float xOffset, float yOffset)
	{
		//redirect call to current...
		if(this->currentProcessingMovie)
			this->currentProcessingMovie->draw_mesh_strip(coords, vertex_count, transform, xOffset, yOffset);
	}

	// -------------------------------------------------------------------------------
	void SwfMovieManager::fill_style_color(float red, float green, float blue, float alpha)
	{
		//redirect call to current...
		if(this->currentProcessingMovie)
			this->currentProcessingMovie->fill_style_color(red, green, blue, alpha);
	}

	// -------------------------------------------------------------------------------
	void SwfMovieManager::fill_style_bitmap(int index)
	{
		//redirect call to current...
		if(this->currentProcessingMovie)
			this->currentProcessingMovie->fill_style_bitmap(index);
	}

	// -------------------------------------------------------------------------------
	void SwfMovieManager::stream_bitmap(int width, int height, char* data, unsigned int &index)
	{
		//redirect call to current...
		if(this->currentProcessingMovie)
			this->currentProcessingMovie->stream_bitmap(width, height, data, index);
	}

	// -------------------------------------------------------------------------------
	void SwfMovieManager::stream_bitmapAlpha(int width, int height, char* data, unsigned int &index)
	{
		//redirect call to current...
		if(this->currentProcessingMovie)
			this->currentProcessingMovie->stream_bitmapAlpha(width, height, data, index);
	}

	// -------------------------------------------------------------------------------
	gameswf::player* SwfMovieManager::getPlayer()
	{
		return this->swfPlayer;
	}
	// -------------------------------------------------------------------------------
	void SwfMovieManager::OnMovieCallback(gameswf::character* movie, const char* command, const char* args)
	{
		assert(this->currentProcessingMovie->movieInterface->m_movie.get_ptr() == movie);
		this->currentProcessingMovie->OnMovieCallback(command, args);
	}
	// -------------------------------------------------------------------------------
	bool SwfMovieManager::onMouseEvent(Orkige::Event const & event)
	{
		optr<MouseEventData> data = event.getDataPtr<MouseEventData>();

		MovieVector hudMovies = this->managedMovies[SwfBaseMovie::HUD];
		for (MovieVector::iterator it = hudMovies.begin(), itend = hudMovies.end(); it != itend; ++it)
		{
			SwfHudMovie* movie = static_cast<SwfHudMovie*>(*it);
			assert(movie);
			float x = data->absX;
			float y = data->absY;
/*
			x/= 200;
			y/= 200;*/

			movie->setMouseSettings(x, y, data->buttons);
		}
		return false;
	}
}