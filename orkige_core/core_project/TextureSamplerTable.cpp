/**************************************************************
	created:	2026/08/02 at 12:00
	filename: 	TextureSamplerTable.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "core_project/TextureSamplerTable.h"

#include "core_project/AssetDatabase.h"

#include <vector>

namespace Orkige
{
	optr<TextureSamplerTable> TextureSamplerTable::sActive;
	//---------------------------------------------------------
	bool TextureSampler::isDefault() const
	{
		const TextureSampler defaults;
		return *this == defaults;
	}
	//---------------------------------------------------------
	String TextureSamplerTable::keyFor(String const & textureReference)
	{
		// pure string work, deliberately: a reference is a resource name, not
		// a path the process may touch, and both separators can appear in one
		// (a scene authored on one host, read on another)
		const size_t slash = textureReference.find_last_of("/\\");
		const size_t nameStart =
			(slash == String::npos) ? 0 : slash + 1;
		const size_t dot = textureReference.find_last_of('.');
		const size_t nameEnd = (dot != String::npos && dot > nameStart)
			? dot : textureReference.size();
		return textureReference.substr(nameStart, nameEnd - nameStart);
	}
	//---------------------------------------------------------
	void TextureSamplerTable::set(String const & textureReference,
		TextureSampler const & sampler)
	{
		const String key = keyFor(textureReference);
		if (key.empty())
		{
			return;
		}
		if (sampler.isDefault())
		{
			// the table holds authored intent only - a default answer is what
			// an absent key already means
			this->mSamplers.erase(key);
			return;
		}
		this->mSamplers[key] = sampler;
	}
	//---------------------------------------------------------
	TextureSampler TextureSamplerTable::lookup(
		String const & textureReference) const
	{
		const std::map<String, TextureSampler>::const_iterator found =
			this->mSamplers.find(keyFor(textureReference));
		return (found != this->mSamplers.end()) ? found->second
			: TextureSampler();
	}
	//---------------------------------------------------------
	void TextureSamplerTable::fillFromAssets(AssetDatabase const & database,
		String const & platform)
	{
		this->mSamplers.clear();
		const std::vector<AssetEntry> assets = database.listAssets();
		for (AssetEntry const & asset : assets)
		{
			// the database owns the path arithmetic - this only asks it where
			// each asset's sidecar lives
			const String metaPath = database.metaFilePathForId(asset.id);
			if (metaPath.empty())
			{
				continue;
			}
			TextureImport import;
			if (!AssetDatabase::readImportSettings(metaPath, import))
			{
				continue;	// an id-only sidecar authors no sampler
			}
			TextureImportSettings const & settings =
				import.resolvedFor(platform);
			TextureSampler sampler;
			sampler.filter = settings.filter;
			sampler.wrap = settings.wrap;
			this->set(asset.relativePath, sampler);
		}
	}
	//---------------------------------------------------------
	void TextureSamplerTable::setActive(optr<TextureSamplerTable> const & table)
	{
		sActive = table;
	}
	//---------------------------------------------------------
	optr<TextureSamplerTable> const & TextureSamplerTable::getActive()
	{
		return sActive;
	}
	//---------------------------------------------------------
	TextureSampler TextureSamplerTable::resolve(
		String const & textureReference)
	{
		if (!sActive)
		{
			return TextureSampler();
		}
		return sActive->lookup(textureReference);
	}
	//---------------------------------------------------------
}
