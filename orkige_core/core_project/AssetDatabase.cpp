/**************************************************************
	created:	2026/07/09 at 10:00
	filename: 	AssetDatabase.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include <tinyxml2.h>

#include "core_project/AssetDatabase.h"
#include "core_project/Project.h"
#include "core_debug/LogManager.h"
#include "core_util/Sha1.h"

#include <algorithm>
#include <filesystem>
#include <iomanip>
#include <random>
#include <sstream>
#include <system_error>
#include <vector>

namespace Orkige
{
	const String AssetDatabase::META_FILE_EXTENSION = ".orkmeta";
	const String AssetDatabase::META_ELEMENT_NAME = "orkmeta";
	const String AssetDatabase::META_ID_ATTRIBUTE = "id";
	const String AssetDatabase::META_TEXTURE_ELEMENT_NAME = "texture";
	const String AssetDatabase::META_COOK_ELEMENT_NAME = "cook";
	const String AssetDatabase::REFERENCE_ID_ATTRIBUTE = "assetId";

	namespace
	{
		//! read one <texture>/<android>/<ios>/<web> element's attributes onto
		//! settings that already carry the inherited defaults (an absent
		//! attribute leaves its value untouched - so override sub-blocks are
		//! deltas over the default block)
		void readTextureSettings(tinyxml2::XMLElement const * element,
			TextureImportSettings & settings)
		{
			if (const char * value = element->Attribute("filter"))
			{
				settings.filter = value;
			}
			if (const char * value = element->Attribute("wrap"))
			{
				settings.wrap = value;
			}
			element->QueryIntAttribute("maxSize", &settings.maxSize);
			element->QueryBoolAttribute("premultiply", &settings.premultiply);
			element->QueryBoolAttribute("generateMips", &settings.generateMips);
			if (const char * value = element->Attribute("format"))
			{
				settings.format = value;
			}
			if (const char * value = element->Attribute("quality"))
			{
				settings.quality = value;
			}
		}
		//! write a settings block's attributes onto an element (full, not a
		//! delta - deterministic output the cook and a re-import both trust)
		void writeTextureSettings(tinyxml2::XMLElement * element,
			TextureImportSettings const & settings)
		{
			element->SetAttribute("filter", settings.filter.c_str());
			element->SetAttribute("wrap", settings.wrap.c_str());
			element->SetAttribute("maxSize", settings.maxSize);
			element->SetAttribute("premultiply", settings.premultiply);
			element->SetAttribute("generateMips", settings.generateMips);
			element->SetAttribute("format", settings.format.c_str());
			element->SetAttribute("quality", settings.quality.c_str());
		}
	}
	//---------------------------------------------------------
	void TextureImportSettings::appliedSize(int srcW, int srcH,
		int & outW, int & outH) const
	{
		outW = srcW > 0 ? srcW : 0;
		outH = srcH > 0 ? srcH : 0;
		if (this->maxSize <= 0 || outW <= 0 || outH <= 0)
		{
			return;		// uncapped, or a degenerate source: pass through
		}
		const int longest = outW > outH ? outW : outH;
		if (longest <= this->maxSize)
		{
			return;		// already within the cap - never upscales
		}
		// scale the longest side down to the cap, the other in proportion,
		// rounding each and never collapsing a side below one texel
		const double scale = static_cast<double>(this->maxSize) / longest;
		outW = static_cast<int>(outW * scale + 0.5);
		outH = static_cast<int>(outH * scale + 0.5);
		if (outW < 1) { outW = 1; }
		if (outH < 1) { outH = 1; }
	}
	//---------------------------------------------------------
	TextureImportSettings const & TextureImport::resolvedFor(
		String const & platform) const
	{
		if (platform == "android" && this->hasAndroid)
		{
			return this->android;
		}
		if (platform == "ios" && this->hasIos)
		{
			return this->ios;
		}
		if (platform == "web" && this->hasWeb)
		{
			return this->web;
		}
		return this->base;
	}
	//---------------------------------------------------------
	String CookSettings::canonical() const
	{
		// one "key=value" line per option in fixed order; values verbatim so
		// the form is byte-stable across writes (@see the header remarks)
		return "clips=" + this->clips + "\n" +
			"extent=" + this->extent + "\n" +
			"tolerance=" + this->tolerance + "\n";
	}
	//---------------------------------------------------------
	String CookSettings::hash() const
	{
		const String canonicalForm = this->canonical();
		return Sha1::hexDigest(canonicalForm.data(), canonicalForm.size());
	}
	//---------------------------------------------------------
	bool CookRecord::matchesInputs(String const & currentSourceHash,
		String const & currentToolHash) const
	{
		// a record without hashes never matches (it also never auto-re-cooks -
		// callers gate on readCookRecord's return first); empty CURRENT hashes
		// mean an unreadable input, which must read as stale, never as a match
		if (this->sourceHash.empty() || currentSourceHash.empty() ||
			this->toolHash.empty() || currentToolHash.empty() ||
			this->settingsHash.empty())
		{
			return false;
		}
		return this->sourceHash == currentSourceHash &&
			this->toolHash == currentToolHash &&
			this->settingsHash == this->settings.hash();
	}

	optr<AssetDatabase> AssetDatabase::sActive;
	//---------------------------------------------------------
	namespace
	{
		//! @brief database log line - guarded because the database runs
		//! before the engine singleton set exists (the player loads its
		//! project before booting the engine); without a LogManager the
		//! message is honestly dropped
		void assetLog(String const & text)
		{
			if (LogManager::getSingletonPtr())
			{
				oDebugMsg("asset", 0, text);
			}
		}
		//! is this a sidecar file name
		bool isMetaFileName(std::filesystem::path const & path)
		{
			return path.extension().string() ==
				AssetDatabase::META_FILE_EXTENSION;
		}
		//! hidden housekeeping files (.DS_Store & co) are never assets
		bool isHiddenFileName(std::filesystem::path const & path)
		{
			const String name = path.filename().string();
			return !name.empty() && name[0] == '.';
		}
	}
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	void AssetDatabase::refresh(String const & projectRootDirectory,
		bool createSidecars)
	{
		this->clear();
		if (projectRootDirectory.empty())
		{
			return;
		}
		std::error_code fsError;
		this->mRootDirectory = std::filesystem::absolute(
			projectRootDirectory, fsError).lexically_normal().string();
		for (String const & subdirectory : { Project::ASSETS_DIR_NAME,
			Project::SCRIPTS_DIR_NAME })
		{
			this->scanDirectory((std::filesystem::path(this->mRootDirectory) /
				subdirectory).string(), createSidecars);
		}
	}
	//---------------------------------------------------------
	void AssetDatabase::clear()
	{
		this->mRootDirectory.clear();
		this->mIdToPath.clear();
		this->mPathToId.clear();
		this->mFileNameToId.clear();
	}
	//---------------------------------------------------------
	std::vector<AssetEntry> AssetDatabase::listAssets() const
	{
		// iterate the path->id map: it is already sorted by project-relative
		// path (std::map), which is the stable order callers want
		std::vector<AssetEntry> entries;
		entries.reserve(this->mPathToId.size());
		for (std::pair<String const, String> const & pathAndId :
			this->mPathToId)
		{
			AssetEntry entry;
			entry.id = pathAndId.second;
			entry.relativePath = pathAndId.first;
			entry.fileName =
				std::filesystem::path(pathAndId.first).filename().string();
			entries.push_back(entry);
		}
		return entries;
	}
	//---------------------------------------------------------
	String AssetDatabase::pathForId(String const & assetId) const
	{
		const std::map<String, String>::const_iterator found =
			this->mIdToPath.find(assetId);
		return (found != this->mIdToPath.end()) ? found->second : String();
	}
	//---------------------------------------------------------
	String AssetDatabase::idForPath(String const & relativePath) const
	{
		// keys are the scanner's lexically-normal generic (forward-slash)
		// form - tolerate "./"-prefixed or doubled-separator spellings, and
		// on Windows also backslashes. Deliberately CASE-SENSITIVE on every
		// platform: a case-insensitive host filesystem (macOS) must not hide
		// a mismatch that breaks on Linux/Android.
		const std::map<String, String>::const_iterator found =
			this->mPathToId.find(std::filesystem::path(relativePath)
				.lexically_normal().generic_string());
		return (found != this->mPathToId.end()) ? found->second : String();
	}
	//---------------------------------------------------------
	String AssetDatabase::idForFileName(String const & fileName) const
	{
		const std::map<String, String>::const_iterator found =
			this->mFileNameToId.find(fileName);
		return (found != this->mFileNameToId.end()) ? found->second : String();
	}
	//---------------------------------------------------------
	String AssetDatabase::fileNameForId(String const & assetId) const
	{
		const String relativePath = this->pathForId(assetId);
		if (relativePath.empty())
		{
			return String();
		}
		return std::filesystem::path(relativePath).filename().string();
	}
	//---------------------------------------------------------
	String AssetDatabase::importAsset(String const & assetPath)
	{
		std::filesystem::path absolute;
		if (!this->resolveInsideRoot(assetPath, absolute))
		{
			return String();
		}
		std::error_code fsError;
		if (!std::filesystem::is_regular_file(absolute, fsError))
		{
			return String(); // not a file inside this project
		}
		const String relativePath = absolute.lexically_relative(
			this->mRootDirectory).generic_string();
		const String metaPath = absolute.string() + META_FILE_EXTENSION;
		String assetId;
		if (!readMetaFile(metaPath, assetId))
		{
			assetId = generateId();
			if (!writeMetaFile(metaPath, assetId))
			{
				assetLog("AssetDatabase: could not write sidecar '" +
					metaPath + "'");
				return String();
			}
			assetLog("AssetDatabase: imported '" + relativePath + "' as " +
				assetId);
		}
		this->registerAsset(relativePath, assetId);
		return assetId;
	}
	//---------------------------------------------------------
	bool AssetDatabase::moveAsset(String const & relativePath,
		String const & newRelativePath)
	{
		std::filesystem::path source;
		std::filesystem::path dest;
		if (!this->resolveInsideRoot(relativePath, source) ||
			!this->resolveInsideRoot(newRelativePath, dest))
		{
			return false;
		}
		std::error_code fsError;
		if (!std::filesystem::is_regular_file(source, fsError))
		{
			return false; // nothing (or not a file) to move
		}
		if (std::filesystem::exists(dest, fsError))
		{
			return false; // never clobber an existing destination
		}
		std::filesystem::create_directories(dest.parent_path(), fsError);
		std::filesystem::rename(source, dest, fsError);
		if (fsError)
		{
			assetLog("AssetDatabase: could not move '" + relativePath +
				"' to '" + newRelativePath + "' - " + fsError.message());
			return false;
		}
		// the sidecar travels with the asset when present - that is what keeps
		// the id (a move without it would look like a fresh asset on rescan)
		const String sourceMeta = source.string() + META_FILE_EXTENSION;
		if (std::filesystem::exists(sourceMeta, fsError))
		{
			std::filesystem::rename(sourceMeta,
				dest.string() + META_FILE_EXTENSION, fsError);
		}
		// map surgery: an unindexed file (sidecar-less / under a non-tracked
		// tree) simply has no entry to carry - the fs move already succeeded
		const String oldRel = source.lexically_relative(
			this->mRootDirectory).generic_string();
		const String newRel = dest.lexically_relative(
			this->mRootDirectory).generic_string();
		const std::map<String, String>::iterator found =
			this->mPathToId.find(oldRel);
		if (found != this->mPathToId.end())
		{
			const String assetId = found->second;
			this->mPathToId.erase(found);
			const String oldName =
				std::filesystem::path(oldRel).filename().string();
			const std::map<String, String>::iterator nameEntry =
				this->mFileNameToId.find(oldName);
			if (nameEntry != this->mFileNameToId.end() &&
				nameEntry->second == assetId)
			{
				this->mFileNameToId.erase(nameEntry);
			}
			this->registerAsset(newRel, assetId);
		}
		return true;
	}
	//---------------------------------------------------------
	String AssetDatabase::duplicateAsset(String const & relativePath)
	{
		std::filesystem::path source;
		if (!this->resolveInsideRoot(relativePath, source))
		{
			return String();
		}
		std::error_code fsError;
		if (!std::filesystem::is_regular_file(source, fsError))
		{
			return String();
		}
		// a unique sibling "<stem> Copy[ N]<ext>"
		const String stem = source.stem().string();
		const String extension = source.extension().string();
		std::filesystem::path dest =
			source.parent_path() / (stem + " Copy" + extension);
		int suffix = 2;
		while (std::filesystem::exists(dest, fsError))
		{
			dest = source.parent_path() /
				(stem + " Copy " + std::to_string(suffix) + extension);
			++suffix;
		}
		// copy the ASSET only - never the sidecar (a copied sidecar duplicates
		// the id); the copy gets a fresh identity below
		std::filesystem::copy_file(source, dest, fsError);
		if (fsError)
		{
			assetLog("AssetDatabase: could not duplicate '" + relativePath +
				"' - " + fsError.message());
			return String();
		}
		const String freshId = generateId();
		const String destMeta = dest.string() + META_FILE_EXTENSION;
		// carry the source's import intent onto the copy (settings are
		// per-asset, the id is per-file); a source without a <texture> block
		// yields a plain id-only sidecar
		TextureImport texture;
		const bool hasSettings = readImportSettings(
			source.string() + META_FILE_EXTENSION, texture);
		const bool wrote = hasSettings
			? writeMetaFile(destMeta, freshId, texture)
			: writeMetaFile(destMeta, freshId);
		if (!wrote)
		{
			assetLog("AssetDatabase: could not write the copy's sidecar '" +
				destMeta + "'");
			return String();
		}
		const String destRel = dest.lexically_relative(
			this->mRootDirectory).generic_string();
		this->registerAsset(destRel, freshId);
		assetLog("AssetDatabase: duplicated '" + relativePath + "' as '" +
			destRel + "' (" + freshId + ")");
		return destRel;
	}
	//---------------------------------------------------------
	String AssetDatabase::generateId()
	{
		// std::random_device-seeded, per thread; two 64-bit draws = 128 bits
		static thread_local std::mt19937_64 engine = []()
		{
			std::random_device device;
			std::seed_seq seed{ device(), device(), device(), device(),
				device(), device(), device(), device() };
			return std::mt19937_64(seed);
		}();
		std::ostringstream stream;
		stream << std::hex << std::setfill('0')
			<< std::setw(16) << engine() << std::setw(16) << engine();
		return stream.str();
	}
	//---------------------------------------------------------
	bool AssetDatabase::readMetaFile(String const & metaFilePath,
		String & assetId)
	{
		tinyxml2::XMLDocument document;
		if (document.LoadFile(metaFilePath.c_str()) != tinyxml2::XML_SUCCESS)
		{
			return false;
		}
		const tinyxml2::XMLElement * root = document.RootElement();
		if (!root || String(root->Name()) != META_ELEMENT_NAME)
		{
			return false;
		}
		const char * id = root->Attribute(META_ID_ATTRIBUTE.c_str());
		if (!id || !*id)
		{
			return false;
		}
		assetId = id;
		return true;
	}
	//---------------------------------------------------------
	bool AssetDatabase::writeMetaFile(String const & metaFilePath,
		String const & assetId)
	{
		tinyxml2::XMLDocument document;
		tinyxml2::XMLElement * root = document.NewElement(
			META_ELEMENT_NAME.c_str());
		root->SetAttribute(META_ID_ATTRIBUTE.c_str(), assetId.c_str());
		document.InsertEndChild(root);
		return document.SaveFile(metaFilePath.c_str()) ==
			tinyxml2::XML_SUCCESS;
	}
	//---------------------------------------------------------
	bool AssetDatabase::writeMetaFile(String const & metaFilePath,
		String const & assetId, TextureImport const & texture)
	{
		tinyxml2::XMLDocument document;
		tinyxml2::XMLElement * root = document.NewElement(
			META_ELEMENT_NAME.c_str());
		root->SetAttribute(META_ID_ATTRIBUTE.c_str(), assetId.c_str());
		tinyxml2::XMLElement * textureElement = document.NewElement(
			META_TEXTURE_ELEMENT_NAME.c_str());
		writeTextureSettings(textureElement, texture.base);
		if (texture.hasAndroid)
		{
			tinyxml2::XMLElement * platform = document.NewElement("android");
			writeTextureSettings(platform, texture.android);
			textureElement->InsertEndChild(platform);
		}
		if (texture.hasIos)
		{
			tinyxml2::XMLElement * platform = document.NewElement("ios");
			writeTextureSettings(platform, texture.ios);
			textureElement->InsertEndChild(platform);
		}
		if (texture.hasWeb)
		{
			tinyxml2::XMLElement * platform = document.NewElement("web");
			writeTextureSettings(platform, texture.web);
			textureElement->InsertEndChild(platform);
		}
		root->InsertEndChild(textureElement);
		document.InsertEndChild(root);
		return document.SaveFile(metaFilePath.c_str()) ==
			tinyxml2::XML_SUCCESS;
	}
	//---------------------------------------------------------
	bool AssetDatabase::readImportSettings(String const & metaFilePath,
		TextureImport & texture)
	{
		texture = TextureImport();	// defaults until proven otherwise
		tinyxml2::XMLDocument document;
		if (document.LoadFile(metaFilePath.c_str()) != tinyxml2::XML_SUCCESS)
		{
			return false;
		}
		const tinyxml2::XMLElement * root = document.RootElement();
		if (!root || String(root->Name()) != META_ELEMENT_NAME)
		{
			return false;
		}
		const tinyxml2::XMLElement * textureElement =
			root->FirstChildElement(META_TEXTURE_ELEMENT_NAME.c_str());
		if (!textureElement)
		{
			return false;	// an id-only v1 sidecar - no import block
		}
		readTextureSettings(textureElement, texture.base);
		// per-platform sub-blocks are deltas: start each from the default
		if (const tinyxml2::XMLElement * android =
			textureElement->FirstChildElement("android"))
		{
			texture.hasAndroid = true;
			texture.android = texture.base;
			readTextureSettings(android, texture.android);
		}
		if (const tinyxml2::XMLElement * ios =
			textureElement->FirstChildElement("ios"))
		{
			texture.hasIos = true;
			texture.ios = texture.base;
			readTextureSettings(ios, texture.ios);
		}
		if (const tinyxml2::XMLElement * web =
			textureElement->FirstChildElement("web"))
		{
			texture.hasWeb = true;
			texture.web = texture.base;
			readTextureSettings(web, texture.web);
		}
		return true;
	}
	//---------------------------------------------------------
	bool AssetDatabase::writeMetaFile(String const & metaFilePath,
		String const & assetId, CookRecord const & cook)
	{
		tinyxml2::XMLDocument document;
		tinyxml2::XMLElement * root = document.NewElement(
			META_ELEMENT_NAME.c_str());
		root->SetAttribute(META_ID_ATTRIBUTE.c_str(), assetId.c_str());
		tinyxml2::XMLElement * cookElement = document.NewElement(
			META_COOK_ELEMENT_NAME.c_str());
		cookElement->SetAttribute("tool", cook.tool.c_str());
		cookElement->SetAttribute("sourceHash", cook.sourceHash.c_str());
		cookElement->SetAttribute("toolHash", cook.toolHash.c_str());
		cookElement->SetAttribute("settingsHash", cook.settingsHash.c_str());
		cookElement->SetAttribute("clips", cook.settings.clips.c_str());
		cookElement->SetAttribute("extent", cook.settings.extent.c_str());
		cookElement->SetAttribute("tolerance",
			cook.settings.tolerance.c_str());
		root->InsertEndChild(cookElement);
		document.InsertEndChild(root);
		return document.SaveFile(metaFilePath.c_str()) ==
			tinyxml2::XML_SUCCESS;
	}
	//---------------------------------------------------------
	bool AssetDatabase::readCookRecord(String const & metaFilePath,
		CookRecord & cook)
	{
		cook = CookRecord();	// empty until proven otherwise
		tinyxml2::XMLDocument document;
		if (document.LoadFile(metaFilePath.c_str()) != tinyxml2::XML_SUCCESS)
		{
			return false;
		}
		const tinyxml2::XMLElement * root = document.RootElement();
		if (!root || String(root->Name()) != META_ELEMENT_NAME)
		{
			return false;
		}
		const tinyxml2::XMLElement * cookElement =
			root->FirstChildElement(META_COOK_ELEMENT_NAME.c_str());
		if (!cookElement)
		{
			return false;	// a pre-record sidecar - never auto-re-cook
		}
		// an absent attribute stays "" (unset); matchesInputs treats missing
		// hashes as never-matching, so a hand-trimmed record reads as stale
		auto attribute = [cookElement](char const * name) -> String
		{
			const char * value = cookElement->Attribute(name);
			return value ? String(value) : String();
		};
		cook.tool = attribute("tool");
		cook.sourceHash = attribute("sourceHash");
		cook.toolHash = attribute("toolHash");
		cook.settingsHash = attribute("settingsHash");
		cook.settings.clips = attribute("clips");
		cook.settings.extent = attribute("extent");
		cook.settings.tolerance = attribute("tolerance");
		return true;
	}
	//---------------------------------------------------------
	String AssetDatabase::metaFilePathForId(String const & assetId) const
	{
		const String relativePath = this->pathForId(assetId);
		if (relativePath.empty() || this->mRootDirectory.empty())
		{
			return String();
		}
		return (std::filesystem::path(this->mRootDirectory) / relativePath)
			.string() + META_FILE_EXTENSION;
	}
	//---------------------------------------------------------
	void AssetDatabase::setActive(optr<AssetDatabase> const & database)
	{
		sActive = database;
	}
	//---------------------------------------------------------
	optr<AssetDatabase> const & AssetDatabase::getActive()
	{
		return sActive;
	}
	//---------------------------------------------------------
	String AssetDatabase::referenceIdForValue(String const & value,
		String const & storedId, ReferenceKind kind)
	{
		if (value.empty())
		{
			return String();
		}
		if (sActive)
		{
			const String currentId = (kind == REF_FILE_NAME)
				? sActive->idForFileName(value) : sActive->idForPath(value);
			if (!currentId.empty())
			{
				return currentId;
			}
		}
		return storedId;
	}
	//---------------------------------------------------------
	void AssetDatabase::resolveReference(String & valueInOut,
		String & assetIdInOut, ReferenceKind kind)
	{
		if (!sActive)
		{
			return; // nothing to check against - both carry as read
		}
		if (!assetIdInOut.empty())
		{
			const String relativePath = sActive->pathForId(assetIdInOut);
			if (!relativePath.empty())
			{
				// the id wins over a possibly stale value - the fixed-up
				// value upgrades the scene on the next save
				const String current = (kind == REF_FILE_NAME)
					? std::filesystem::path(relativePath).filename().string()
					: relativePath;
				if (current != valueInOut)
				{
					assetLog("AssetDatabase: reference '" + valueInOut +
						"' resolved by id " + assetIdInOut + " to '" +
						current + "'");
					valueInOut = current;
				}
				return;
			}
		}
		// no id / the id does not resolve: the legacy value carries; when it
		// names a known asset the id self-heals to that asset's
		const String currentId = (kind == REF_FILE_NAME)
			? sActive->idForFileName(valueInOut)
			: sActive->idForPath(valueInOut);
		if (!currentId.empty())
		{
			assetIdInOut = currentId;
		}
	}
	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	bool AssetDatabase::resolveInsideRoot(String const & relativeOrAbsolute,
		std::filesystem::path & absoluteOut) const
	{
		if (this->mRootDirectory.empty() || relativeOrAbsolute.empty())
		{
			return false;
		}
		std::error_code fsError;
		std::filesystem::path absolute(relativeOrAbsolute);
		if (!absolute.is_absolute())
		{
			absolute = std::filesystem::path(this->mRootDirectory) / absolute;
		}
		absolute = std::filesystem::absolute(absolute, fsError)
			.lexically_normal();
		const std::filesystem::path relative =
			absolute.lexically_relative(this->mRootDirectory);
		// containment: an outside path relativizes to a ".."-led one. Compare
		// the first COMPONENT as a path (relative.native() is a wide string on
		// Windows, and a plain prefix test would also reject a file literally
		// named "..foo")
		const bool escapesRoot = relative.empty() ||
			(relative.begin() != relative.end() &&
				*relative.begin() == std::filesystem::path(".."));
		if (escapesRoot)
		{
			return false;
		}
		absoluteOut = absolute;
		return true;
	}
	//---------------------------------------------------------
	void AssetDatabase::scanDirectory(String const & directory,
		bool createSidecars)
	{
		std::error_code fsError;
		if (!std::filesystem::is_directory(directory, fsError))
		{
			return;
		}
		// deterministic order: collect and sort before touching anything
		std::vector<std::filesystem::path> assetFiles;
		std::vector<std::filesystem::path> metaFiles;
		for (std::filesystem::recursive_directory_iterator
			iterator(directory, fsError), end;
			!fsError && iterator != end; iterator.increment(fsError))
		{
			if (!iterator->is_regular_file(fsError) ||
				isHiddenFileName(iterator->path()))
			{
				continue;
			}
			if (isMetaFileName(iterator->path()))
			{
				metaFiles.push_back(iterator->path());
			}
			else
			{
				assetFiles.push_back(iterator->path());
			}
		}
		std::sort(assetFiles.begin(), assetFiles.end());
		std::sort(metaFiles.begin(), metaFiles.end());

		for (std::filesystem::path const & assetFile : assetFiles)
		{
			const String relativePath = assetFile.lexically_relative(
				this->mRootDirectory).generic_string();
			const String metaPath = assetFile.string() + META_FILE_EXTENSION;
			String assetId;
			const bool hasMeta = readMetaFile(metaPath, assetId);
			if (!hasMeta)
			{
				if (!createSidecars)
				{
					continue; // read-only: this asset simply has no id
				}
				assetId = generateId();
				if (!writeMetaFile(metaPath, assetId))
				{
					assetLog("AssetDatabase: could not write sidecar '" +
						metaPath + "' - '" + relativePath + "' stays id-less");
					continue;
				}
				assetLog("AssetDatabase: imported '" + relativePath +
					"' as " + assetId);
			}
			if (this->mIdToPath.find(assetId) != this->mIdToPath.end())
			{
				// duplicated id: a copied asset+sidecar pair - the copy gets
				// a fresh identity, read-only runs keep the
				// first (sorted) asset and skip the copy honestly
				if (!createSidecars)
				{
					assetLog("AssetDatabase: '" + relativePath +
						"' duplicates id " + assetId + " of '" +
						this->mIdToPath[assetId] + "' - skipped (read-only)");
					continue;
				}
				const String freshId = generateId();
				if (!writeMetaFile(metaPath, freshId))
				{
					assetLog("AssetDatabase: could not re-mint sidecar '" +
						metaPath + "' - '" + relativePath + "' skipped");
					continue;
				}
				assetLog("AssetDatabase: '" + relativePath +
					"' duplicated id " + assetId + " of '" +
					this->mIdToPath[assetId] + "' - re-minted as " + freshId);
				assetId = freshId;
			}
			this->registerAsset(relativePath, assetId);
		}

		// orphaned sidecars: the asset moved/vanished WITHOUT its sidecar -
		// no silent re-linking (content-hash matching is future work): the
		// import mode deletes the orphan (the moved asset was minted a fresh
		// id above), read-only runs just report it
		for (std::filesystem::path const & metaFile : metaFiles)
		{
			const String assetPath = metaFile.string().substr(0,
				metaFile.string().size() - META_FILE_EXTENSION.size());
			if (std::filesystem::exists(assetPath, fsError))
			{
				continue;
			}
			const String relativeMetaPath = metaFile.lexically_relative(
				this->mRootDirectory).generic_string();
			if (createSidecars)
			{
				std::filesystem::remove(metaFile, fsError);
				assetLog("AssetDatabase: dropped orphaned sidecar '" +
					relativeMetaPath + "' (its asset is gone - a moved asset "
					"without its sidecar got a fresh id)");
			}
			else
			{
				assetLog("AssetDatabase: orphaned sidecar '" +
					relativeMetaPath + "' (read-only - left in place)");
			}
		}
	}
	//---------------------------------------------------------
	void AssetDatabase::registerAsset(String const & relativePath,
		String const & assetId)
	{
		this->mIdToPath[assetId] = relativePath;
		this->mPathToId[relativePath] = assetId;
		const String fileName =
			std::filesystem::path(relativePath).filename().string();
		const std::map<String, String>::const_iterator existing =
			this->mFileNameToId.find(fileName);
		if (existing == this->mFileNameToId.end())
		{
			this->mFileNameToId[fileName] = assetId;
		}
		else if (existing->second != assetId)
		{
			assetLog("AssetDatabase: file name '" + fileName + "' is "
				"ambiguous ('" + this->mIdToPath[existing->second] + "' vs '" +
				relativePath + "') - name lookups keep the first; resource "
				"names should stay unique per project");
		}
	}
	//---------------------------------------------------------
}
