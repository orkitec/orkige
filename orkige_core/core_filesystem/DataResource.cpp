/**************************************************************
	created:	2026/08/03 at 12:00
	filename: 	DataResource.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "core_filesystem/DataResource.h"
#include "core_filesystem/ResourceReader.h"
#include "core_util/PathJail.h"

#include <string>

namespace Orkige
{
	//---------------------------------------------------------
	bool DataResource::checkName(String const & name, String & outError)
	{
		if(name.empty())
		{
			outError = "data resource name is empty";
			return false;
		}
		// the ONE containment primitive: absolute paths, drive/UNC roots and
		// any ".." segment are refused, so a name can only ever address content
		// the project itself ships
		if(!PathJail::isSafeRelativeEntry(name))
		{
			outError = "data resource name '" + name + "' is not a "
				"project-relative path (absolute paths and '..' are refused)";
			return false;
		}
		return true;
	}
	//---------------------------------------------------------
	bool DataResource::read(String const & name, String & outText,
		String & outError)
	{
		if(!DataResource::checkName(name, outError))
		{
			return false;
		}
		ResourceReader const * reader = ResourceAccess::reader();
		if(reader == NULL)
		{
			// NO fopen fallback - see the header. A caller here is content
			// reading content; with nothing mounted there is nothing to read,
			// and reaching for the raw filesystem instead would hand a script
			// exactly the capability the sandbox took away.
			outError = "no content reader is installed - data resources are "
				"readable only while the content mounts are up";
			return false;
		}
		String text;
		if(!reader->readText(name, text))
		{
			outError = "data resource '" + name + "' not found";
			return false;
		}
		if(text.size() > DataResource::kMaxBytes)
		{
			outError = "data resource '" + name + "' is " +
				std::to_string(text.size()) + " bytes, over the " +
				std::to_string(DataResource::kMaxBytes) + " byte limit";
			return false;
		}
		outText.swap(text);
		return true;
	}
	//---------------------------------------------------------
}
