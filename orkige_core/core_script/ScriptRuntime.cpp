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
#include "core_tween/TimerManager.h"
#include "core_script/ScriptEventPayload.h"
#include "core_base/TypeInfo.h"
#include "core_filesystem/ResourceReader.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace Orkige
{
	IMPL_OSINGLETON(ScriptRuntime)
#ifdef ORKIGE_LUA
	namespace
	{
		//! @brief try the process-wide archive ResourceReader for a script's
		//! source. Returns true with @p outSource filled on an archive hit (the
		//! script resolved by name across loose files + mounted paks/APKs);
		//! false means no reader is installed OR the name missed, so the caller
		//! FALLS BACK to loading the on-disk file. This is the ONE routing point
		//! that lets a pak/APK-resident script load in place instead of by fopen.
		bool readScriptThroughReader(String const & scriptFile, String & outSource)
		{
			ResourceReader const * reader = ResourceAccess::reader();
			return reader && reader->readText(scriptFile, outSource);
		}
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
		sol::state & lua = this->luaManager.state();
		optr<ScriptInstance> instance(new ScriptInstance());
		// per-instance sandbox: a fresh table whose reads fall through to the
		// real globals - writes stay in the instance (isolation between
		// instances; deliberate sharing goes through the `shared` table)
		instance->environment = sol::environment(lua, sol::create, lua.globals());

		// ARCHIVE-FIRST: when an archive reader is installed AND it resolves the
		// script by name, load the source from memory (the runString idiom, but
		// into THIS instance's sandbox) - so a script mounted inside a pak/APK
		// loads in place, no real file on disk. The chunk name is the script
		// path so Lua errors still read "<file>:<line>". No reader, or a miss,
		// falls through to the on-disk file below (headless core tests and
		// loose-file dev keep working unchanged).
		String source;
		if (readScriptThroughReader(scriptFile, source))
		{
			const sol::protected_function_result loadResult = lua.safe_script(
				source, instance->environment, sol::script_pass_on_error,
				"@" + scriptFile);
			if (!loadResult.valid())
			{
				const sol::error error = loadResult;
				*outError = error.what();
				return optr<ScriptInstance>();
			}
			instance->selfTable = lua.create_table();
			return instance;
		}

		const String resolvedPath = this->resolveScriptPath(scriptFile);
		if (resolvedPath.empty())
		{
			*outError = "script file not found (searched the project root '" +
				this->scriptSearchRoot + "' and the working directory)";
			return optr<ScriptInstance>();
		}
		// load the on-disk file through a memory chunk so the chunk NAME stays
		// the path the component asked for (project-relative), never the
		// machine-local absolute path resolveScriptPath found. Every instance
		// of one script then shares ONE chunk name - the key the script
		// debugger's breakpoints and the error file:line prefixes agree on.
		std::ifstream file(resolvedPath, std::ios::binary);
		if (!file)
		{
			*outError = "script file could not be read: " + resolvedPath;
			return optr<ScriptInstance>();
		}
		std::ostringstream buffer;
		buffer << file.rdbuf();
		source = buffer.str();
		const sol::protected_function_result loadResult = lua.safe_script(
			source, instance->environment, sol::script_pass_on_error,
			"@" + scriptFile);
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
		sol::state & lua = this->luaManager.state();
		// a throwaway sandbox: reads fall through to _G, top-level runs to
		// populate `properties`, but we never call init/update. A parse/run
		// error just means "no exports discovered" - a broken script's export
		// set is empty, its load failure surfaces later through the component.
		sol::environment probe(lua, sol::create, lua.globals());
		// ARCHIVE-FIRST, same routing as loadScriptInstance: read the source
		// through the injected reader (pak/APK-resident scripts resolve by name)
		// and fall back to the on-disk file when no reader is set or it misses.
		String source;
		if (readScriptThroughReader(scriptFile, source))
		{
			const sol::protected_function_result loadResult = lua.safe_script(
				source, probe, sol::script_pass_on_error, "@" + scriptFile);
			if (!loadResult.valid())
			{
				return properties;
			}
		}
		else
		{
			const String resolvedPath = this->resolveScriptPath(scriptFile);
			if (resolvedPath.empty())
			{
				return properties;	// no file -> no exports (honest, not an error)
			}
			const sol::protected_function_result loadResult = lua.safe_script_file(
				resolvedPath, probe, sol::script_pass_on_error);
			if (!loadResult.valid())
			{
				return properties;
			}
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
#ifdef ORKIGE_LUA
	namespace
	{
		//! stringify a Lua scalar for a ScriptValueMap field: an integral number
		//! prints without a decimal tail (so "3" not "3.0" - ids/counts stay
		//! clean), everything else through Lua's own tostring
		String luaScalarToString(sol::state_view lua, sol::object const & value)
		{
			if (value.is<bool>())
			{
				return value.as<bool>() ? "1" : "0";
			}
			if (value.is<double>())
			{
				const double number = value.as<double>();
				const long long integral = static_cast<long long>(number);
				if (static_cast<double>(integral) == number)
				{
					return std::to_string(integral);
				}
				std::ostringstream out;
				out << number;
				return out.str();
			}
			if (value.is<String>())
			{
				return value.as<String>();
			}
			// any other type: fall back to Lua tostring (rarely hit)
			const sol::protected_function tostring = lua["tostring"];
			if (tostring.valid())
			{
				const sol::protected_function_result result = tostring(value);
				if (result.valid())
				{
					return result.get<String>();
				}
			}
			return "";
		}
		//! is a Lua table an ARRAY (a non-empty run of 1..n integer keys)?
		bool luaTableIsArray(sol::table const & table)
		{
			return table.size() > 0 && table[1].valid();
		}
		//! convert one Lua argument table into a ScriptValueMap: a scalar field
		//! maps to a field; an array subtable to a string-list; a nested map
		//! subtable is FLATTENED (its scalar members become fields) - the same
		//! shape the MCP verb layer flattens its JSON arguments into, so a
		//! `cell = {col=.., row=..}` arrives as the col/row fields the verb reads
		void luaTableToValueMap(sol::state_view lua, sol::table const & table,
			ScriptValueMap & out)
		{
			table.for_each([&](sol::object const & key, sol::object const & value)
			{
				if (!key.is<String>())
				{
					return;	// only string-keyed argument fields are meaningful
				}
				const String name = key.as<String>();
				if (value.is<sol::table>())
				{
					const sol::table sub = value.as<sol::table>();
					if (luaTableIsArray(sub))
					{
						StringVector list;
						for (std::size_t i = 1; i <= sub.size(); ++i)
						{
							list.push_back(luaScalarToString(lua, sub[i]));
						}
						out.lists[name] = list;
					}
					else
					{
						// flatten a nested map's scalar members into fields
						sub.for_each([&](sol::object const & subKey,
							sol::object const & subValue)
						{
							if (subKey.is<String>() &&
								!subValue.is<sol::table>())
							{
								out.fields[subKey.as<String>()] =
									luaScalarToString(lua, subValue);
							}
						});
					}
				}
				else
				{
					out.fields[name] = luaScalarToString(lua, value);
				}
			});
		}
		//! build the Lua reply table from a ScriptValueMap: fields as string
		//! values, lists as array subtables (reply values stay strings - the
		//! script tonumber()s what it needs, matching the MCP structuredContent)
		sol::table valueMapToLuaTable(sol::state_view lua,
			ScriptValueMap const & reply)
		{
			sol::table table = lua.create_table();
			for (auto const & field : reply.fields)
			{
				table[field.first] = field.second;
			}
			for (auto const & list : reply.lists)
			{
				sol::table array = lua.create_table();
				for (std::size_t i = 0; i < list.second.size(); ++i)
				{
					array[i + 1] = list.second[i];
				}
				table[list.first] = array;
			}
			return table;
		}
	}
#endif //ORKIGE_LUA
	//---------------------------------------------------------
	void ScriptRuntime::registerHostFunction(char const * tableName,
		char const * functionName, ScriptHostFunction function)
	{
#ifdef ORKIGE_LUA
		sol::state & lua = this->luaManager.state();
		if (!lua[tableName].is<sol::table>())
		{
			lua[tableName] = lua.create_table();
		}
		lua[tableName][functionName] =
			[function](sol::this_state ts, sol::variadic_args args) -> sol::object
			{
				sol::state_view lua(ts);
				ScriptValueMap request;
				// the single (optional) table argument carries the named fields
				if (args.size() > 0 && args[0].is<sol::table>())
				{
					luaTableToValueMap(lua, args[0].as<sol::table>(), request);
				}
				ScriptValueMap reply;
				String error;
				if (!function(request, reply, error))
				{
					// the honest failure at the call site: a Lua error the
					// calling script sees with its own file:line
					luaL_error(ts, "%s", error.c_str());
					return sol::lua_nil;	// unreachable (luaL_error longjmps)
				}
				return valueMapToLuaTable(lua, reply);
			};
#else
		(void)tableName;
		(void)functionName;
		(void)function;
#endif
	}
	//---------------------------------------------------------
	namespace
	{
		//! the process-wide scriptable-component access registry: populated at
		//! module-init time by OSCRIPT_HANDLE (each component's OrkigeMetaExport),
		//! consumed by ScriptComponent's populateSelfTable / ensureScriptApi. A
		//! function-local static, safe to touch before any ScriptRuntime exists.
		std::vector<ScriptComponentAccess> & mutableComponentAccessRegistry()
		{
			static std::vector<ScriptComponentAccess> registry;
			return registry;
		}
	}
	//---------------------------------------------------------
	void ScriptRuntime::registerComponentAccess(ScriptComponentAccess entry)
	{
		std::vector<ScriptComponentAccess> & registry =
			mutableComponentAccessRegistry();
		// idempotent BY NAME: a re-run of the OrkigeMetaExport blocks (a second
		// module init in a test process) replaces the entry, never duplicates it
		for (ScriptComponentAccess & existing : registry)
		{
			if (existing.name == entry.name)
			{
				existing = std::move(entry);
				return;
			}
		}
		registry.push_back(std::move(entry));
	}
	//---------------------------------------------------------
	std::vector<ScriptComponentAccess> const &
		ScriptRuntime::componentAccessRegistry()
	{
		return mutableComponentAccessRegistry();
	}
	//---------------------------------------------------------
#ifdef ORKIGE_LUA
	sol::object ScriptRuntime::componentHandleFor(sol::state_view lua,
		String const & id, String const & name)
	{
		// resolve the declared component KIND by its script vocabulary name OR
		// its reflected kind name (the MCP get_component name) - so
		// getComponent("transform") and getComponent("TransformComponent") reach
		// the same component; an unknown name is a quiet nil (never a throw)
		for (ScriptComponentAccess const & access : componentAccessRegistry())
		{
			const bool nameMatches = access.name == name ||
				(access.type && name == access.type->getName());
			if (!nameMatches || !access.makeHandleFor)
			{
				continue;
			}
			GameObject * owner = this->componentResolver
				? this->componentResolver(id) : NULL;
			if (!owner)
			{
				return sol::object(lua, sol::in_place, sol::lua_nil);
			}
			// makeHandleFor yields nil for an absent component (it has-component
			// guards the assert-on-absent typed getter), so this is the whole
			// absent/present decision
			return access.makeHandleFor(lua, *owner);
		}
		return sol::object(lua, sol::in_place, sol::lua_nil);
	}
#endif
	//---------------------------------------------------------
	void ScriptRuntime::installComponentAccessors(
		std::function<GameObject * (String const &)> resolveById)
	{
#ifdef ORKIGE_LUA
		this->componentResolver = std::move(resolveById);
		sol::state & lua = this->luaManager.state();
		if (!lua["world"].is<sol::table>())
		{
			lua["world"] = lua.create_table();
		}
		// the generic floor: world.getComponent(id, name) reaches ANY declared
		// component KIND by its script (or reflected) name (nil for absent/unknown)
		lua["world"]["getComponent"] =
			[](sol::this_state ts, String const & id, String const & name)
				-> sol::object
			{
				sol::state_view view(ts);
				return ScriptRuntime::getSingleton().componentHandleFor(
					view, id, name);
			};
		// the convenience accessors (world.getTransform, getRigidBody, ...
		// getLevel) - each a declared component's own OSCRIPT_HANDLE, wired here
		// from the registry rather than by a hand-written line per type
		for (ScriptComponentAccess const & access : componentAccessRegistry())
		{
			if (access.worldAccessor.empty())
			{
				continue;
			}
			const String component = access.name;
			lua["world"][access.worldAccessor] =
				[component](sol::this_state ts, String const & id) -> sol::object
				{
					sol::state_view view(ts);
					return ScriptRuntime::getSingleton().componentHandleFor(
						view, id, component);
				};
		}
#else
		(void)resolveById;
#endif
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
#ifdef ORKIGE_LUA
	namespace
	{
		//! collect a table's own string keys into out (values untouched)
		void collectStringKeys(sol::table const & table, StringVector & out)
		{
			for (auto const & pair : table)
			{
				if (pair.first.is<String>())
				{
					out.push_back(pair.first.as<String>());
				}
			}
		}
	}
#endif //ORKIGE_LUA
	//---------------------------------------------------------
	StringVector ScriptRuntime::globalNames()
	{
		StringVector names;
#ifdef ORKIGE_LUA
		collectStringKeys(this->luaManager.state().globals(), names);
		std::sort(names.begin(), names.end());
		names.erase(std::unique(names.begin(), names.end()), names.end());
#endif
		return names;
	}
	//---------------------------------------------------------
	StringVector ScriptRuntime::globalMemberNames(String const & name)
	{
		StringVector names;
#ifdef ORKIGE_LUA
		sol::state & lua = this->luaManager.state();
		const sol::object value = lua[name];
		if (value.is<sol::table>())
		{
			// the table's own keys (an API table's functions)...
			collectStringKeys(value.as<sol::table>(), names);
			// ...plus, for a registered usertype (whose methods live behind
			// the metatable's __index), the method names too
			const sol::object meta =
				value.as<sol::table>()[sol::metatable_key];
			if (meta.is<sol::table>())
			{
				const sol::object index = meta.as<sol::table>()["__index"];
				if (index.is<sol::table>())
				{
					collectStringKeys(index.as<sol::table>(), names);
				}
			}
		}
		std::sort(names.begin(), names.end());
		names.erase(std::unique(names.begin(), names.end()), names.end());
#else
		(void)name;
#endif
		return names;
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
	//--- script debugger -------------------------------------
	//---------------------------------------------------------
	// The Lua half of the backend-neutral debug seam declared in
	// ScriptRuntime.h: a lua_sethook line hook (installed only while
	// breakpoints exist or a step is pending), the blocking break pump and the
	// C-API stack/local/upvalue readback. All of it lives here because this
	// file IS the sanctioned scripting-backend seam - no other translation
	// unit may name a Lua type or test ORKIGE_LUA. The pure decision logic
	// (chunk matching, the step state machine) is ScriptDebugCore, unit-tested
	// headlessly.
#ifdef ORKIGE_LUA
	namespace
	{
		//! the debugger's process-wide state (one Lua state per process; the
		//! hook is a plain C function, so the state is file-static by design)
		struct LuaDebugState
		{
			ScriptDebugCore::BreakpointIndex	breakpoints;
			ScriptStepMode	stepMode = ScriptStepMode::None;	//!< armed step
			int				stepBaseDepth = 0;	//!< depth the step released at
			bool			broken = false;		//!< blocked inside the hook now
			ScriptStepMode	resumeStep = ScriptStepMode::None;	//!< step asked by debugResume
			unsigned int	breakSequence = 0;	//!< increments per break entry
			String			breakFile;			//!< normalized paused chunk
			int				breakLine = 0;		//!< paused 1-based line
			std::vector<ScriptStackFrame>	frames;	//!< captured at break
			std::vector<int>	frameLevels;	//!< lua stack level per frame
			std::function<void()>	pumpHandler;	//!< the transport pump
			lua_State *		hookedState = nullptr;	//!< where the hook lives
		};
		LuaDebugState & luaDebug()
		{
			static LuaDebugState state;
			return state;
		}

		//! current call depth (number of activation records on the stack)
		int luaStackDepth(lua_State * L)
		{
			lua_Debug activation;
			int depth = 0;
			while (lua_getstack(L, depth, &activation))
			{
				++depth;
			}
			return depth;
		}

		//! @brief a bounded, metamethod-free display string for the value at
		//! `index` (raw type inspection only - a break must never run script
		//! code to render a variable). Fills the type name and whether the
		//! value is a table (expandable via an explicit expand request).
		String luaDescribeValue(lua_State * L, int index, String & outType,
			bool & outExpandable)
		{
			outExpandable = false;
			const int type = lua_type(L, index);
			outType = lua_typename(L, type);
			switch (type)
			{
			case LUA_TNIL:
				return "nil";
			case LUA_TBOOLEAN:
				return lua_toboolean(L, index) ? "true" : "false";
			case LUA_TNUMBER:
			{
				char buffer[48];
				if (lua_isinteger(L, index))
				{
					std::snprintf(buffer, sizeof(buffer), "%lld",
						static_cast<long long>(lua_tointeger(L, index)));
				}
				else
				{
					std::snprintf(buffer, sizeof(buffer), "%.14g",
						lua_tonumber(L, index));
				}
				return buffer;
			}
			case LUA_TSTRING:
			{
				std::size_t length = 0;
				char const * text = lua_tolstring(L, index, &length);
				const std::size_t kCap = 120;
				String value(text, std::min(length, kCap));
				if (length > kCap)
				{
					value += "...";
				}
				return "\"" + value + "\"";
			}
			case LUA_TTABLE:
			{
				outExpandable = true;
				const long long length = static_cast<long long>(
					lua_rawlen(L, index));
				return length > 0
					? ("table[" + std::to_string(length) + "]") : "table";
			}
			case LUA_TFUNCTION:
				return "function";
			case LUA_TUSERDATA:
			case LUA_TLIGHTUSERDATA:
				return "userdata";
			case LUA_TTHREAD:
				return "thread";
			default:
				return outType;
			}
		}

		//! @brief render a table KEY (at `index`) as the display/expand name:
		//! plain string keys stay as-is, everything else becomes the bracket
		//! form ("[3]", "[true]") ScriptRuntime::debugVariables can walk back.
		String luaDescribeKey(lua_State * L, int index)
		{
			if (lua_type(L, index) == LUA_TSTRING)
			{
				return lua_tostring(L, index);
			}
			String type;
			bool expandable = false;
			return "[" + luaDescribeValue(L, index, type, expandable) + "]";
		}

		//! @brief push the table field named by `key` (raw access, never a
		//! metamethod): a "[...]" bracket name resolves as integer / number /
		//! boolean, everything else as a string key. The table sits at -1;
		//! true = the field value replaced nothing (it is now at -1 on TOP of
		//! the table - the caller manages the stack).
		bool luaPushFieldByName(lua_State * L, String const & key)
		{
			if (key.size() >= 3 && key.front() == '[' && key.back() == ']')
			{
				const String inner = key.substr(1, key.size() - 2);
				if (inner == "true" || inner == "false")
				{
					lua_pushboolean(L, inner == "true");
				}
				else
				{
					try
					{
						std::size_t consumed = 0;
						const double number = std::stod(inner, &consumed);
						if (consumed != inner.size())
						{
							return false;
						}
						if (number == static_cast<long long>(number))
						{
							lua_pushinteger(L,
								static_cast<lua_Integer>(number));
						}
						else
						{
							lua_pushnumber(L, number);
						}
					}
					catch (std::exception const &)
					{
						return false;
					}
				}
			}
			else
			{
				lua_pushlstring(L, key.c_str(), key.size());
			}
			lua_rawget(L, -2);
			return true;
		}

		//! capture the paused call stack (innermost first) + the lua level of
		//! each captured frame so a later locals request can address it
		void luaCaptureFrames(lua_State * L)
		{
			LuaDebugState & debug = luaDebug();
			debug.frames.clear();
			debug.frameLevels.clear();
			lua_Debug activation;
			for (int level = 0; lua_getstack(L, level, &activation); ++level)
			{
				if (!lua_getinfo(L, "Sln", &activation))
				{
					continue;
				}
				ScriptStackFrame frame;
				frame.isScript = activation.what != nullptr &&
					std::strcmp(activation.what, "C") != 0;
				frame.source = frame.isScript
					? ScriptDebugCore::normalizeChunk(activation.source)
					: String("[host]");
				frame.line = activation.currentline;
				if (activation.name != nullptr)
				{
					frame.function = activation.name;
				}
				else if (activation.what != nullptr &&
					std::strcmp(activation.what, "main") == 0)
				{
					frame.function = "(main chunk)";
				}
				debug.frames.push_back(frame);
				debug.frameLevels.push_back(level);
			}
		}

		//! the line hook: the fast rejects run first (armed step / a breakpoint
		//! on this line number), then the blocking break pump on a hit
		void luaDebugHook(lua_State * L, lua_Debug * activation)
		{
			LuaDebugState & debug = luaDebug();
			if (debug.broken || activation->event != LUA_HOOKLINE)
			{
				// never re-enter: a deferred command running script code while
				// broken must not open a second nested break
				return;
			}
			const int line = activation->currentline;
			bool hit = false;
			if (debug.stepMode != ScriptStepMode::None)
			{
				hit = ScriptDebugCore::stepShouldBreak(debug.stepMode,
					debug.stepBaseDepth, luaStackDepth(L));
			}
			if (!hit && debug.breakpoints.anyOnLine(line))
			{
				lua_getinfo(L, "S", activation);
				hit = debug.breakpoints.matches(
					ScriptDebugCore::normalizeChunk(activation->source), line);
			}
			if (!hit)
			{
				return;
			}
			lua_getinfo(L, "S", activation);
			debug.breakFile = ScriptDebugCore::normalizeChunk(
				activation->source);
			debug.breakLine = line;
			luaCaptureFrames(L);
			debug.stepMode = ScriptStepMode::None;	// the armed step landed
			debug.resumeStep = ScriptStepMode::None;
			++debug.breakSequence;
			// the blocking pump: the transport handler runs until a resume /
			// step command clears `broken`. No handler = resume immediately
			// (a headless run must never wedge).
			if (debug.pumpHandler)
			{
				debug.broken = true;
				while (debug.broken)
				{
					debug.pumpHandler();
				}
			}
			debug.breakFile.clear();
			debug.breakLine = 0;
			debug.frames.clear();
			debug.frameLevels.clear();
			// a requested step arms relative to the depth we release at
			if (debug.resumeStep != ScriptStepMode::None)
			{
				debug.stepMode = debug.resumeStep;
				debug.stepBaseDepth = luaStackDepth(L);
				debug.resumeStep = ScriptStepMode::None;
			}
			// with neither breakpoints nor a step left, drop the hook so free
			// running scripts pay nothing
			if (debug.breakpoints.empty() &&
				debug.stepMode == ScriptStepMode::None)
			{
				lua_sethook(L, nullptr, 0, 0);
				debug.hookedState = nullptr;
			}
		}

		//! install/remove the line hook to match the wanted state (breakpoints
		//! set or a step armed = installed; neither = removed)
		void luaApplyHookInstallation(lua_State * L)
		{
			LuaDebugState & debug = luaDebug();
			const bool wanted = !debug.breakpoints.empty() ||
				debug.stepMode != ScriptStepMode::None;
			if (wanted && debug.hookedState == nullptr)
			{
				lua_sethook(L, luaDebugHook, LUA_MASKLINE, 0);
				debug.hookedState = L;
			}
			else if (!wanted && debug.hookedState != nullptr)
			{
				lua_sethook(debug.hookedState, nullptr, 0, 0);
				debug.hookedState = nullptr;
			}
		}
	}
#endif //ORKIGE_LUA
	//---------------------------------------------------------
	bool ScriptRuntime::checkSyntax(String const & source,
		String const & chunkName, String * outError)
	{
		oAssert(outError);
#ifdef ORKIGE_LUA
		// load = compile only; the resulting chunk is dropped unrun (no
		// environment, no side effects - the live-diagnostics seam)
		const sol::load_result chunk = this->luaManager.state().load(
			source, "@" + chunkName);
		if (!chunk.valid())
		{
			const sol::error error = chunk;
			*outError = error.what();
			return false;
		}
		return true;
#else
		(void)source;
		(void)chunkName;
		*outError = ScriptRuntime::disabledError();
		return false;
#endif
	}
	//---------------------------------------------------------
	bool ScriptRuntime::debugBreakSupported()
	{
#if defined(__EMSCRIPTEN__) || !defined(ORKIGE_LUA)
		return false;
#else
		return true;
#endif
	}
	//---------------------------------------------------------
	bool ScriptRuntime::setDebugBreakpoints(
		std::vector<ScriptBreakpoint> const & breakpoints, String * outError)
	{
		oAssert(outError);
#if defined(__EMSCRIPTEN__)
		(void)breakpoints;
		*outError = "script breakpoints are not supported in the browser "
			"player (the page's main thread cannot block at a break)";
		return false;
#elif defined(ORKIGE_LUA)
		LuaDebugState & debug = luaDebug();
		debug.breakpoints.assign(breakpoints);
		luaApplyHookInstallation(this->luaManager.state().lua_state());
		return true;
#else
		(void)breakpoints;
		*outError = ScriptRuntime::disabledError();
		return false;
#endif
	}
	//---------------------------------------------------------
	bool ScriptRuntime::debugBreakNext(String * outError)
	{
		oAssert(outError);
#if defined(__EMSCRIPTEN__)
		*outError = "break on next statement is not supported in the browser "
			"player (the page's main thread cannot block at a break)";
		return false;
#elif defined(ORKIGE_LUA)
		LuaDebugState & debug = luaDebug();
		if (debug.broken)
		{
			// already paused: the held break IS the pause - nothing to arm
			// (the editor disables the Break control while broken anyway)
			return true;
		}
		// arm a one-shot next-line break from this UNBROKEN state: In breaks at
		// the very next LINE event, any depth (@see ScriptDebugCore). The base
		// depth is irrelevant to In; 0 keeps it honest. luaApplyHookInstallation
		// installs the hook if it was not already carrying breakpoints/a step.
		debug.stepMode = ScriptDebugCore::breakNextStepMode();
		debug.stepBaseDepth = 0;
		luaApplyHookInstallation(this->luaManager.state().lua_state());
		return true;
#else
		*outError = ScriptRuntime::disabledError();
		return false;
#endif
	}
	//---------------------------------------------------------
	void ScriptRuntime::setDebugPumpHandler(std::function<void()> handler)
	{
#ifdef ORKIGE_LUA
		luaDebug().pumpHandler = std::move(handler);
#else
		(void)handler;
#endif
	}
	//---------------------------------------------------------
	bool ScriptRuntime::isDebugBroken() const
	{
#ifdef ORKIGE_LUA
		return luaDebug().broken;
#else
		return false;
#endif
	}
	//---------------------------------------------------------
	unsigned int ScriptRuntime::debugBreakSequence() const
	{
#ifdef ORKIGE_LUA
		return luaDebug().breakSequence;
#else
		return 0;
#endif
	}
	//---------------------------------------------------------
	String ScriptRuntime::debugBreakFile() const
	{
#ifdef ORKIGE_LUA
		return luaDebug().broken ? luaDebug().breakFile : String();
#else
		return String();
#endif
	}
	//---------------------------------------------------------
	int ScriptRuntime::debugBreakLine() const
	{
#ifdef ORKIGE_LUA
		return luaDebug().broken ? luaDebug().breakLine : 0;
#else
		return 0;
#endif
	}
	//---------------------------------------------------------
	std::vector<ScriptStackFrame> ScriptRuntime::debugStackFrames() const
	{
#ifdef ORKIGE_LUA
		return luaDebug().broken ? luaDebug().frames
			: std::vector<ScriptStackFrame>();
#else
		return std::vector<ScriptStackFrame>();
#endif
	}
	//---------------------------------------------------------
	void ScriptRuntime::debugResume(ScriptStepMode stepMode)
	{
#ifdef ORKIGE_LUA
		LuaDebugState & debug = luaDebug();
		if (!debug.broken)
		{
			return;
		}
		debug.resumeStep = stepMode;
		debug.broken = false;	// releases the pump loop inside the hook
#else
		(void)stepMode;
#endif
	}
	//---------------------------------------------------------
	std::vector<ScriptDebugVariable> ScriptRuntime::debugVariables(
		int frameIndex, StringVector const & expandPath, int maxEntries,
		String * outError)
	{
		oAssert(outError);
		std::vector<ScriptDebugVariable> variables;
#ifdef ORKIGE_LUA
		LuaDebugState & debug = luaDebug();
		if (!debug.broken)
		{
			*outError = "not paused at a script break";
			return variables;
		}
		if (frameIndex < 0 ||
			static_cast<std::size_t>(frameIndex) >= debug.frameLevels.size())
		{
			*outError = "no such stack frame";
			return variables;
		}
		if (maxEntries <= 0)
		{
			maxEntries = 64;
		}
		lua_State * L = this->luaManager.state().lua_state();
		const int stackTop = lua_gettop(L);
		lua_Debug activation;
		if (!lua_getstack(L, debug.frameLevels[frameIndex], &activation))
		{
			*outError = "stack frame vanished";
			return variables;
		}
		const auto describeTop = [&](String const & name,
			String const & scope)
		{
			ScriptDebugVariable variable;
			variable.name = name;
			variable.scope = scope;
			variable.value = luaDescribeValue(L, -1, variable.type,
				variable.expandable);
			variables.push_back(variable);
		};
		if (expandPath.empty())
		{
			// the frame's locals (skip the compiler's "(...)" temporaries)...
			for (int index = 1;; ++index)
			{
				char const * name = lua_getlocal(L, &activation, index);
				if (name == nullptr)
				{
					break;
				}
				if (name[0] != '(' &&
					static_cast<int>(variables.size()) < maxEntries)
				{
					describeTop(name, "local");
				}
				lua_pop(L, 1);
			}
			// ...then the function's upvalues (the enclosing-scope captures)
			if (lua_getinfo(L, "f", &activation))
			{
				for (int index = 1;; ++index)
				{
					char const * name = lua_getupvalue(L, -1, index);
					if (name == nullptr)
					{
						break;
					}
					if (name[0] != '(' &&
						static_cast<int>(variables.size()) < maxEntries)
					{
						describeTop(name, "upvalue");
					}
					lua_pop(L, 1);
				}
			}
			lua_settop(L, stackTop);
			return variables;
		}
		// expand request: resolve the ROOT variable by name among the frame's
		// locals first, then its upvalues, then walk the table-key chain
		bool rootFound = false;
		for (int index = 1; !rootFound; ++index)
		{
			char const * name = lua_getlocal(L, &activation, index);
			if (name == nullptr)
			{
				break;
			}
			if (expandPath[0] == name)
			{
				rootFound = true;	// value stays on the stack
				break;
			}
			lua_pop(L, 1);
		}
		if (!rootFound && lua_getinfo(L, "f", &activation))
		{
			for (int index = 1; !rootFound; ++index)
			{
				char const * name = lua_getupvalue(L, -1, index);
				if (name == nullptr)
				{
					break;
				}
				if (expandPath[0] == name)
				{
					lua_remove(L, -2);	// drop the function under the value
					rootFound = true;
					break;
				}
				lua_pop(L, 1);
			}
			if (!rootFound)
			{
				lua_pop(L, 1);	// the function
			}
		}
		if (!rootFound)
		{
			lua_settop(L, stackTop);
			*outError = "no variable '" + expandPath[0] + "' in this frame";
			return variables;
		}
		for (std::size_t step = 1; step < expandPath.size(); ++step)
		{
			if (lua_type(L, -1) != LUA_TTABLE ||
				!luaPushFieldByName(L, expandPath[step]))
			{
				lua_settop(L, stackTop);
				*outError = "'" + expandPath[step - 1] +
					"' is not an expandable table";
				return variables;
			}
			lua_remove(L, -2);	// keep only the field value
		}
		if (lua_type(L, -1) != LUA_TTABLE)
		{
			lua_settop(L, stackTop);
			*outError = "'" + expandPath.back() + "' is not a table";
			return variables;
		}
		// list the target table's entries (raw iteration, bounded)
		lua_pushnil(L);
		while (lua_next(L, -2) != 0)
		{
			if (static_cast<int>(variables.size()) >= maxEntries)
			{
				lua_pop(L, 2);	// key + value - stop the walk
				break;
			}
			describeTop(luaDescribeKey(L, -2), "field");
			lua_pop(L, 1);	// the value; the key stays for lua_next
		}
		lua_settop(L, stackTop);
		return variables;
#else
		(void)frameIndex;
		(void)expandPath;
		(void)maxEntries;
		*outError = ScriptRuntime::disabledError();
		return variables;
#endif
	}
	//---------------------------------------------------------
	void ScriptRuntime::debugDetach()
	{
#ifdef ORKIGE_LUA
		LuaDebugState & debug = luaDebug();
		debug.breakpoints.clear();
		if (debug.broken)
		{
			this->debugResume(ScriptStepMode::None);
		}
		else
		{
			debug.stepMode = ScriptStepMode::None;
			luaApplyHookInstallation(this->luaManager.state().lua_state());
		}
#endif
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
		// the SAME owner-token retire for this sandbox's scheduled timers
		// (timer.after/every tagged with `this`): a stale timer must never fire
		// into a dead sandbox. Guarded - the editor never creates a TimerManager.
		if(TimerManager::getSingletonPtr() != 0)
		{
			TimerManager::getSingleton().cancelOwner(this);
		}
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
	void ScriptInstance::installComponentAccessor()
	{
#ifdef ORKIGE_LUA
		if (!this->selfTable.valid())
		{
			return;
		}
		// self:getComponent(name) - the generic floor. Reads self.id at CALL
		// time (never captures the owner), resolving any declared component KIND
		// by its script or reflected name through the ONE registry, nil for an
		// absent/unknown kind. Same weak-handle currency as world.getComponent.
		this->selfTable["getComponent"] =
			[](sol::this_state ts, sol::table self, String const & name)
				-> sol::object
			{
				sol::state_view view(ts);
				const sol::object idObject = self["id"];
				if (!idObject.is<String>())
				{
					return sol::object(view, sol::in_place, sol::lua_nil);
				}
				return ScriptRuntime::getSingleton().componentHandleFor(
					view, idObject.as<String>(), name);
			};
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
