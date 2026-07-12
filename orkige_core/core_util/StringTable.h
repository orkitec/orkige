/********************************************************************
	created:	Friday 2026/07/10 at 12:00
	filename: 	StringTable.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __StringTable_h__10_7_2026__12_00_00__
#define __StringTable_h__10_7_2026__12_00_00__

//! @file StringTable.h
//! @brief backend-neutral localisation table: per-language key->string maps
//! loaded from XLIFF 1.2 (.xlf) files, with %%0%% positional formatting. No
//! renderer or Ogre dependency - games reach it through the Lua loc() accessor
//! and the player loads a project's loc/ directory via the config-asset
//! convention. Inline placeholder codes in the files (<x id="N"/>) map to the
//! %%N%% syntax in memory, so the public formatting surface never changes.

#include "core_module/OrkigePrerequisites.h"
#include "core_util/String.h"
#include "core_util/Singleton.h"

#include <map>
#include <set>

namespace Orkige
{
	//! @brief a set of language string tables plus the active language.
	//! @remarks Singleton so the Lua loc() accessor and games share one table;
	//! the app constructs it (like InputActionMap). A miss returns the key
	//! itself, so an unlocalised build shows readable placeholder text.
	//!
	//! Files are XLIFF 1.2 documents, one per target language, in a directory
	//! (loc/<lang>.xlf, BCP-47 names). All languages load at boot; setLanguage
	//! is I/O-free. A lookup resolves active language -> source language ->
	//! the key itself, so an untranslated string falls back to its own source
	//! text (which the target file carries) before falling back to the key.
	class ORKIGE_CORE_DLL StringTable : public Singleton<StringTable>
	{
		DECL_OSINGLETON_ORKIGE_CORE_DLL(StringTable);
		//--- Types -------------------------------------------------
	public:
		//! manifest Settings key naming the project's localisation directory
		//! (config-asset convention, bundled by orkige_export.py)
		static ORKIGE_CORE_DLL const String LOCALISATION_SETTING_KEY;
	protected:
	private:
		typedef std::map<String, String> Table;			//!< key -> string
		//--- Variables ---------------------------------------------
	public:
	protected:
	private:
		std::map<String, Table>	mLanguages;		//!< language code -> table
		String					mLanguage;		//!< active language code
		String					mSourceLanguage;//!< source-language code
		mutable std::set<String> mWarnedKeys;	//!< (lang/key) misses warned once
		//--- Methods -----------------------------------------------
	public:
		StringTable();
		~StringTable();

		//! @brief load every *.xlf file in a directory. Each file populates its
		//! target language (from usable targets) and contributes its source
		//! texts to the source-language table. Returns true if at least one file
		//! loaded; a broken file is skipped (logged) and never partially loaded.
		bool loadXliffDirectory(String const & directory);
		//! @brief parse one XLIFF 1.2 (.xlf) file. Returns false - and mutates
		//! nothing - on a well-formedness failure, a missing xliff/file/body
		//! spine, a duplicate key, or a source-language that disagrees with an
		//! already-loaded file; the whole document parses before any table
		//! mutation, so a broken file never leaves a partial table.
		bool loadXliffFile(String const & filePath);
		//! @brief add / overwrite a single entry (used by tests). The first
		//! language ever written becomes active if none is set yet.
		void set(String const & language, String const & key,
			String const & value);

		//! @brief set the active language; unknown codes are accepted (get then
		//! falls back to the source language, then the key) so a missing
		//! translation never asserts. I/O-free - all languages are resident.
		void setLanguage(String const & language);
		//! the active language code ("" when nothing has been loaded/selected)
		String const & getLanguage() const { return this->mLanguage; }
		//! the source language code ("" until an XLIFF file establishes it)
		String const & getSourceLanguage() const { return this->mSourceLanguage; }
		//! is a table for that language present?
		bool hasLanguage(String const & language) const;
		//! @brief every loaded language code, sorted (the source language
		//! included); feeds the editor preview and a settings language switch
		StringVector getLanguages() const;

		//! @brief the localized string for a key: the active-language entry, else
		//! the source-language entry (the untranslated fallback), else the key
		//! itself (so untranslated UI stays readable). A fall-through past the
		//! active language logs once per (language, key).
		String const & get(String const & key) const;
		//! is the key present in the active language?
		bool has(String const & key) const;
		//! @brief get + positional substitution: every `%%0%%`..`%%N%%` in the
		//! looked-up string is replaced by args[N] (out-of-range placeholders
		//! are left untouched)
		String format(String const & key, StringVector const & args) const;

		//! drop every loaded language and clear the active selection
		void clear();
	protected:
	private:
	};
}

#endif //__StringTable_h__10_7_2026__12_00_00__
