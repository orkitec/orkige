/**************************************************************
	created:	2026/07/09 at 12:00
	filename: 	LevelSequence.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __LevelSequence_h__9_7_2026__12_00_00__
#define __LevelSequence_h__9_7_2026__12_00_00__

#include "core_module/OrkigePrerequisites.h"
#include "core_util/String.h"

#include <vector>

namespace Orkige
{
	//! @brief the ordered level list of a game - a levels.olevels asset, the
	//! ORDERED sibling of the flat manifest Settings map (which cannot express
	//! an order). Each entry names a scene (project-relative) plus a display
	//! name and its par slide count; the game plays them front to back, and
	//! win->next advances the index.
	//! @remarks A PROJECT-CONFIG asset (the ConfigAsset convention #81):
	//! manifest-referenced by the Settings key "levels" (LEVELS_SETTING_KEY),
	//! resolved project-relative, bundled into exports via CONFIG_SETTING_KEYS.
	//! Written in the same XMLArchive form as the other .o* config files so the
	//! Python generator can emit it byte-stably; round-trips headlessly.
	class ORKIGE_CORE_DLL LevelSequence
	{
		//--- Types -------------------------------------------
	public:
		//! one level in the sequence
		struct Entry
		{
			String	scenePath;	//!< project-relative scene ("scenes/level2.oscene")
			String	name;		//!< display name ("Straight Shot")
			int		par;		//!< target slide count for a 3-star finish (0 = unscored)

			Entry() : par(0) {}
			Entry(String const & path, String const & displayName, int parValue)
				: scenePath(path), name(displayName), par(parValue) {}
		};
		//--- Variables ---------------------------------------
	public:
		static const String LEVELS_FILE_MAGIC;		//!< "orkige.olevels"
		static const int LEVELS_FORMAT_VERSION;		//!< 1
		static const String LEVELS_SETTING_KEY;		//!< manifest key "levels"
	private:
		std::vector<Entry>	mEntries;
		//--- Methods -----------------------------------------
	public:
		LevelSequence();

		//! number of levels
		int getCount() const { return static_cast<int>(mEntries.size()); }
		//! is index a valid level
		bool isValidIndex(int index) const;
		//! the entries (read-only)
		std::vector<Entry> const & getEntries() const { return mEntries; }

		//! project-relative scene path of a level ("" for an invalid index)
		String getScenePath(int index) const;
		//! display name of a level ("" for an invalid index)
		String getName(int index) const;
		//! par slide count of a level (0 for an invalid index)
		int getPar(int index) const;

		//! append a level (authoring/tests)
		void addEntry(Entry const & entry);
		//! drop every level
		void clear();

		//! @brief load a levels.olevels file; false (and the sequence left
		//! untouched) on a missing/wrong-magic/too-new file
		bool load(String const & fileName);
		//! @brief write a levels.olevels file
		bool save(String const & fileName) const;
	protected:
	private:
	};
}

#endif //__LevelSequence_h__9_7_2026__12_00_00__
