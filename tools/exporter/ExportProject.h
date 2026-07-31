/********************************************************************
	created:	Friday 2026/07/31 at 12:00
	filename: 	ExportProject.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __ExportProject_h__31_7_2026__12_00_00__
#define __ExportProject_h__31_7_2026__12_00_00__

#include <core_util/String.h>

#include <map>

//! @file ExportProject.h
//! @brief the slice of a project manifest an export packages from.
//!
//! Deliberately NOT `core_project/Project`: loading one creates an
//! AssetDatabase and makes it the PROCESS-WIDE ACTIVE database (see
//! Project::getAssetDatabase), which an in-process export would rip out from
//! under the editor's open project. The exporter reads the same small semantic
//! XML document for the four facts it needs - name, main scene, settings, root
//! - and touches nothing global. The editor fills the struct from the project
//! it already has open instead of re-reading the file.

namespace OrkigeExport
{
	//! @brief the manifest facts an export packages from
	struct ExportProject
	{
		Orkige::String					root;		//!< absolute project root
		Orkige::String					name;		//!< human-readable name
		Orkige::String					mainScene;	//!< project-relative
		std::map<Orkige::String, Orkige::String>	settings;

		//! @brief a setting's value, or @p fallback when unset
		Orkige::String setting(Orkige::String const & key,
			Orkige::String const & fallback = "") const;

		//! @brief executable/artifact base name: the project name reduced to
		//! its alphanumerics ("My Game 2" -> "MyGame2"), "OrkigeGame" when
		//! nothing survives.
		Orkige::String exeName() const;

		//! @brief reverse-DNS-safe lowercase slug for the default bundle and
		//! package ids; a leading digit is prefixed with 'p' (a reverse-DNS
		//! label may not start with one), "orkigegame" when nothing survives.
		Orkige::String idSlug() const;

		//! @brief the `native.target` setting, trimmed ("" = no compiled game
		//! code, the Lua/scene shape)
		Orkige::String nativeTarget() const;

		//! @brief read `<root>/project.orkproj` (or the .orkproj file itself)
		//! into @p project. False with an honest @p error on a missing,
		//! unparseable or nameless manifest; @p project is then untouched.
		static bool readManifest(Orkige::String const & path,
			ExportProject & project, Orkige::String * error);
	};
}

#endif //__ExportProject_h__31_7_2026__12_00_00__
