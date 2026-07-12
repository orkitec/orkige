/********************************************************************
	created:	Wednesday 2026/07/08 at 20:00
	filename: 	bootstrap_next.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

//! @file bootstrap_next.cpp
//! @brief Ogre-Next-backend bootstrap of the render facade selfcheck/smoke
//! @remarks The ONLY next-flavor test TU allowed to see backend types:
//! SDL owns the window, RenderBackend::createRenderSystem (the Next boot -
//! there is no Engine on this flavor, the RenderSystem IS the boot) brings
//! Ogre-Next up into it. Everything the checks VERIFY lives in the
//! backend-agnostic mains against facade headers only.

#include "SelfcheckBootstrap.h"

#include <SDL3/SDL.h>
#include <engine_render_next/NextBackend.h>

#include <OgreColourValue.h>
#include <OgreDataStream.h>
#include <OgreImage2.h>
#include <OgreException.h>

#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

extern "C" void* orkige_native_window_handle(SDL_Window* window);

namespace SelfcheckBootstrap
{
	namespace
	{
		SDL_Window* gWindow = NULL;
	}
	//---------------------------------------------------------
	Orkige::RenderSystem* boot(unsigned int width, unsigned int height,
		Orkige::String const & logFileName)
	{
		if(!SDL_Init(SDL_INIT_VIDEO))
		{
			SDL_Log("render_facade next bootstrap: SDL_Init failed: %s",
				SDL_GetError());
			return NULL;
		}
		// HIGH_PIXEL_DENSITY: the render surface tracks the OS backing scale.
		// The Metal window already renders at the view's backing scale; the
		// flag makes SDL's own point/pixel accounting agree, and keeps this
		// request identical to the classic bootstrap so both flavors report
		// the same drawable (the render_backend_parity WYSIWYG gate).
		gWindow = SDL_CreateWindow("render facade (next backend)",
			static_cast<int>(width), static_cast<int>(height),
			SDL_WINDOW_HIGH_PIXEL_DENSITY);
		if(!gWindow)
		{
			SDL_Log("render_facade next bootstrap: SDL_CreateWindow "
				"failed: %s", SDL_GetError());
			return NULL;
		}

		Orkige::RenderBackend::NextBootOptions options;
		options.windowTitle = "render facade (next backend)";
		options.width = width;
		options.height = height;
		options.nativeWindowHandle = std::to_string(reinterpret_cast<size_t>(
			orkige_native_window_handle(gWindow)));
		options.logFileName = logFileName;
		// the Hlms shader templates the ogre-next port ships - backend-
		// internal media, hence bootstrap business and not a facade call
		// (same rule as the classic bootstrap's RTSS media registration)
#ifdef ORKIGE_SELFCHECK_NEXT_MEDIA_DIR
		options.hlmsMediaDir = ORKIGE_SELFCHECK_NEXT_MEDIA_DIR;
#endif
		return Orkige::RenderBackend::createRenderSystem(options);
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
		Orkige::RenderBackend::destroyRenderSystem();
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
		// decode through the backend's image codecs (next: the FreeImage
		// codec that wrote the file in the first place)
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
			Ogre::Image2 image;
			image.load(stream,
				fileName.substr(fileName.find_last_of('.') + 1));
			for(Ogre::uint32 y = 0; y < image.getHeight(); ++y)
			{
				for(Ogre::uint32 x = 0; x < image.getWidth(); ++x)
				{
					const Ogre::ColourValue pixel =
						image.getColourAt(x, y, 0);
					if(pixel.r + pixel.g + pixel.b > 0.1f)
					{
						return true;
					}
				}
			}
		}
		catch(Ogre::Exception const & e)
		{
			SDL_Log("render_facade next bootstrap: decoding '%s' failed: %s",
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
			Ogre::Image2 image;
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
			SDL_Log("render_facade next bootstrap: decoding '%s' failed: %s",
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
			Ogre::Image2 image;
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
			SDL_Log("render_facade next bootstrap: decoding '%s' failed: %s",
				fileName.c_str(), e.getDescription().c_str());
			return -1.0f;
		}
	}
}
