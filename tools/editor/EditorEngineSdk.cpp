/********************************************************************
	created:	Saturday 2026/08/02 at 09:00
	filename: 	EditorEngineSdk.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#include "EditorEngineSdk.h"

#include "EditorResourcePaths.h"

#include <cstdlib>

namespace OrkigeEditor
{
	namespace
	{
		//! the directories a downloaded editor searches for the build programs:
		//! this machine's PATH. Nothing else - a tool the user did not put on
		//! their PATH is a tool this editor has no business guessing at.
		Orkige::StringVector programSearchDirectories()
		{
			const char* const path = std::getenv("PATH");
			return Orkige::NativeModule::searchPathDirectories(
				path != 0 ? Orkige::String(path) : Orkige::String());
		}
	}
	//---------------------------------------------------------
	EngineSdkStatus resolveEngineSdk(Orkige::String const & projectName,
		Orkige::String const & target)
	{
		EngineSdkStatus status;
		// the writable state directory is the only place a distributed app may
		// hold an installed pack (its own bundle is read-only, and writing into
		// it would invalidate the signature) - @see EditorResourcePaths.h
		status.packDirectory = Orkige::NativeModule::installedPackDirectory(
			editorWritableStateDirectory(), ORKIGE_EDITOR_RENDER_BACKEND);
		status.engine = Orkige::NativeModule::resolveEngineSdk(
			ORKIGE_EDITOR_ENGINE_ROOT, ORKIGE_EDITOR_ENGINE_BUILD_DIR,
			ORKIGE_EDITOR_BUILD_TYPE, status.packDirectory);
		// the baked programs are this build machine's; on a downloaded editor
		// they do not exist and the PATH answers instead
		status.toolchain = Orkige::NativeModule::resolveToolchain(
			ORKIGE_EDITOR_CMAKE, ORKIGE_EDITOR_MAKE_PROGRAM,
			programSearchDirectories());
		status.problem = Orkige::NativeModule::modulePrerequisiteProblem(
			status.engine, status.toolchain, ORKIGE_EDITOR_RENDER_BACKEND,
			status.packDirectory, projectName, target);
		return status;
	}
	//---------------------------------------------------------
	Orkige::StringVector moduleConfigureArguments(
		EngineSdkStatus const & status)
	{
		Orkige::StringVector arguments = {
			Orkige::String("-DCMAKE_MAKE_PROGRAM=") +
				status.toolchain.makeProgram,
			// hermeticity, the same as the presets: never let the module build
			// pick up the banned /usr/local prefix. It is a property of how
			// Orkige builds, not of this machine, so both shapes carry it.
			"-DCMAKE_IGNORE_PREFIX_PATH=/usr/local",
		};
		if(status.engine.fromPack())
		{
			// everything else the module needs, the pack itself records: the
			// scripting backend, the compile contract and the OS floor. Passing
			// this machine's SDK root or this editor's scripting cache entry
			// would only override what the pack already knows.
			return arguments;
		}
		arguments.push_back(
			Orkige::String("-DORKIGE_SCRIPTING=") + ORKIGE_EDITOR_SCRIPTING);
#ifdef __APPLE__
		if(ORKIGE_EDITOR_OSX_SYSROOT[0] != '\0')
		{
			arguments.push_back(Orkige::String("-DCMAKE_OSX_SYSROOT=") +
				ORKIGE_EDITOR_OSX_SYSROOT);
		}
#endif
		return arguments;
	}
}
