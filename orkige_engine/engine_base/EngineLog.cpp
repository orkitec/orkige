/**************************************************************
	created:	2026/07/08 at 21:00
	filename: 	EngineLog.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	The engine-log service: the ONLY place above the render backends
	that talks to the logging backend (OGRE's LogManager - incidentally
	also present in Ogre-Next, and swappable behind this TU either way).
	Extracted from engine_runtime/PlayerRuntime.cpp's PlayerLogForwarder
	so the player runtimes and the editor share one capture.
***************************************************************/

#include "engine_base/EngineLog.h"

#include <OgreLogManager.h>

#include <cstdio>
#include <deque>
#include <mutex>

namespace Orkige
{
	const size_t EngineLogCapture::DEFAULT_BACKLOG_LINES = 200;
	//---------------------------------------------------------
	//! the backend listener: queues every logged line (mutex-guarded -
	//! messageLogged may in principle fire off the main thread)
	struct EngineLogCapture::Impl : public Ogre::LogListener
	{
		size_t				backlogLineCap;
		bool				attached = false;
		std::mutex			mutex;
		std::deque<Line>	pending;

		explicit Impl(size_t lineCap) : backlogLineCap(lineCap) {}

		virtual void messageLogged(Ogre::String const & message,
			Ogre::LogMessageLevel lml, bool maskDebug,
			Ogre::String const & logName, bool & skipThisMessage) override
		{
			(void)maskDebug;
			(void)logName;
			if (skipThisMessage)
			{
				return;
			}
			const char * level = "info";
#ifdef ORKIGE_RENDER_NEXT
			// Ogre-Next's level enum has no LML_WARNING slot
			if (lml >= Ogre::LML_CRITICAL)
			{
				level = "error";
			}
#else
			if (lml == Ogre::LML_WARNING)
			{
				level = "warning";
			}
			else if (lml >= Ogre::LML_CRITICAL)
			{
				level = "error";
			}
#endif
			std::lock_guard<std::mutex> lock(this->mutex);
			if (this->pending.size() >= this->backlogLineCap)
			{
				this->pending.pop_front();	// oldest lines drop on overflow
			}
			this->pending.push_back({ level, message });
		}
	};
	//---------------------------------------------------------
	EngineLogCapture::EngineLogCapture(size_t backlogLineCap)
		: mImpl(std::make_unique<Impl>(backlogLineCap))
	{
	}
	//---------------------------------------------------------
	EngineLogCapture::~EngineLogCapture()
	{
		this->detach();
	}
	//---------------------------------------------------------
	bool EngineLogCapture::attach()
	{
		if (this->mImpl->attached)
		{
			return true;
		}
		if (!Ogre::LogManager::getSingletonPtr() ||
			!Ogre::LogManager::getSingleton().getDefaultLog())
		{
			return false;	// no engine log yet - call after the engine is up
		}
		Ogre::LogManager::getSingleton().getDefaultLog()
			->addListener(this->mImpl.get());
		this->mImpl->attached = true;
		return true;
	}
	//---------------------------------------------------------
	void EngineLogCapture::detach()
	{
		if (!this->mImpl->attached)
		{
			return;
		}
		// the log may already be gone at teardown - a vanished LogManager
		// means the listener registration died with it
		if (Ogre::LogManager::getSingletonPtr() &&
			Ogre::LogManager::getSingleton().getDefaultLog())
		{
			Ogre::LogManager::getSingleton().getDefaultLog()
				->removeListener(this->mImpl.get());
		}
		this->mImpl->attached = false;
	}
	//---------------------------------------------------------
	bool EngineLogCapture::isAttached() const
	{
		return this->mImpl->attached;
	}
	//---------------------------------------------------------
	std::vector<EngineLogCapture::Line> EngineLogCapture::drain()
	{
		std::deque<Line> lines;
		{
			std::lock_guard<std::mutex> lock(this->mImpl->mutex);
			lines.swap(this->mImpl->pending);
		}
		return std::vector<Line>(lines.begin(), lines.end());
	}
	//---------------------------------------------------------
	void EngineLogCapture::logMessage(String const & text)
	{
		if (Ogre::LogManager::getSingletonPtr())
		{
			Ogre::LogManager::getSingleton().logMessage(text);
		}
	}
	//---------------------------------------------------------
	void EngineLogCapture::logError(String const & text)
	{
		if (Ogre::LogManager::getSingletonPtr())
		{
#ifdef ORKIGE_RENDER_NEXT
			// Ogre-Next's LogManager has no logError shorthand
			Ogre::LogManager::getSingleton().logMessage(text,
				Ogre::LML_CRITICAL);
#else
			Ogre::LogManager::getSingleton().logError(text);
#endif
		}
		else
		{
			fprintf(stderr, "%s\n", text.c_str());
		}
	}
}
