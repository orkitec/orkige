/********************************************************************
	created:	Saturday 2026/08/02 at 09:00
	filename: 	EditorEngineSdk.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __EditorEngineSdk_h__2_8_2026__09_00_00__
#define __EditorEngineSdk_h__2_8_2026__09_00_00__

#include <core_project/NativeModule.h>
#include <core_util/String.h>

//! @file EditorEngineSdk.h
//! @brief the ONE answer to "which engine does this editor build a project's
//! compiled C++ game code against" - and, when it cannot, which of the two
//! prerequisites is missing.
//!
//! Compile-on-Play and project export both ask this, so the resolution has one
//! definition (@see core_project/NativeModule.h, where the decision itself
//! lives and is unit-tested):
//! - an editor built in the SOURCE TREE resolves its own engine build tree,
//!   exactly as it always has - the engine libraries a module links are then
//!   the ones this very editor runs on.
//! - a DOWNLOADED editor has neither repository nor build tree, so it looks
//!   for an installed SDK pack (Docs/sdk-pack.md) in its writable state
//!   directory. A signed app bundle is read-only, so the pack can only live
//!   there and never beside the executable.
//!
//! TWO prerequisites, reported as two: a missing SDK is something to install
//! through Orkige, a missing compiler/cmake/ninja is something to install on
//! the machine. We ship the engine, never a toolchain.

namespace OrkigeEditor
{
	//! @brief the resolved engine + build programs, and the one sentence to
	//! show when a native module cannot be built here
	struct EngineSdkStatus
	{
		Orkige::NativeModule::EngineSdk		engine;
		Orkige::NativeModule::Toolchain		toolchain;
		//! where an installed pack is looked for, whether or not one is there
		//! (named in the refusal, so the sentence is actionable)
		Orkige::String						packDirectory;
		//! "" when a native module can be built; the actionable sentence
		//! otherwise
		Orkige::String						problem;

		//! can this editor build compiled C++ game code right now?
		bool ready() const { return this->problem.empty(); }
	};

	//! @brief resolve the engine and the build programs for @p projectName's
	//! module @p target (both only appear in the refusal sentence).
	EngineSdkStatus resolveEngineSdk(Orkige::String const & projectName,
		Orkige::String const & target);

	//! @brief the extra cache arguments a module configure needs in this
	//! shape, beside the ones NativeModule::configureCommand always writes.
	//! @remarks A build tree hands the module THIS editor's own toolchain
	//! settings (its make program, its scripting backend, its SDK root), which
	//! is what keeps a developer's module byte-compatible with the engine
	//! beside it. Against a pack none of those machine paths exist: the pack
	//! records the scripting backend and the OS floor itself, and the make
	//! program is whatever this machine has.
	Orkige::StringVector moduleConfigureArguments(
		EngineSdkStatus const & status);
}

#endif //__EditorEngineSdk_h__2_8_2026__09_00_00__
