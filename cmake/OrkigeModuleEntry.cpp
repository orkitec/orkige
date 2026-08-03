/**************************************************************
	created:	2026/08/03 at 09:00
	filename: 	OrkigeModuleEntry.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

//! @file OrkigeModuleEntry.cpp
//! @brief the platform entry point of a native game module, owned by the
//! engine rather than by the game.
//!
//! GAME CODE STAYS PLATFORM-NEUTRAL. A module writes a plain `main()`; what
//! that main has to BE is a property of the target, exactly like the shape of
//! the artifact around it (@see cmake/OrkigeTargetShape.cmake). On a phone the
//! process does not start in main at all - the platform starts an application
//! object, and the window system hands control to the app's own function from
//! inside its run loop - so something has to sit in front. Putting that
//! "something" in the game's source would mean every project's main.cpp
//! carries a platform include it must not care about, and would have to be
//! edited, by its author, the day the project targets another one.
//!
//! So the game-module helper compiles THIS translation unit into every module
//! whose target needs a platform entry, and gives all the module's OTHER
//! translation units the window system's rename-only view of the same header
//! (its no-implementation switch), which turns the game's `main` into the
//! function called from here. Two halves of one documented seam:
//!
//!   - every module TU: the header with the implementation suppressed, so it
//!     contributes the rename and nothing else. A multi-TU module would
//!     otherwise get one platform entry per file and fail to link.
//!   - this TU: the implementation, which un-suppresses itself and pulls in
//!     the entry the platform actually starts.
//!
//! On the platforms that need no entry of their own the implementation header
//! emits nothing, so this file is empty weight rather than a special case.

// a no-op when the force-include already ran (its guard is set); spelled anyway
// so this file compiles on its own terms
#include <SDL3/SDL_main.h>

// the force-included header (see above) already ran with the implementation
// suppressed, so its include guard is set and only the implementation is left
// to bring in - which is exactly what this file exists for
#ifdef SDL_MAIN_NOIMPL
#undef SDL_MAIN_NOIMPL
#endif
#include <SDL3/SDL_main_impl.h>
