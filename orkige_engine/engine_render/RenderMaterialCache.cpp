/********************************************************************
	created:	Tuesday 2026/08/04 at 12:00
	filename: 	RenderMaterialCache.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#include "engine_render/RenderMaterialCache.h"

namespace Orkige
{
	//---------------------------------------------------------
	bool materialDescEqual(RenderMaterialDesc const & left,
		RenderMaterialDesc const & right)
	{
		return left.albedo == right.albedo
			&& left.albedoTexture == right.albedoTexture
			&& left.metalness == right.metalness
			&& left.roughness == right.roughness
			&& left.normalTexture == right.normalTexture
			&& left.emissive == right.emissive
			&& left.emissiveTexture == right.emissiveTexture
			&& left.alphaTest == right.alphaTest
			&& left.twoSided == right.twoSided;
	}
	//---------------------------------------------------------
	bool RenderMaterialCache::needsBuild(String const & name,
		void const * material, RenderMaterialDesc const & desc) const
	{
		if(material == NULL)
		{
			// nothing lives under that name yet - there is a build to do
			return true;
		}
		std::map<String, Entry>::const_iterator found = this->mEntries.find(name);
		if(found == this->mEntries.end())
		{
			return true;
		}
		// the entry describes the material object it was recorded against; a
		// different object under the same name is a different material
		if(found->second.material != material)
		{
			return true;
		}
		return !materialDescEqual(found->second.desc, desc);
	}
	//---------------------------------------------------------
	void RenderMaterialCache::recordBuilt(String const & name,
		void const * material, RenderMaterialDesc const & desc)
	{
		if(material == NULL)
		{
			// a build that produced nothing is not a build to remember
			this->forget(name);
			return;
		}
		Entry entry;
		entry.material = material;
		entry.desc = desc;
		this->mEntries[name] = entry;
		++this->mBuildCount;
	}
	//---------------------------------------------------------
	void RenderMaterialCache::forget(String const & name)
	{
		this->mEntries.erase(name);
	}
	//---------------------------------------------------------
	void RenderMaterialCache::clear()
	{
		this->mEntries.clear();
	}
	//---------------------------------------------------------
	RenderMaterialCache & RenderMaterialCache::shared()
	{
		static RenderMaterialCache instance;
		return instance;
	}
}
