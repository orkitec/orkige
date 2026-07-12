/********************************************************************
	created:	Wednesday 2026/07/08 at 18:00
	filename: 	bootstrap_classic.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

//! @file bootstrap_classic.cpp
//! @brief classic-backend bootstrap of the render facade selfcheck
//! @remarks The ONLY selfcheck TU allowed to see backend/engine types:
//! SDL owns the window, Orkige::Engine (the classic bootstrapper) brings
//! OGRE up into it and creates the facade RenderSystem in setup() - the
//! same boot recipe hello_orkige uses. Everything the selfcheck VERIFIES
//! lives in selfcheck_main.cpp against facade headers only.

#include "SelfcheckBootstrap.h"

#include <SDL3/SDL.h>
#include <engine_graphic/Engine.h>
#include <engine_render/RenderSystem.h>
#include <core_event/GlobalEventManager.h>
#include <core_script/ScriptRuntime.h>
#include <engine_util/StringUtil.h>	// StringUtil::Converter (toString)
#include <core_util/Timer.h>

#include <cstdlib>
#include <fstream>
#include <memory>
#include <vector>

using Orkige::uptr;

extern "C" void* orkige_native_window_handle(SDL_Window* window);

namespace SelfcheckBootstrap
{
	namespace
	{
		SDL_Window* gWindow = NULL;
		uptr<Orkige::GlobalEventManager> gEventManager;
		uptr<Orkige::ScriptRuntime> gScriptRuntime;
		uptr<Orkige::Engine> gEngine;
	}
	//---------------------------------------------------------
	Orkige::RenderSystem* boot(unsigned int width, unsigned int height,
		Orkige::String const & logFileName)
	{
		if(!SDL_Init(SDL_INIT_VIDEO))
		{
			SDL_Log("render_facade_selfcheck: SDL_Init failed: %s",
				SDL_GetError());
			return NULL;
		}
		// HIGH_PIXEL_DENSITY: the render surface tracks the OS backing scale.
		// classic OGRE then auto-detects the scaled backing store (960 points
		// -> 1920 px drawable on a 2x display) exactly as the Next Metal window
		// does, so both flavors report the same drawable for the same request
		// (the render_backend_parity WYSIWYG gate).
		gWindow = SDL_CreateWindow("render facade selfcheck",
			static_cast<int>(width), static_cast<int>(height),
			SDL_WINDOW_HIGH_PIXEL_DENSITY);
		if(!gWindow)
		{
			SDL_Log("render_facade_selfcheck: SDL_CreateWindow failed: %s",
				SDL_GetError());
			return NULL;
		}

		// the engine singletons Engine::setup depends on (the hello_orkige
		// recipe: scripting seam before the module init functions)
		Orkige::Timer::initialise();
		gEventManager = std::make_unique<Orkige::GlobalEventManager>();
		gScriptRuntime = std::make_unique<Orkige::ScriptRuntime>();
		init_module_orkige_core();

		gEngine = std::make_unique<Orkige::Engine>(Ogre::SMT_DEFAULT,
			Orkige::StringUtil::BLANK, Orkige::StringUtil::BLANK,
			Orkige::StringUtil::BLANK, logFileName);
		gEngine->setCustomWindowParam("width",
			Orkige::StringUtil::Converter::toString(width));
		gEngine->setCustomWindowParam("height",
			Orkige::StringUtil::Converter::toString(height));
		// honor the runtime graphics-API pick (GL3Plus default / Metal /
		// Vulkan) - the knob ORTHOGONAL to ORKIGE_RENDER_BACKEND
		if(const char* renderSystemEnv = std::getenv("ORKIGE_RENDERSYSTEM"))
		{
			gEngine->setPreferredRenderSystem(renderSystemEnv);
		}

		// classic needs its RTSS shader library (and OgreUnifiedShader.h
		// from Media/Main) registered before setup - backend-internal
		// media, hence bootstrap business and not a facade call
		Ogre::ResourceGroupManager::getSingleton().addResourceLocation(
			ORKIGE_SELFCHECK_OGRE_MEDIA_DIR "/Main", "FileSystem",
			Ogre::RGN_INTERNAL);
		Ogre::ResourceGroupManager::getSingleton().addResourceLocation(
			ORKIGE_SELFCHECK_OGRE_MEDIA_DIR "/RTShaderLib", "FileSystem",
			Ogre::RGN_INTERNAL);

		if(!gEngine->setup("render facade selfcheck",
			Orkige::Engine::SHOW_NEVER,
			Orkige::StringUtil::Converter::toString(
				reinterpret_cast<size_t>(orkige_native_window_handle(gWindow)))))
		{
			SDL_Log("render_facade_selfcheck: Engine::setup failed");
			return NULL;
		}
		// Engine::setup created the facade - hand it out through the
		// backend-agnostic access point the checks will use anyway
		return Orkige::RenderSystem::get();
	}
	//---------------------------------------------------------
	void pumpHostEvents(bool & outQuitRequested)
	{
		SDL_Event event;
		while(SDL_PollEvent(&event))
		{
			if(event.type == SDL_EVENT_QUIT)
			{
				outQuitRequested = true;
			}
		}
	}
	//---------------------------------------------------------
	void getLogicalWindowSize(unsigned int & outWidth,
		unsigned int & outHeight)
	{
		outWidth = 0;
		outHeight = 0;
		if(gWindow)
		{
			int pointsW = 0, pointsH = 0;
			SDL_GetWindowSize(gWindow, &pointsW, &pointsH);
			outWidth = pointsW > 0 ? static_cast<unsigned int>(pointsW) : 0;
			outHeight = pointsH > 0 ? static_cast<unsigned int>(pointsH) : 0;
		}
	}
	//---------------------------------------------------------
	void shutdown()
	{
		gEngine.reset();		// destroys the facade RenderSystem too
		gScriptRuntime.reset();
		gEventManager.reset();
		if(gWindow)
		{
			SDL_DestroyWindow(gWindow);
			gWindow = NULL;
		}
		SDL_Quit();
	}
	//---------------------------------------------------------
	bool imageHasNonBlackPixel(Orkige::String const & fileName)
	{
		// decode through the backend's image codecs (classic: the STBI
		// png codec that wrote the file in the first place)
		std::ifstream file(fileName, std::ios::binary);
		if(!file)
		{
			return false;
		}
		std::vector<char> bytes((std::istreambuf_iterator<char>(file)),
			std::istreambuf_iterator<char>());
		if(bytes.empty())
		{
			return false;
		}
		try
		{
			Ogre::DataStreamPtr stream(new Ogre::MemoryDataStream(
				bytes.data(), bytes.size(), false /*freeOnClose*/));
			Ogre::Image image;
			image.load(stream,
				fileName.substr(fileName.find_last_of('.') + 1));
			for(Ogre::uint32 y = 0; y < image.getHeight(); ++y)
			{
				for(Ogre::uint32 x = 0; x < image.getWidth(); ++x)
				{
					const Ogre::ColourValue pixel = image.getColourAt(x, y, 0);
					if(pixel.r + pixel.g + pixel.b > 0.1f)
					{
						return true;
					}
				}
			}
		}
		catch(Ogre::Exception const & e)
		{
			SDL_Log("render_facade_selfcheck: decoding '%s' failed: %s",
				fileName.c_str(), e.getDescription().c_str());
			return false;
		}
		return false;
	}
	//---------------------------------------------------------
	bool readImagePixel(Orkige::String const & fileName,
		unsigned int x, unsigned int y,
		float & outRed, float & outGreen, float & outBlue)
	{
		std::ifstream file(fileName, std::ios::binary);
		if(!file)
		{
			return false;
		}
		std::vector<char> bytes((std::istreambuf_iterator<char>(file)),
			std::istreambuf_iterator<char>());
		if(bytes.empty())
		{
			return false;
		}
		try
		{
			Ogre::DataStreamPtr stream(new Ogre::MemoryDataStream(
				bytes.data(), bytes.size(), false /*freeOnClose*/));
			Ogre::Image image;
			image.load(stream,
				fileName.substr(fileName.find_last_of('.') + 1));
			if(x >= image.getWidth() || y >= image.getHeight())
			{
				return false;
			}
			const Ogre::ColourValue pixel = image.getColourAt(x, y, 0);
			outRed = pixel.r;
			outGreen = pixel.g;
			outBlue = pixel.b;
			return true;
		}
		catch(Ogre::Exception const & e)
		{
			SDL_Log("render_facade_selfcheck: decoding '%s' failed: %s",
				fileName.c_str(), e.getDescription().c_str());
			return false;
		}
	}
	//---------------------------------------------------------
	float imageMaxBrightness(Orkige::String const & fileName)
	{
		std::ifstream file(fileName, std::ios::binary);
		if(!file)
		{
			return -1.0f;
		}
		std::vector<char> bytes((std::istreambuf_iterator<char>(file)),
			std::istreambuf_iterator<char>());
		if(bytes.empty())
		{
			return -1.0f;
		}
		try
		{
			Ogre::DataStreamPtr stream(new Ogre::MemoryDataStream(
				bytes.data(), bytes.size(), false /*freeOnClose*/));
			Ogre::Image image;
			image.load(stream,
				fileName.substr(fileName.find_last_of('.') + 1));
			float best = 0.0f;
			for(Ogre::uint32 y = 0; y < image.getHeight(); ++y)
			{
				for(Ogre::uint32 x = 0; x < image.getWidth(); ++x)
				{
					const Ogre::ColourValue pixel = image.getColourAt(x, y, 0);
					const float luma = (pixel.r + pixel.g + pixel.b) / 3.0f;
					best = luma > best ? luma : best;
				}
			}
			return best;
		}
		catch(Ogre::Exception const & e)
		{
			SDL_Log("render_facade_selfcheck: decoding '%s' failed: %s",
				fileName.c_str(), e.getDescription().c_str());
			return -1.0f;
		}
	}
}
