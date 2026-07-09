/**************************************************************
	created:	2026/07/09 at 14:00
	filename: 	CVarManager.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "core_debug/CVarManager.h"

#include <cctype>
#include <cmath>
#include <cstdlib>

namespace Orkige
{
	IMPL_OSINGLETON_GETCREATE(CVarManager);
	//---------------------------------------------------------
	const String CVarManager::SETTING_PREFIX = "cvar.";
	//---------------------------------------------------------
	namespace
	{
		//! trim leading/trailing ASCII whitespace
		String trimmed(String const & text)
		{
			std::size_t begin = 0;
			std::size_t end = text.size();
			while (begin < end &&
				std::isspace(static_cast<unsigned char>(text[begin])))
			{
				++begin;
			}
			while (end > begin &&
				std::isspace(static_cast<unsigned char>(text[end - 1])))
			{
				--end;
			}
			return text.substr(begin, end - begin);
		}
		//---------------------------------------------------------
		String toLower(String const & text)
		{
			String out = text;
			for (char & c : out)
			{
				c = static_cast<char>(std::tolower(
					static_cast<unsigned char>(c)));
			}
			return out;
		}
	}
	//---------------------------------------------------------
	//--- CVar ------------------------------------------------
	//---------------------------------------------------------
	CVar::CVar()
		: type(CVarType::String)
		, flags(CVAR_NONE)
	{
	}
	//---------------------------------------------------------
	int CVar::asInt() const
	{
		return static_cast<int>(std::strtol(this->value.c_str(), NULL, 10));
	}
	//---------------------------------------------------------
	float CVar::asFloat() const
	{
		return std::strtof(this->value.c_str(), NULL);
	}
	//---------------------------------------------------------
	bool CVar::asBool() const
	{
		const String lowered = toLower(this->value);
		return lowered == "1" || lowered == "true" || lowered == "on" ||
			lowered == "yes";
	}
	//---------------------------------------------------------
	//--- CVarManager -----------------------------------------
	//---------------------------------------------------------
	CVarManager::CVarManager()
		: mInOnChange(false)
	{
	}
	//---------------------------------------------------------
	CVarManager::~CVarManager()
	{
	}
	//---------------------------------------------------------
	bool CVarManager::coerce(CVarType type, String const & value,
		String * outCanonical, String * outError)
	{
		const String text = trimmed(value);
		switch (type)
		{
		case CVarType::Int:
		{
			if (text.empty())
			{
				if (outError) { *outError = "expected an integer"; }
				return false;
			}
			char * end = NULL;
			const long parsed = std::strtol(text.c_str(), &end, 10);
			if (end != text.c_str() + text.size())
			{
				if (outError)
				{
					*outError = "'" + value + "' is not an integer";
				}
				return false;
			}
			if (outCanonical) { *outCanonical = cvarToString(int(parsed)); }
			return true;
		}
		case CVarType::Float:
		{
			if (text.empty())
			{
				if (outError) { *outError = "expected a number"; }
				return false;
			}
			char * end = NULL;
			const float parsed = std::strtof(text.c_str(), &end);
			// reject junk AND the non-finite spellings strtof accepts (nan/inf)
			// - a cvar value must be a real, usable number
			if (end != text.c_str() + text.size() || !std::isfinite(parsed))
			{
				if (outError)
				{
					*outError = "'" + value + "' is not a number";
				}
				return false;
			}
			if (outCanonical) { *outCanonical = cvarToString(parsed); }
			return true;
		}
		case CVarType::Bool:
		{
			const String lowered = toLower(text);
			if (lowered == "1" || lowered == "true" || lowered == "on" ||
				lowered == "yes")
			{
				if (outCanonical) { *outCanonical = "1"; }
				return true;
			}
			if (lowered == "0" || lowered == "false" || lowered == "off" ||
				lowered == "no")
			{
				if (outCanonical) { *outCanonical = "0"; }
				return true;
			}
			if (outError)
			{
				*outError = "'" + value +
					"' is not a boolean (1/0, true/false, on/off)";
			}
			return false;
		}
		case CVarType::String:
		default:
			// strings are never rejected (kept verbatim, not trimmed)
			if (outCanonical) { *outCanonical = value; }
			return true;
		}
	}
	//---------------------------------------------------------
	void CVarManager::applyAndNotify(CVar & cvar, String const & canonicalValue)
	{
		cvar.value = canonicalValue;
		if (!cvar.onChange || this->mInOnChange)
		{
			// re-entrancy guard: an onChange that sets ANOTHER cvar (or the
			// same one) must not recurse into a second notify storm - the
			// value is already stored, the outer notify covers the settle
			return;
		}
		this->mInOnChange = true;
		cvar.onChange(cvar);
		this->mInOnChange = false;
	}
	//---------------------------------------------------------
	CVar * CVarManager::registerCVar(String const & name, CVarType type,
		String const & defaultValue, int flags, String const & description,
		CVar::ChangeCallback onChange)
	{
		String canonicalDefault;
		// a default that does not parse is a programmer error - fall back to a
		// type-safe zero-value rather than storing garbage
		if (!coerce(type, defaultValue, &canonicalDefault, NULL))
		{
			switch (type)
			{
			case CVarType::Int:		canonicalDefault = "0"; break;
			case CVarType::Float:	canonicalDefault = "0"; break;
			case CVarType::Bool:	canonicalDefault = "0"; break;
			case CVarType::String:
			default:				canonicalDefault = defaultValue; break;
			}
		}

		std::map<String, CVar>::iterator it = this->mCVars.find(name);
		if (it != this->mCVars.end())
		{
			// idempotent re-registration: keep the EXISTING value (a manifest
			// override applied before the owning system registered must
			// survive), refresh the default/description and (re)wire onChange,
			// then re-apply so the newly wired hook sees the current value
			CVar & existing = it->second;
			existing.defaultValue = canonicalDefault;
			if (!description.empty())
			{
				existing.description = description;
			}
			existing.flags |= flags;
			if (onChange)
			{
				existing.onChange = onChange;
				applyAndNotify(existing, existing.value);
			}
			return &existing;
		}

		CVar cvar;
		cvar.name = name;
		cvar.type = type;
		cvar.defaultValue = canonicalDefault;
		cvar.description = description;
		cvar.flags = flags;
		cvar.onChange = onChange;
		cvar.value = canonicalDefault;
		// a manifest override captured before this registration wins over the
		// default (order-independence, see applySettings); a bad held value is
		// silently ignored - the default stands
		std::map<String, String>::iterator pending =
			this->mPendingOverrides.find(name);
		if (pending != this->mPendingOverrides.end())
		{
			String canonicalOverride;
			if (coerce(type, pending->second, &canonicalOverride, NULL))
			{
				cvar.value = canonicalOverride;
			}
			this->mPendingOverrides.erase(pending);
		}
		CVar & stored = this->mCVars[name] = cvar;
		// fire onChange once with the initial value (the live re-apply seam is
		// driven the same way for the default as for a later set)
		if (stored.onChange)
		{
			applyAndNotify(stored, stored.value);
		}
		return &stored;
	}
	//---------------------------------------------------------
	bool CVarManager::exists(String const & name) const
	{
		return this->mCVars.find(name) != this->mCVars.end();
	}
	//---------------------------------------------------------
	CVar * CVarManager::find(String const & name)
	{
		std::map<String, CVar>::iterator it = this->mCVars.find(name);
		return (it == this->mCVars.end()) ? NULL : &it->second;
	}
	//---------------------------------------------------------
	CVar const * CVarManager::find(String const & name) const
	{
		std::map<String, CVar>::const_iterator it = this->mCVars.find(name);
		return (it == this->mCVars.end()) ? NULL : &it->second;
	}
	//---------------------------------------------------------
	int CVarManager::getInt(String const & name, int fallback) const
	{
		CVar const * cvar = find(name);
		return cvar ? cvar->asInt() : fallback;
	}
	//---------------------------------------------------------
	float CVarManager::getFloat(String const & name, float fallback) const
	{
		CVar const * cvar = find(name);
		return cvar ? cvar->asFloat() : fallback;
	}
	//---------------------------------------------------------
	bool CVarManager::getBool(String const & name, bool fallback) const
	{
		CVar const * cvar = find(name);
		return cvar ? cvar->asBool() : fallback;
	}
	//---------------------------------------------------------
	String CVarManager::getString(String const & name,
		String const & fallback) const
	{
		CVar const * cvar = find(name);
		return cvar ? cvar->value : fallback;
	}
	//---------------------------------------------------------
	bool CVarManager::setString(String const & name, String const & value,
		String * outError)
	{
		CVar * cvar = find(name);
		if (!cvar)
		{
			if (outError) { *outError = "unknown cvar '" + name + "'"; }
			return false;
		}
		if (cvar->hasFlag(CVAR_READONLY))
		{
			if (outError)
			{
				*outError = "cvar '" + name + "' is read-only";
			}
			return false;
		}
		String canonical;
		if (!coerce(cvar->type, value, &canonical, outError))
		{
			return false;
		}
		applyAndNotify(*cvar, canonical);
		return true;
	}
	//---------------------------------------------------------
	bool CVarManager::reset(String const & name, String * outError)
	{
		CVar * cvar = find(name);
		if (!cvar)
		{
			if (outError) { *outError = "unknown cvar '" + name + "'"; }
			return false;
		}
		if (cvar->hasFlag(CVAR_READONLY))
		{
			if (outError)
			{
				*outError = "cvar '" + name + "' is read-only";
			}
			return false;
		}
		applyAndNotify(*cvar, cvar->defaultValue);
		return true;
	}
	//---------------------------------------------------------
	StringVector CVarManager::findByPrefix(String const & prefix) const
	{
		StringVector names;
		for (std::map<String, CVar>::const_iterator it = this->mCVars.begin(),
			itend = this->mCVars.end(); it != itend; ++it)
		{
			if (prefix.empty() ||
				it->first.compare(0, prefix.size(), prefix) == 0)
			{
				names.push_back(it->first);
			}
		}
		return names; // the map is sorted, so the result is too
	}
	//---------------------------------------------------------
	void CVarManager::applySettings(std::map<String, String> const & settings)
	{
		for (std::map<String, String>::const_iterator it = settings.begin(),
			itend = settings.end(); it != itend; ++it)
		{
			if (it->first.compare(0, SETTING_PREFIX.size(), SETTING_PREFIX)
				!= 0 || it->first.size() <= SETTING_PREFIX.size())
			{
				continue;
			}
			const String name = it->first.substr(SETTING_PREFIX.size());
			if (exists(name))
			{
				// registered already: apply live (onChange fires). A bad value
				// is dropped honestly - the manifest is tool-written.
				setString(name, it->second, NULL);
			}
			else
			{
				// not registered yet: hold the override for registerCVar to
				// pick up (order-independence)
				this->mPendingOverrides[name] = it->second;
			}
		}
	}
	//---------------------------------------------------------
	void CVarManager::collectPersisted(
		std::map<String, String> & settings) const
	{
		for (std::map<String, CVar>::const_iterator it = this->mCVars.begin(),
			itend = this->mCVars.end(); it != itend; ++it)
		{
			CVar const & cvar = it->second;
			if (!cvar.hasFlag(CVAR_PERSIST))
			{
				continue;
			}
			const String key = SETTING_PREFIX + cvar.name;
			if (cvar.value != cvar.defaultValue)
			{
				settings[key] = cvar.value;
			}
			else
			{
				// back at the default: drop a previously-persisted override so
				// the manifest does not pin the (now stale) old value
				settings.erase(key);
			}
		}
	}
	//---------------------------------------------------------
	//--- CVarStaticInit --------------------------------------
	//---------------------------------------------------------
	CVarStaticInit::CVarStaticInit(String const & name, CVarType type,
		String const & defaultValue, int flags, String const & description)
	{
		// getSingleton auto-creates the manager on first touch, so this is
		// safe at static-init time regardless of translation-unit order
		CVarManager::getSingleton().registerCVar(name, type, defaultValue,
			flags, description);
	}
	//---------------------------------------------------------
}
