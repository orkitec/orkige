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
	const String AssetDatabase::REFERENCE_ID_ATTRIBUTE = "assetId";

	namespace
	{
		//! read one <texture>/<android>/<ios> element's attributes onto
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
		}
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
		return this->base;
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
		if (this->mRootDirectory.empty() || assetPath.empty())
		{
			return String();
		}
		std::error_code fsError;
		std::filesystem::path absolute(assetPath);
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
		if (escapesRoot ||
			!std::filesystem::is_regular_file(absolute, fsError))
		{
			return String(); // not a file inside this project
		}
		const String relativePath = relative.generic_string();
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
				// a fresh identity (Unity behavior), read-only runs keep the
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
