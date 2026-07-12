/**************************************************************
	created:	2026/07/08 at 10:00
	filename: 	ScriptRuntime.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "core_script/ScriptRuntime.h"
#include "core_script/ScriptEventBus.h"
#include "core_script/ScriptEventPayload.h"

#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include <utility>

namespace Orkige
{
	IMPL_OSINGLETON(ScriptRuntime)
#ifdef ORKIGE_LUA
	namespace
	{
		//! walk a global path like {"shared","jumper","wins"} to its value
		//! (nil when any step is missing or not a table)
		sol::object resolveGlobalPath(sol::state & lua, StringVector const & path)
		{
			if (path.empty())
			{
				return sol::object(sol::lua_nil);
			}
			sol::object current = lua[path[0]];
			for (std::size_t i = 1; i < path.size(); ++i)
			{
				if (!current.is<sol::table>())
				{
					return sol::object(sol::lua_nil);
				}
				current = current.as<sol::table>()[path[i]];
			}
			return current;
		}
	}
#endif //ORKIGE_LUA
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	ScriptRuntime::ScriptRuntime()
	{
	}
	//---------------------------------------------------------
	ScriptRuntime::~ScriptRuntime()
	{
		// release every held script callback (event-bus subscriptions) WHILE the
		// Lua state is still open: the ScriptManager member below is destroyed
		// AFTER this body runs, so a subscription's sol reference would otherwise
		// be freed against a closed state. Owner-scoped subs normally clear as
		// their sandboxes retire; this sweeps any owner-less (console) ones too.
		ScriptEventBus::getSingleton().clear();
	}
	//---------------------------------------------------------
	bool ScriptRuntime::available()
	{
#ifdef ORKIGE_LUA
		return ScriptRuntime::getSingletonPtr() != NULL;
#else
		return false;
#endif
	}
	//---------------------------------------------------------
	char const * ScriptRuntime::backendName()
	{
#ifdef ORKIGE_LUA
		return "Lua";
#else
		return "none";
#endif
	}
	//---------------------------------------------------------
	String ScriptRuntime::resolveScriptPath(String const & scriptPath) const
	{
		if (scriptPath.empty())
		{
			return "";
		}
		std::error_code ignored;
		const std::filesystem::path path(scriptPath);
		if (path.is_absolute())
		{
			return std::filesystem::is_regular_file(path, ignored) ? scriptPath : "";
		}
		if (!this->scriptSearchRoot.empty())
		{
			const std::filesystem::path rooted =
				std::filesystem::path(this->scriptSearchRoot) / path;
			if (std::filesystem::is_regular_file(rooted, ignored))
			{
				return rooted.string();
			}
		}
		// working-directory fallback: keeps bare-scene runs (no project) and
		// tests working with cwd-relative script paths
		if (std::filesystem::is_regular_file(path, ignored))
		{
			return scriptPath;
		}
		return "";
	}
	//---------------------------------------------------------
	ScriptRuntime::Result ScriptRuntime::runString(String const & code)
	{
		Result result;
#ifdef ORKIGE_LUA
		sol::state & lua = this->luaManager.state();
		const sol::protected_function_result scriptResult =
			lua.safe_script(code, sol::script_pass_on_error);
		if (!scriptResult.valid())
		{
			const sol::error error = scriptResult;
			result.error = error.what();
			return result;
		}
		result.success = true;
		for (int i = 0; i < scriptResult.return_count(); ++i)
		{
			const String value =
				lua["tostring"](scriptResult.get<sol::object>(i));
			result.returnValues.push_back(value);
		}
#else
		(void)code;
		result.error = ScriptRuntime::disabledError();
#endif
		return result;
	}
	//---------------------------------------------------------
	optr<ScriptInstance> ScriptRuntime::loadScriptInstance(
		String const & scriptFile, String * outError)
	{
		oAssert(outError);
#ifdef ORKIGE_LUA
		const String resolvedPath = this->resolveScriptPath(scriptFile);
		if (resolvedPath.empty())
		{
			*outError = "script file not found (searched the project root '" +
				this->scriptSearchRoot + "' and the working directory)";
			return optr<ScriptInstance>();
		}
		sol::state & lua = this->luaManager.state();
		optr<ScriptInstance> instance(new ScriptInstance());
		// per-instance sandbox: a fresh table whose reads fall through to the
		// real globals - writes stay in the instance (isolation between
		// instances; deliberate sharing goes through the `shared` table)
		instance->environment = sol::environment(lua, sol::create, lua.globals());
		const sol::protected_function_result loadResult = lua.safe_script_file(
			resolvedPath, instance->environment, sol::script_pass_on_error);
		if (!loadResult.valid())
		{
			const sol::error error = loadResult;
			*outError = error.what();
			return optr<ScriptInstance>();
		}
		instance->selfTable = lua.create_table();
		return instance;
#else
		(void)scriptFile;
		*outError = ScriptRuntime::disabledError();
		return optr<ScriptInstance>();
#endif
	}
	//---------------------------------------------------------
#ifdef ORKIGE_LUA
	namespace
	{
		//! read a 0..3 numeric field of a Lua array table (1-based), default 0
		float luaArrayFloat(sol::table const & table, int index, float fallback)
		{
			const sol::object value = table[index];
			return value.is<double>()
				? static_cast<float>(value.as<double>()) : fallback;
		}
		//! @brief translate ONE `properties` entry (a sub-table) into a neutral
		//! ScriptExportProperty. Returns false (the entry is skipped) when the
		//! `type` string is missing or unknown - an unknown export type is
		//! ignored, never a hard error, so a typo does not brick a whole script.
		bool readExportEntry(String const & name, sol::table const & spec,
			ScriptExportProperty & outProperty)
		{
			const sol::object typeObject = spec["type"];
			if (!typeObject.is<String>())
			{
				return false;
			}
			const String typeName = typeObject.as<String>();
			outProperty.name = name;
			if (typeName == "number" || typeName == "float" || typeName == "int")
			{
				outProperty.kind = PropertyKind::Float;
				const sol::object def = spec["default"];
				outProperty.defaultValue = PropertyValue::makeFloat(
					def.is<double>() ? def.as<double>() : 0.0);
			}
			else if (typeName == "bool" || typeName == "boolean")
			{
				outProperty.kind = PropertyKind::Bool;
				const sol::object def = spec["default"];
				outProperty.defaultValue = PropertyValue::makeBool(
					def.is<bool>() ? def.as<bool>() : false);
			}
			else if (typeName == "string")
			{
				outProperty.kind = PropertyKind::String;
				const sol::object def = spec["default"];
				outProperty.defaultValue = PropertyValue::makeString(
					def.is<String>() ? def.as<String>() : String());
			}
			else if (typeName == "vec3")
			{
				outProperty.kind = PropertyKind::Vec3;
				PropVec3 vec;
				const sol::object def = spec["default"];
				if (def.is<sol::table>())
				{
					const sol::table t = def.as<sol::table>();
					vec.x = luaArrayFloat(t, 1, 0.0f);
					vec.y = luaArrayFloat(t, 2, 0.0f);
					vec.z = luaArrayFloat(t, 3, 0.0f);
				}
				outProperty.defaultValue = PropertyValue::makeVec3(vec);
			}
			else if (typeName == "color")
			{
				outProperty.kind = PropertyKind::Color;
				PropColor col;
				const sol::object def = spec["default"];
				if (def.is<sol::table>())
				{
					const sol::table t = def.as<sol::table>();
					col.r = luaArrayFloat(t, 1, 0.0f);
					col.g = luaArrayFloat(t, 2, 0.0f);
					col.b = luaArrayFloat(t, 3, 0.0f);
					col.a = luaArrayFloat(t, 4, 1.0f);
				}
				outProperty.defaultValue = PropertyValue::makeColor(col);
			}
			else if (typeName == "asset")
			{
				outProperty.kind = PropertyKind::AssetRef;
				const sol::object kind = spec["kind"];
				outProperty.referenceHint =
					kind.is<String>() ? kind.as<String>() : String();
				const sol::object def = spec["default"];
				outProperty.defaultValue = PropertyValue::makeAssetRef(
					outProperty.referenceHint,
					def.is<String>() ? def.as<String>() : String());
			}
			else if (typeName == "object")
			{
				outProperty.kind = PropertyKind::ObjectRef;
				const sol::object kind = spec["kind"];
				outProperty.referenceHint =
					kind.is<String>() ? kind.as<String>() : String();
				const sol::object def = spec["default"];
				outProperty.defaultValue = PropertyValue::makeObjectRef(
					outProperty.referenceHint,
					def.is<String>() ? def.as<String>() : String());
			}
			else
			{
				return false;	// unknown export type - skip it
			}
			// optional slider range (numeric kinds); harmless on the others
			const sol::object minObject = spec["min"];
			const sol::object maxObject = spec["max"];
			if (minObject.is<double>() || maxObject.is<double>())
			{
				outProperty.hasRange = true;
				outProperty.minValue = minObject.is<double>()
					? static_cast<float>(minObject.as<double>()) : 0.0f;
				outProperty.maxValue = maxObject.is<double>()
					? static_cast<float>(maxObject.as<double>()) : 0.0f;
			}
			return true;
		}
	}
#endif //ORKIGE_LUA
	//---------------------------------------------------------
	std::vector<ScriptExportProperty> ScriptRuntime::readExportedProperties(
		String const & scriptFile)
	{
		std::vector<ScriptExportProperty> properties;
#ifdef ORKIGE_LUA
		const String resolvedPath = this->resolveScriptPath(scriptFile);
		if (resolvedPath.empty())
		{
			return properties;	// no file -> no exports (honest, not an error)
		}
		sol::state & lua = this->luaManager.state();
		// a throwaway sandbox: reads fall through to _G, top-level runs to
		// populate `properties`, but we never call init/update. A parse/run
		// error just means "no exports discovered" - a broken script's export
		// set is empty, its load failure surfaces later through the component.
		sol::environment probe(lua, sol::create, lua.globals());
		const sol::protected_function_result loadResult = lua.safe_script_file(
			resolvedPath, probe, sol::script_pass_on_error);
		if (!loadResult.valid())
		{
			return properties;
		}
		const sol::object propertiesObject = probe["properties"];
		if (!propertiesObject.is<sol::table>())
		{
			return properties;	// no `properties` table declared
		}
		// Lua tables are unordered; sort by name so the schema (and therefore
		// the inspector order + the serialized field order) is DETERMINISTIC.
		const sol::table table = propertiesObject.as<sol::table>();
		std::vector<std::pair<String, sol::table> > entries;
		table.for_each([&entries](sol::object const & key, sol::object const & value)
		{
			if (key.is<String>() && value.is<sol::table>())
			{
				entries.push_back({ key.as<String>(), value.as<sol::table>() });
			}
		});
		std::sort(entries.begin(), entries.end(),
			[](std::pair<String, sol::table> const & a,
				std::pair<String, sol::table> const & b)
			{
				return a.first < b.first;
			});
		for (auto const & entry : entries)
		{
			ScriptExportProperty property;
			if (readExportEntry(entry.first, entry.second, property))
			{
				properties.push_back(property);
			}
		}
#else
		(void)scriptFile;
#endif
		return properties;
	}
	//---------------------------------------------------------
	double ScriptRuntime::getNumber(StringVector const & path, double fallback)
	{
#ifdef ORKIGE_LUA
		const sol::object value =
			resolveGlobalPath(this->luaManager.state(), path);
		return value.is<double>() ? value.as<double>() : fallback;
#else
		(void)path;
		return fallback;
#endif
	}
	//---------------------------------------------------------
	bool ScriptRuntime::getBool(StringVector const & path, bool fallback)
	{
#ifdef ORKIGE_LUA
		const sol::object value =
			resolveGlobalPath(this->luaManager.state(), path);
		return value.is<bool>() ? value.as<bool>() : fallback;
#else
		(void)path;
		return fallback;
#endif
	}
	//---------------------------------------------------------
	String ScriptRuntime::getString(StringVector const & path,
		String const & fallback)
	{
#ifdef ORKIGE_LUA
		const sol::object value =
			resolveGlobalPath(this->luaManager.state(), path);
		return value.is<String>() ? value.as<String>() : fallback;
#else
		(void)path;
		return fallback;
#endif
	}
	//---------------------------------------------------------
	bool ScriptRuntime::hasGlobalTable(String const & name)
	{
#ifdef ORKIGE_LUA
		sol::state & lua = this->luaManager.state();
		return lua[name].is<sol::table>();
#else
		(void)name;
		return false;
#endif
	}
	//---------------------------------------------------------
	void ScriptRuntime::ensureGlobalTable(String const & name)
	{
#ifdef ORKIGE_LUA
		sol::state & lua = this->luaManager.state();
		if (!lua[name].is<sol::table>())
		{
			lua[name] = lua.create_table();
		}
#else
		(void)name;
#endif
	}
	//---------------------------------------------------------
	double ScriptRuntime::numberArg(ScriptArgs const & args, int index,
		double fallback)
	{
#ifdef ORKIGE_LUA
		if(index < 0 || static_cast<std::size_t>(index) >= args.size())
		{
			return fallback;
		}
		const sol::object value = args.get<sol::object>(index);
		return value.is<double>() ? value.as<double>() : fallback;
#else
		(void)args;
		(void)index;
		return fallback;
#endif
	}
	//---------------------------------------------------------
	String ScriptRuntime::stringArg(ScriptArgs const & args, int index,
		String const & fallback)
	{
#ifdef ORKIGE_LUA
		if(index < 0 || static_cast<std::size_t>(index) >= args.size())
		{
			return fallback;
		}
		const sol::object value = args.get<sol::object>(index);
		return value.is<String>() ? value.as<String>() : fallback;
#else
		(void)args;
		(void)index;
		return fallback;
#endif
	}
	//---------------------------------------------------------
	bool ScriptRuntime::boolArg(ScriptArgs const & args, int index,
		bool fallback)
	{
#ifdef ORKIGE_LUA
		if(index < 0 || static_cast<std::size_t>(index) >= args.size())
		{
			return fallback;
		}
		const sol::object value = args.get<sol::object>(index);
		return value.is<bool>() ? value.as<bool>() : fallback;
#else
		(void)args;
		(void)index;
		return fallback;
#endif
	}
	//---------------------------------------------------------
	ScriptRuntime::ArgType ScriptRuntime::argType(ScriptArgs const & args,
		int index)
	{
#ifdef ORKIGE_LUA
		if(index < 0 || static_cast<std::size_t>(index) >= args.size())
		{
			return AT_ABSENT;
		}
		const sol::object value = args.get<sol::object>(index);
		// bool BEFORE number: Lua booleans and numbers are distinct types, and a
		// value that is a boolean must not be mistaken for a 0/1 number
		if(value.is<bool>())	{ return AT_BOOL; }
		if(value.is<double>())	{ return AT_NUMBER; }
		if(value.is<String>())	{ return AT_STRING; }
		return AT_OTHER;
#else
		(void)args;
		(void)index;
		return AT_ABSENT;
#endif
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------

	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	String ScriptRuntime::disabledError()
	{
		return "scripting disabled (built with ORKIGE_SCRIPTING=OFF)";
	}
	//---------------------------------------------------------
	//--- ScriptCallback --------------------------------------
	//---------------------------------------------------------
	ScriptCallback::ScriptCallback()
	{
	}
	//---------------------------------------------------------
	ScriptCallback ScriptCallback::fromArgs(ScriptArgs const & args, int index)
	{
		ScriptCallback callback;
#ifdef ORKIGE_LUA
		if(index >= 0 && static_cast<std::size_t>(index) < args.size())
		{
			const sol::object value = args.get<sol::object>(index);
			if(value.is<sol::protected_function>())
			{
				callback.mFunction = value.as<sol::protected_function>();
			}
		}
#else
		(void)args;
		(void)index;
#endif
		return callback;
	}
	//---------------------------------------------------------
	bool ScriptCallback::valid() const
	{
#ifdef ORKIGE_LUA
		return this->mFunction.valid();
#else
		return false;
#endif
	}
	//---------------------------------------------------------
	bool ScriptCallback::invoke(String * outError) const
	{
		oAssert(outError);
#ifdef ORKIGE_LUA
		if(!this->mFunction.valid())
		{
			return true;	// nothing to call - not an error
		}
		const sol::protected_function_result result = this->mFunction();
		if(!result.valid())
		{
			const sol::error error = result;
			*outError = error.what();
			return false;
		}
		return true;
#else
		*outError = "scripting disabled (built with ORKIGE_SCRIPTING=OFF)";
		return false;
#endif
	}
	//---------------------------------------------------------
	bool ScriptCallback::invokeNumbers(float const * values, int count,
		bool * outRequestedStop, String * outError) const
	{
		oAssert(outError);
		if(outRequestedStop)
		{
			*outRequestedStop = false;
		}
#ifdef ORKIGE_LUA
		if(!this->mFunction.valid())
		{
			return true;	// nothing to call - not an error
		}
		sol::protected_function_result result;
		switch(count)
		{
		case 1:  result = this->mFunction(values[0]); break;
		case 2:  result = this->mFunction(values[0], values[1]); break;
		case 3:  result = this->mFunction(values[0], values[1], values[2]); break;
		default: result = this->mFunction(values[0], values[1], values[2],
					values[3]); break;
		}
		if(!result.valid())
		{
			const sol::error error = result;
			*outError = error.what();
			return false;
		}
		// the documented cancel channel: an explicit `return false`
		if(outRequestedStop && result.return_count() > 0)
		{
			const sol::object returned = result.get<sol::object>(0);
			if(returned.is<bool>() && !returned.as<bool>())
			{
				*outRequestedStop = true;
			}
		}
		return true;
#else
		(void)values;
		(void)count;
		*outError = "scripting disabled (built with ORKIGE_SCRIPTING=OFF)";
		return false;
#endif
	}
	//---------------------------------------------------------
	//--- event-bus payload <-> Lua conversion ----------------
	//---------------------------------------------------------
#ifdef ORKIGE_LUA
	namespace
	{
		//! set a bounded scalar on a sol table under a string OR array-index key
		//! (a nil scalar is simply left absent)
		void payloadSetScalar(sol::table & table, ScriptEventKey const & key,
			ScriptEventScalar const & scalar)
		{
			switch (scalar.kind)
			{
			case ScriptEventScalar::Kind::Bool:
				if (key.isIndex) { table[key.index] = scalar.boolValue; }
				else { table[key.name] = scalar.boolValue; }
				break;
			case ScriptEventScalar::Kind::Number:
				if (key.isIndex) { table[key.index] = scalar.numberValue; }
				else { table[key.name] = scalar.numberValue; }
				break;
			case ScriptEventScalar::Kind::String:
				if (key.isIndex) { table[key.index] = scalar.stringValue; }
				else { table[key.name] = scalar.stringValue; }
				break;
			case ScriptEventScalar::Kind::Nil:
			default:
				break;
			}
		}
		//! build the Lua table a handler receives from a bounded payload
		sol::table payloadToLuaTable(sol::state_view lua,
			ScriptEventPayload const & payload)
		{
			sol::table table = lua.create_table();
			for (std::pair<ScriptEventKey, ScriptEventField> const & entry :
				payload.fields)
			{
				if (!entry.second.isTable)
				{
					payloadSetScalar(table, entry.first, entry.second.scalar);
					continue;
				}
				sol::table nested = lua.create_table();
				for (std::pair<ScriptEventKey, ScriptEventScalar> const & sub :
					entry.second.table)
				{
					payloadSetScalar(nested, sub.first, sub.second);
				}
				if (entry.first.isIndex) { table[entry.first.index] = nested; }
				else { table[entry.first.name] = nested; }
			}
			return table;
		}
		//! read a Lua value as a bounded scalar; false when it is not a
		//! string/number/bool (the caller then rejects or recurses one level)
		bool luaValueToScalar(sol::object const & value, ScriptEventScalar * out)
		{
			switch (value.get_type())
			{
			case sol::type::boolean:
				*out = ScriptEventScalar::makeBool(value.as<bool>());
				return true;
			case sol::type::number:
				*out = ScriptEventScalar::makeNumber(value.as<double>());
				return true;
			case sol::type::string:
				*out = ScriptEventScalar::makeString(value.as<String>());
				return true;
			default:
				return false;
			}
		}
		//! read a Lua key as a bounded key (string name or array index); false
		//! when it is neither
		bool luaKeyToEventKey(sol::object const & key, ScriptEventKey * out,
			String * error, char const * context)
		{
			if (key.get_type() == sol::type::number)
			{
				*out = ScriptEventKey::indexed(
					static_cast<long long>(key.as<double>()));
				return true;
			}
			if (key.get_type() == sol::type::string)
			{
				*out = ScriptEventKey::named(key.as<String>());
				return true;
			}
			*error = String(context) + " key must be a string or an array index";
			return false;
		}
		//! @brief bounded-convert trailing argument #index (the emit payload) into
		//! a ScriptEventPayload. Absent/nil = an empty event (true). A non-table,
		//! a value that is not string/number/bool, or a table nested deeper than
		//! one level is rejected with *error (false) - the honest failure at emit.
		bool luaTableToPayload(ScriptArgs const & args, int index,
			ScriptEventPayload * out, String * error)
		{
			if (index < 0 || static_cast<std::size_t>(index) >= args.size())
			{
				return true;	// no payload argument - an empty event
			}
			const sol::object value = args.get<sol::object>(index);
			if (value.get_type() == sol::type::nil)
			{
				return true;	// explicit nil - an empty event
			}
			if (value.get_type() != sol::type::table)
			{
				*error = "the payload must be a table";
				return false;
			}
			const sol::table table = value.as<sol::table>();
			for (std::pair<sol::object, sol::object> const & kv : table)
			{
				ScriptEventKey key;
				if (!luaKeyToEventKey(kv.first, &key, error, "a payload"))
				{
					return false;
				}
				ScriptEventField field;
				ScriptEventScalar scalar;
				if (luaValueToScalar(kv.second, &scalar))
				{
					field.isTable = false;
					field.scalar = scalar;
				}
				else if (kv.second.get_type() == sol::type::table)
				{
					// exactly one nesting level: every nested value must be scalar
					field.isTable = true;
					const sol::table nested = kv.second.as<sol::table>();
					for (std::pair<sol::object, sol::object> const & nkv : nested)
					{
						ScriptEventKey nestedKey;
						if (!luaKeyToEventKey(nkv.first, &nestedKey, error,
							"a nested payload"))
						{
							return false;
						}
						ScriptEventScalar nestedScalar;
						if (!luaValueToScalar(nkv.second, &nestedScalar))
						{
							*error = "a payload value nests deeper than one level "
								"or is not a string/number/bool";
							return false;
						}
						field.table.push_back(
							std::make_pair(nestedKey, nestedScalar));
					}
				}
				else
				{
					*error = "a payload value must be a string, number, bool or "
						"a table of those";
					return false;
				}
				out->fields.push_back(std::make_pair(key, field));
			}
			return true;
		}
	}
#endif
	//---------------------------------------------------------
	bool ScriptCallback::invokePayload(ScriptEventPayload const & payload,
		String * outError) const
	{
		oAssert(outError);
#ifdef ORKIGE_LUA
		if (!this->mFunction.valid())
		{
			return true;	// nothing to call - not an error
		}
		sol::state_view lua(this->mFunction.lua_state());
		const sol::table table = payloadToLuaTable(lua, payload);
		const sol::protected_function_result result = this->mFunction(table);
		if (!result.valid())
		{
			const sol::error error = result;
			*outError = error.what();
			return false;
		}
		return true;
#else
		(void)payload;
		*outError = "scripting disabled (built with ORKIGE_SCRIPTING=OFF)";
		return false;
#endif
	}
	//---------------------------------------------------------
	void ScriptRuntime::emitEventFromScript(String const & name,
		ScriptArgs const & args)
	{
#ifdef ORKIGE_LUA
		ScriptEventPayload payload;
		String error;
		if (!luaTableToPayload(args, 0, &payload, &error))
		{
			// raised at the emit call site (sol turns the exception into a Lua
			// error): the script author learns immediately their payload is
			// out of the bus's bounds
			throw std::runtime_error("events.emit('" + name + "'): " + error);
		}
		ScriptEventBus::getSingleton().emit(name, payload);
#else
		(void)name;
		(void)args;
#endif
	}
	//---------------------------------------------------------
	//--- ScriptCallScope -------------------------------------
	//---------------------------------------------------------
	ScriptCallScope::ScriptCallScope(void const * owner)
	{
		ScriptEventBus & bus = ScriptEventBus::getSingleton();
		this->mPrevious = bus.currentOwner();
		bus.setCurrentOwner(owner);
	}
	//---------------------------------------------------------
	ScriptCallScope::~ScriptCallScope()
	{
		ScriptEventBus::getSingleton().setCurrentOwner(this->mPrevious);
	}
	//---------------------------------------------------------
	//--- ScriptInstance --------------------------------------
	//---------------------------------------------------------
	ScriptInstance::ScriptInstance()
	{
	}
	//---------------------------------------------------------
	ScriptInstance::~ScriptInstance()
	{
		// retire this sandbox's event-bus subscriptions FIRST, while its Lua
		// environment is still valid - dropping the held callbacks here (not at
		// the process-exit static teardown) is what makes "destroying or
		// hot-reloading a script component auto-cancels its subscriptions" true
		ScriptEventBus::getSingleton().cancelOwner(this);
#ifdef ORKIGE_LUA
		// deterministic release of everything the instance kept alive: engine
		// objects held by script locals (a Lua-booted GuiManager and its
		// widgets, for example) must run their C++ destructors NOW - while the
		// engine is still up - not at lua_close, which happens after the
		// engine has been torn down. Drop the instance's references first,
		// then run two full GC cycles (userdata finalization can take two).
		if (this->environment.valid())
		{
			lua_State* luaState = this->environment.lua_state();
			this->updateFunction = sol::protected_function();
			this->selfTable = sol::table();
			this->environment = sol::environment();
			lua_gc(luaState, LUA_GCCOLLECT, 0);
			lua_gc(luaState, LUA_GCCOLLECT, 0);
		}
#endif
	}
	//---------------------------------------------------------
	void ScriptInstance::setSelfProperty(char const * key,
		PropertyValue const & value)
	{
#ifdef ORKIGE_LUA
		switch (value.kind())
		{
		case PropertyKind::Int:
		case PropertyKind::Enum:
			this->selfTable[key] = value.asInt();
			break;
		case PropertyKind::Float:
			this->selfTable[key] = value.asFloat();
			break;
		case PropertyKind::Bool:
			this->selfTable[key] = value.asBool();
			break;
		case PropertyKind::String:
		case PropertyKind::AssetRef:
		case PropertyKind::ObjectRef:
			this->selfTable[key] = value.asString();
			break;
		case PropertyKind::Vec3:
		{
			const PropVec3 vec = value.asVec3();
			this->selfTable[key] = this->selfTable.lua_state() ?
				sol::state_view(this->selfTable.lua_state()).create_table_with(
					1, vec.x, 2, vec.y, 3, vec.z) : sol::lua_nil;
			break;
		}
		case PropertyKind::Color:
		{
			const PropColor col = value.asColor();
			this->selfTable[key] = this->selfTable.lua_state() ?
				sol::state_view(this->selfTable.lua_state()).create_table_with(
					1, col.r, 2, col.g, 3, col.b, 4, col.a) : sol::lua_nil;
			break;
		}
		default:
			break;
		}
#else
		(void)key;
		(void)value;
#endif
	}
	//---------------------------------------------------------
	bool ScriptInstance::callInit(String * outError)
	{
		oAssert(outError);
#ifdef ORKIGE_LUA
		// subscribing in init() is the idiom: tag those subscriptions with this
		// sandbox so a hot-reload (a fresh instance re-running init) or a removal
		// cancels them cleanly
		ScriptCallScope ownerScope(this);
		const sol::object initObject = this->environment["init"];
		if (initObject.is<sol::protected_function>())
		{
			const sol::protected_function_result initResult =
				initObject.as<sol::protected_function>()(this->selfTable);
			if (!initResult.valid())
			{
				const sol::error error = initResult;
				*outError = error.what();
				return false;
			}
		}
		const sol::object updateObject = this->environment["update"];
		if (updateObject.is<sol::protected_function>())
		{
			this->updateFunction = updateObject.as<sol::protected_function>();
		}
		return true;
#else
		*outError = "scripting disabled (built with ORKIGE_SCRIPTING=OFF)";
		return false;
#endif
	}
	//---------------------------------------------------------
	bool ScriptInstance::callUpdate(float deltaTime, String * outError)
	{
		oAssert(outError);
#ifdef ORKIGE_LUA
		if (!this->updateFunction.valid())
		{
			return true;	// no update function - nothing to do
		}
		ScriptCallScope ownerScope(this);
		const sol::protected_function_result result =
			this->updateFunction(this->selfTable, deltaTime);
		if (!result.valid())
		{
			const sol::error error = result;
			*outError = error.what();
			return false;
		}
		return true;
#else
		(void)deltaTime;
		*outError = "scripting disabled (built with ORKIGE_SCRIPTING=OFF)";
		return false;
#endif
	}
	//---------------------------------------------------------
	bool ScriptInstance::callShutdown(String * outError)
	{
		oAssert(outError);
#ifdef ORKIGE_LUA
		if (!this->environment.valid())
		{
			return true;
		}
		ScriptCallScope ownerScope(this);
		const sol::object shutdownObject = this->environment["shutdown"];
		if (shutdownObject.is<sol::protected_function>())
		{
			const sol::protected_function_result result =
				shutdownObject.as<sol::protected_function>()(this->selfTable);
			if (!result.valid())
			{
				const sol::error error = result;
				*outError = error.what();
				return false;
			}
		}
		return true;
#else
		*outError = "scripting disabled (built with ORKIGE_SCRIPTING=OFF)";
		return false;
#endif
	}
}
