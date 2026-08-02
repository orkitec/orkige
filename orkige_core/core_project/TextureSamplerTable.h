/**************************************************************
	created:	2026/08/02 at 12:00
	filename: 	TextureSamplerTable.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __TextureSamplerTable_h__2_8_2026__12_00_00__
#define __TextureSamplerTable_h__2_8_2026__12_00_00__

#include "core_module/OrkigePrerequisites.h"
#include "core_util/String.h"
#include "core_util/optr.h"

#include <map>

namespace Orkige
{
	class AssetDatabase;

	//! @brief how the runtime SAMPLES one texture asset - the only import
	//! setting a running game honors (everything else in a texture's import
	//! block conditions the shipped pixels at cook time and is already baked
	//! into them by the time a runtime sees the file).
	//! @remarks the choice fields stay strings ("point"/"bilinear",
	//! "clamp"/"wrap") because this type lives in core, BELOW the render facade
	//! that owns the SpriteQuad sampler enums - the engine layer maps them.
	struct ORKIGE_CORE_DLL TextureSampler
	{
		String	filter = "bilinear";	//!< "point" | "bilinear"
		String	wrap = "clamp";			//!< "clamp" | "wrap"

		//! is this the sampler a texture without any authored intent gets
		bool isDefault() const;
		bool operator==(TextureSampler const & other) const
		{
			return this->filter == other.filter && this->wrap == other.wrap;
		}
	};

	//! @brief the open project's texture sampler answers, keyed by texture -
	//! the ONE thing a component asks when it needs to know how a texture is
	//! sampled.
	//! @remarks ONE lookup, TWO fill sources, selected by where the project
	//! came from:
	//!  - AUTHORING (a project directory): fillFromAssets() resolves each
	//!    texture's `.orkmeta` import block for the running platform, so an
	//!    edit to a sidecar shows up on the next project refresh.
	//!  - PACKAGED (an exported app): the export RESOLVED the same question
	//!    once, for the platform it packaged for, and wrote the answers into
	//!    the payload manifest (@see Project). An exported payload carries no
	//!    sidecars at all - it is frozen build output, not an editable project.
	//!
	//! Entries are keyed by a texture's bare STEM ("ball" for "assets/ball.png")
	//! because that is what survives the export-time cook: a compressed texture
	//! is written beside its source name ("ball.png" -> "ball.dds") while the
	//! scene reference keeps naming the source. Resource names are basenames in
	//! the engine's resource groups and so are unique per project anyway.
	//! Comparison is CASE-SENSITIVE on every platform, like the asset database's
	//! path lookups: a case-insensitive host filesystem must not hide a
	//! mismatch that breaks on Linux/Android.
	//!
	//! Only NON-DEFAULT samplers are stored: an absent key answers with the
	//! defaults, so a project that never authors a sampler carries no table.
	class ORKIGE_CORE_DLL TextureSamplerTable
	{
		//--- Variables ---------------------------------------
	private:
		std::map<String, TextureSampler>	mSamplers;	//!< stem -> sampler
		static optr<TextureSamplerTable>	sActive;	//!< the open project's (may be NULL)
		//--- Methods -----------------------------------------
	public:
		//! @brief the lookup key of a texture reference: its bare file name
		//! without the extension ("assets/ball.png" -> "ball"). Pure.
		static String keyFor(String const & textureReference);

		//! @brief record @p sampler for a texture reference. A DEFAULT sampler
		//! removes the entry instead of storing one - the table only ever holds
		//! authored intent.
		void set(String const & textureReference, TextureSampler const & sampler);
		//! @brief the sampler for a texture reference; the defaults when the
		//! table knows nothing about it
		TextureSampler lookup(String const & textureReference) const;

		bool empty() const { return this->mSamplers.empty(); }
		size_t size() const { return this->mSamplers.size(); }
		void clear() { this->mSamplers.clear(); }
		//! every recorded entry, sorted by key (a stable emission order)
		std::map<String, TextureSampler> const & entries() const
		{
			return this->mSamplers;
		}

		//! @brief fill from a project's sidecars: every id-carrying asset whose
		//! `<texture>` block resolves - for @p platform - to a non-default
		//! sampler. The AUTHORING source; also what an export bakes from.
		//! @param platform the import-settings platform token ("" = desktop,
		//! "ios", "android", "web")
		void fillFromAssets(AssetDatabase const & database,
			String const & platform);

		//--- the active table (the open project's) -----------
		//! @brief make the given table the process-wide one components resolve
		//! against (NULL = none - every texture then samples with the defaults)
		static void setActive(optr<TextureSamplerTable> const & table);
		//! the active table or NULL
		static optr<TextureSamplerTable> const & getActive();
		//! @brief the active table's answer for a texture reference, or the
		//! defaults when there is no open project. The one call a component
		//! makes.
		static TextureSampler resolve(String const & textureReference);
	};
	//---------------------------------------------------------
}

#endif //__TextureSamplerTable_h__2_8_2026__12_00_00__
