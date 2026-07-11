/**************************************************************
	created:	2026/07/11 at 12:00
	filename: 	SaveStore.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __SaveStore_h__11_7_2026__12_00_00__
#define __SaveStore_h__11_7_2026__12_00_00__

#include "core_module/OrkigePrerequisites.h"
#include "core_util/Singleton.h"
#include "core_util/String.h"

#include <map>

namespace Orkige
{
	//! @brief the general per-project persistence store the Lua `save` table
	//! is a face on: a flat, typed key -> value map (Number / Bool / String)
	//! for settings, unlocks, high scores. Written ATOMICALLY (a temp file is
	//! rename()d over the real one, so a crash mid-write never corrupts the
	//! previous save) to the SAME writable directory the LevelManager
	//! progression save uses, under a distinct per-project file name so the two
	//! coexist.
	//! @remarks Values are FLAT and typed - there is no nested-table support by
	//! design: a game namespaces its keys ("hero.coins", "audio.music") instead.
	//! @remarks Crash semantics: only a flush() (autosave point) reaches disk.
	//! set() marks the store dirty but does NOT write; the player flushes on a
	//! clean shutdown. Changes made since the last flush are LOST on a hard
	//! crash - the always-on crash breadcrumb trail is the recovery aid for
	//! that window, not this store.
	//! @remarks ONLY a runtime that owns a SaveStore persists - the player
	//! creates one (tools/player/main.cpp), the editor never does, so the Lua
	//! `save` API is an honest no-op in edit mode (like the LevelManager /
	//! TweenManager). A missing/garbage/too-new file loads as an empty store.
	class ORKIGE_CORE_DLL SaveStore : public Singleton<SaveStore>
	{
		DECL_OSINGLETON(SaveStore);
		//--- Types -------------------------------------------
	public:
		//! the value shape a stored entry carries
		enum ValueKind
		{
			VK_NUMBER = 0,	//!< a double (scores, counters, tunables)
			VK_BOOL,		//!< a boolean (unlocks, flags)
			VK_STRING		//!< a UTF-8 string (names, chosen options)
		};
	private:
		//! one stored value: its kind plus the canonical string it round-trips
		//! as (numbers/bools are parsed back through the typed getters)
		struct Entry
		{
			ValueKind	kind;
			String		value;	//!< canonical string form (per the kind)
			Entry() : kind(VK_NUMBER) {}
			Entry(ValueKind k, String const & v) : kind(k), value(v) {}
		};
		//--- Variables ---------------------------------------
	public:
		static const String SAVE_FILE_MAGIC;	//!< "orkige.save"
		static const int SAVE_FORMAT_VERSION;	//!< 1
	private:
		std::map<String, Entry>	mEntries;	//!< the live key -> value map
		String					mSaveFile;	//!< save path ("" = no persistence)
		bool					mDirty;		//!< a set/remove happened since the last flush
		//--- Methods -----------------------------------------
	public:
		SaveStore();
		virtual ~SaveStore();

		//! set the save file path ("" disables persistence)
		void setSaveFile(String const & path) { mSaveFile = path; }
		//! @see SaveStore::setSaveFile
		String const & getSaveFile() const { return mSaveFile; }

		//--- typed writes (Lua: save.set dispatches on the value type) ---
		//! store a number under `key` (replaces any existing value)
		void setNumber(String const & key, double value);
		//! store a bool under `key`
		void setBool(String const & key, bool value);
		//! store a string under `key`
		void setString(String const & key, String const & value);

		//--- typed reads (Lua: save.getNumber / getBool / getString) ---
		//! read `key` as a number (fallback when absent or a different kind)
		double getNumber(String const & key, double fallback) const;
		//! read `key` as a bool (fallback when absent or a different kind)
		bool getBool(String const & key, bool fallback) const;
		//! read `key` as a string (fallback when absent; a Number/Bool value
		//! returns its canonical string form)
		String getString(String const & key, String const & fallback) const;

		//! is `key` present (any kind)
		bool has(String const & key) const;
		//! remove `key` (no-op when absent); marks the store dirty when it existed
		void remove(String const & key);
		//! drop every entry (does not touch the file until the next flush)
		void clear();
		//! number of stored entries (tests / diagnostics)
		size_t count() const { return mEntries.size(); }
		//! has a set/remove happened since the last successful flush/load
		bool isDirty() const { return mDirty; }

		//! @brief write the store to disk ATOMICALLY (temp file + rename). A
		//! no-op returning true when nothing changed since the last flush, and
		//! false (leaving the store dirty) when no save file is set or the write
		//! fails. Clears the dirty flag on success.
		bool flush();

		//! @brief load the store from disk, REPLACING the in-memory contents. A
		//! missing / garbage / too-new file is the honest "start empty" fallback
		//! (returns false without leaving a half-populated store); a clean load
		//! returns true and clears the dirty flag.
		bool load();
	protected:
	private:
		//! kind <-> canonical name for the on-disk record ("number"/"bool"/"string")
		static String kindName(ValueKind kind);
		static bool kindFromName(String const & name, ValueKind & outKind);
	};
}

#endif //__SaveStore_h__11_7_2026__12_00_00__
