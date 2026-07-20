/********************************************************************
	created:	Sunday 2026/07/20 at 12:00
	filename: 	ResourceReader.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

#include "core_filesystem/ResourceReader.h"

namespace Orkige
{
	namespace
	{
		//! the process-wide, non-owning reader pointer. A function-local static
		//! so it is safe to touch before any engine object exists (a core test
		//! that never installs one reads nullptr and every caller falls back).
		//! Single-threaded install/clear at boot/teardown by design - the read
		//! side (reader()) runs on the main thread with the loaders.
		ResourceReader *& mutableReader()
		{
			static ResourceReader * reader = nullptr;
			return reader;
		}
	}
	//---------------------------------------------------------
	void ResourceAccess::setReader(ResourceReader * reader)
	{
		mutableReader() = reader;
	}
	//---------------------------------------------------------
	ResourceReader * ResourceAccess::reader()
	{
		return mutableReader();
	}
}
