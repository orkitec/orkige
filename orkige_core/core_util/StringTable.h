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
//! loaded from plain text, with %%0%% positional formatting. No renderer or
//! Ogre dependency (unlike the classic engine_base/Localisation, which stays
//! Ogre::ConfigFile-bound) - games reach it through the Lua loc() accessor and
//! the player loads a project's file via the config-asset convention.

#include "core_module/OrkigePrerequisites.h"
#include "core_util/String.h"
#include "core_util/Singleton.h"

#include <map>

namespace Orkige
{
	//! @brief a set of language string tables plus the active language.
	//! @remarks Singleton so the Lua loc() accessor and games share one table;
	//! the app constructs it (like InputActionMap). A miss returns the key
	//! itself, so an unlocalised build shows readable placeholder text.
	class ORKIGE_CORE_DLL StringTable : public Singleton<StringTable>
	{
		DECL_OSINGLETON_ORKIGE_CORE_DLL(StringTable);
		//--- Types -------------------------------------------------
	public:
		//! manifest Settings key naming the project's localisation file
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
		//--- Methods -----------------------------------------------
	public:
		StringTable();
		~StringTable();

		//! @brief parse a localisation file with `[lang]` section headers and
		//! `key = value` lines (# / ; comments and blank lines ignored). The
		//! first loaded language becomes active if none is set yet. Data merges
		//! into any already loaded tables. Returns false if the file cannot be
		//! opened.
		bool loadFile(String const & filePath);
		//! @brief load one language's `key = value` pairs from a headerless
		//! file into the given language code. Returns false if unreadable.
		bool loadLanguage(String const & language, String const & filePath);
		//! @brief add / overwrite a single entry (used by loaders and tests)
		void set(String const & language, String const & key,
			String const & value);

		//! @brief set the active language; unknown codes are accepted (get then
		//! always returns the key) so a missing translation never asserts
		void setLanguage(String const & language);
		//! the active language code ("" when nothing has been loaded/selected)
		String const & getLanguage() const { return this->mLanguage; }
		//! is a table for that language present?
		bool hasLanguage(String const & language) const;

		//! @brief the localized string for a key in the active language; the
		//! key itself on a miss (so untranslated UI stays readable)
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
		//! parse `key = value` into the given language table (shared by the two
		//! loaders); returns the number of entries added
		unsigned int parseInto(String const & language,
			std::istream & stream);
	};
}

#endif //__StringTable_h__10_7_2026__12_00_00__
