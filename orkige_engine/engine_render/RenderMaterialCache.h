/********************************************************************
	created:	Tuesday 2026/08/04 at 12:00
	filename: 	RenderMaterialCache.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __RenderMaterialCache_h__4_8_2026__12_00_00__
#define __RenderMaterialCache_h__4_8_2026__12_00_00__

#include "engine_render/RenderPrerequisites.h"
#include "engine_render/RenderMaterial.h"
#include <core_util/String.h>

#include <cstddef>
#include <map>

namespace Orkige
{
	/** \addtogroup Render
	*  @{ */

	//! @brief exact field-by-field equality of two material descriptions.
	//! EXACT on purpose (no tolerance): two descriptions parsed from the same
	//! unchanged `.omat` are bit-identical, and any authored edit - however
	//! small - must count as a change.
	ORKIGE_ENGINE_DLL bool materialDescEqual(RenderMaterialDesc const & left,
		RenderMaterialDesc const & right);

	//! @brief the memo behind `RenderSystem::createMaterial`: does this named
	//! backend material still have to be built from this description?
	//!
	//! WHY it exists: createMaterial is CREATE-OR-UPDATE, so that an edited
	//! `.omat` re-applies to every surface already using it. But building is
	//! not cheap and it is not local - assigning a field of a live material
	//! invalidates every renderable already bound to it (the next flavor
	//! flushes the datablock's linked renderables; the classic one rebuilds
	//! the pass and its generated techniques). A scene where N instances
	//! reference ONE material asks for the same build N times, and the k-th
	//! build touches the k-1 surfaces already bound - quadratic in N, paid as
	//! a scene-load stall.
	//!
	//! The memo turns the repeat into a no-op WITHOUT weakening the update
	//! contract: an edited asset parses to a DIFFERENT description, so it
	//! still rebuilds and still reaches every surface.
	//!
	//! IDENTITY, not just the name: an entry is trusted only while the very
	//! material object it was recorded against is the one found under that
	//! name. A material that was destroyed and rebuilt arrives through the
	//! build path, which rewrites the entry - so the memo can never describe
	//! something that is gone.
	//!
	//! An INCOMPLETE build (a texture the description names is missing) is
	//! never recorded: the map may appear later, and the honest answer is to
	//! try again rather than freeze the miss.
	//!
	//! Pure and backend-neutral - both flavors consult the ONE shared
	//! instance, and the decision is unit-tested on a standalone one.
	class ORKIGE_ENGINE_DLL RenderMaterialCache
	{
		//--- Types -------------------------------------------
	private:
		//! what a name was last built as: the material object and its description
		struct Entry
		{
			void const *		material;	//!< the backend material the entry describes
			RenderMaterialDesc	desc;		//!< the description it was built from
		};
		//--- Variables ---------------------------------------
	private:
		std::map<String, Entry>	mEntries;			//!< name -> last completed build
		unsigned long			mBuildCount = 0;	//!< completed builds recorded (the test hook)
		//--- Methods -----------------------------------------
	public:
		//! @brief must @p material, found under @p name, be (re)built from @p desc?
		//! @param material the live backend material object, or NULL when the
		//! name resolves to nothing yet (which always needs a build)
		//! @return false ONLY when this exact material object was last built
		//! completely from an equal description
		bool needsBuild(String const & name, void const * material,
			RenderMaterialDesc const & desc) const;
		//! record a COMPLETED build (an incomplete one must not be recorded -
		//! @see forget)
		void recordBuilt(String const & name, void const * material,
			RenderMaterialDesc const & desc);
		//! @brief drop the entry for @p name, so the next call rebuilds. The
		//! incomplete-build answer, and the hook for a material going away.
		void forget(String const & name);
		//! @brief drop every entry (render-system teardown). The build count
		//! is a cumulative record of work done, not of live entries, so it
		//! survives - a caller reads it as a DELTA.
		void clear();
		//! how many completed builds were recorded - the observable a scene
		//! load asserts its shared materials against
		unsigned long buildCount() const { return this->mBuildCount; }
		//! how many names are currently memoized
		std::size_t size() const { return this->mEntries.size(); }

		//! @brief the ONE process-wide instance both backends consult, so the
		//! decision is made in a single place no matter which flavor is up
		static RenderMaterialCache & shared();
	};

	/** @} */
}

#endif //__RenderMaterialCache_h__4_8_2026__12_00_00__
