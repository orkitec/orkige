/********************************************************************
	created:	Saturday 2026/07/12 at 17:30
	filename: 	AnimationPreviewStage.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

//! @file AnimationPreviewStage.cpp
//! @brief the shared vector-animation preview stage (@see AnimationPreviewStage.h)

#include "AnimationPreviewStage.h"

#include <core_util/PngWriter.h>
#include <core_util/VectorShapeRaster.h>
#include <engine_render/RenderSystem.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>

namespace OrkigeEditor
{
	using Orkige::VectorAnimAsset;
	using Orkige::VectorAnimEval;
	using Orkige::VectorTessellator;

	//---------------------------------------------------------
	AnimationPreviewStage::AnimationPreviewStage() {}
	//---------------------------------------------------------
	AnimationPreviewStage::~AnimationPreviewStage()
	{
		if (Orkige::RenderSystem * render = Orkige::RenderSystem::get())
		{
			for (std::string const & name : this->mUploadNames)
			{
				if (!name.empty())
				{
					render->destroyTexture2D(name);
				}
			}
		}
	}
	//---------------------------------------------------------
	void AnimationPreviewStage::invalidateRender()
	{
		this->mPixelsDirty = true;
		this->mTextureDirty = true;
	}
	//---------------------------------------------------------
	bool AnimationPreviewStage::load(std::string const & projectRoot,
		std::string const & animRelPath, std::string & outError)
	{
		if (animRelPath.empty())
		{
			this->clear();
			outError.clear();
			return true;
		}
		const std::filesystem::path abs =
			std::filesystem::path(projectRoot) / animRelPath;
		std::ifstream in(abs, std::ios::binary);
		if (!in)
		{
			this->mLastError = "no such file: " + abs.string();
			outError = this->mLastError;
			return false;
		}
		std::stringstream buffer;
		buffer << in.rdbuf();
		VectorAnimAsset::Document doc;
		if (!VectorAnimAsset::parse(buffer.str(), doc))
		{
			this->mLastError = "'" + animRelPath + "' is not a valid .oanim "
				"(parse failed - a static document cooks to .oshape instead)";
			outError = this->mLastError;
			return false;
		}
		if (!this->mEval.build(doc))
		{
			this->mLastError = "'" + animRelPath + "' has no evaluable rig";
			outError = this->mLastError;
			return false;
		}
		this->mLoadedFile = animRelPath;
		this->mClipIndex = 0;
		this->mTimeSeconds = 0.0f;
		this->mBlendClipIndex = -1;
		this->mBlendWeight = 0.0f;
		this->mPlaying = false;
		this->invalidateRender();
		this->mLastError.clear();
		outError.clear();
		return true;
	}
	//---------------------------------------------------------
	void AnimationPreviewStage::clear()
	{
		this->mEval = VectorAnimEval();
		this->mLoadedFile.clear();
		this->mClipIndex = 0;
		this->mTimeSeconds = 0.0f;
		this->mBlendClipIndex = -1;
		this->mBlendWeight = 0.0f;
		this->mPlaying = false;
		this->mVertexCount = 0;
		this->mVisiblePixelCount = 0;
		this->mColouredPixelCount = 0;
		this->invalidateRender();
	}
	//---------------------------------------------------------
	void AnimationPreviewStage::setSize(int side)
	{
		const int resolved = side < 16 ? 16 : (side > 2048 ? 2048 : side);
		if (this->mSide != resolved)
		{
			this->mSide = resolved;
			this->invalidateRender();
		}
	}
	//---------------------------------------------------------
	int AnimationPreviewStage::resolvedClipIndex() const
	{
		const int count = static_cast<int>(this->mEval.document().clips.size());
		if (count <= 0)
		{
			return -1;
		}
		if (this->mClipIndex < 0 || this->mClipIndex >= count)
		{
			return 0;
		}
		return this->mClipIndex;
	}
	//---------------------------------------------------------
	bool AnimationPreviewStage::setClipByName(std::string const & name)
	{
		const int index = this->mEval.findClip(name);
		if (index < 0)
		{
			return false;
		}
		this->setClipIndex(index);
		return true;
	}
	//---------------------------------------------------------
	void AnimationPreviewStage::setClipIndex(int index)
	{
		const int count = static_cast<int>(this->mEval.document().clips.size());
		if (index < 0 || index >= count)
		{
			return;
		}
		this->mClipIndex = index;
		this->mTimeSeconds = 0.0f;
		this->invalidateRender();
	}
	//---------------------------------------------------------
	void AnimationPreviewStage::setTimeSeconds(float seconds)
	{
		const float resolved = seconds < 0.0f ? 0.0f : seconds;
		if (this->mTimeSeconds != resolved)
		{
			this->mTimeSeconds = resolved;
			this->invalidateRender();
		}
	}
	//---------------------------------------------------------
	bool AnimationPreviewStage::setBlend(std::string const & clipName,
		float weight)
	{
		if (clipName.empty())
		{
			this->clearBlend();
			return true;
		}
		const int index = this->mEval.findClip(clipName);
		if (index < 0)
		{
			return false;
		}
		this->mBlendClipIndex = index;
		this->mBlendWeight = weight < 0.0f ? 0.0f : (weight > 1.0f ? 1.0f : weight);
		this->invalidateRender();
		return true;
	}
	//---------------------------------------------------------
	void AnimationPreviewStage::clearBlend()
	{
		this->mBlendClipIndex = -1;
		this->mBlendWeight = 0.0f;
		this->invalidateRender();
	}
	//---------------------------------------------------------
	void AnimationPreviewStage::tick(float deltaSeconds)
	{
		if (this->mPlaying && this->isLoaded() && deltaSeconds > 0.0f)
		{
			this->mTimeSeconds += deltaSeconds;
			this->invalidateRender();
		}
	}
	//---------------------------------------------------------
	void AnimationPreviewStage::render()
	{
		if (!this->mPixelsDirty)
		{
			return;
		}
		const std::size_t pixelBytes =
			static_cast<std::size_t>(this->mSide) * this->mSide * 4;
		this->mPixels.assign(pixelBytes, 0);
		this->mVertexCount = 0;
		this->mVisiblePixelCount = 0;
		this->mColouredPixelCount = 0;
		this->mPixelsDirty = false;
		if (!this->isLoaded())
		{
			return;
		}
		const int clip = this->resolvedClipIndex();
		if (clip < 0)
		{
			return;
		}
		this->mEval.evaluateAt(clip, this->mTimeSeconds, this->mPoseA);
		VectorAnimEval::Pose const * posed = &this->mPoseA;
		if (this->mBlendClipIndex >= 0 && this->mBlendWeight > 0.0f)
		{
			this->mEval.evaluateAt(this->mBlendClipIndex, this->mTimeSeconds,
				this->mPoseB);
			if (VectorAnimEval::blendPose(this->mPoseA, this->mPoseB,
				this->mBlendWeight, this->mPoseBlend))
			{
				posed = &this->mPoseBlend;
			}
		}
		this->mEval.composeRegions(*posed, this->mRegions);
		const VectorTessellator::Bounds bounds =
			VectorTessellator::computeBounds(this->mRegions);
		VectorTessellator::build(this->mRegions,
			VectorTessellator::defaultFeatherWidth(bounds), this->mMesh);
		this->mVertexCount = static_cast<int>(this->mMesh.positions.size());
		if (this->mMesh.indices.empty())
		{
			return;
		}
		Orkige::VectorShapeRaster::rasterize(this->mMesh, this->mSide,
			this->mSide, this->mPixels.data());
		for (std::size_t pixel = 0; pixel + 3 < this->mPixels.size(); pixel += 4)
		{
			if (this->mPixels[pixel + 3] > 8)
			{
				++this->mVisiblePixelCount;
				if (this->mPixels[pixel] < 245 ||
					this->mPixels[pixel + 1] < 245 ||
					this->mPixels[pixel + 2] < 245)
				{
					++this->mColouredPixelCount;
				}
			}
		}
	}
	//---------------------------------------------------------
	std::string AnimationPreviewStage::uploadTexture()
	{
		Orkige::RenderSystem * render = Orkige::RenderSystem::get();
		if (!render)
		{
			return "";
		}
		if (!this->mTextureDirty && this->mActiveUpload >= 0)
		{
			return this->mUploadNames[this->mActiveUpload];
		}
		this->render();
		if (this->mPixels.empty())
		{
			return "";
		}
		const int nextUpload = (this->mActiveUpload + 1) % 2;
		if (this->mUploadNames[nextUpload].empty())
		{
			const std::string base = "__oanimpreview_" +
				std::to_string(std::hash<std::string>{}(
					std::to_string(reinterpret_cast<std::uintptr_t>(this))));
			this->mUploadNames[nextUpload] = base + "_" +
				std::to_string(nextUpload);
		}
		if (!render->createTexture2D(this->mUploadNames[nextUpload],
			this->mPixels.data(),
			static_cast<unsigned int>(this->mSide),
			static_cast<unsigned int>(this->mSide)))
		{
			return "";
		}
		this->mActiveUpload = nextUpload;
		this->mTextureDirty = false;
		return this->mUploadNames[this->mActiveUpload];
	}
	//---------------------------------------------------------
	bool AnimationPreviewStage::renderToPng(std::string const & pngPath,
		std::string & outError)
	{
		if (!this->isLoaded())
		{
			this->mLastError = "nothing is loaded in the animation preview";
			outError = this->mLastError;
			return false;
		}
		this->render();
		if (!Orkige::PngWriter::writeFile(pngPath, this->mPixels.data(),
			this->mSide, this->mSide))
		{
			this->mLastError = "could not write the preview PNG: " + pngPath;
			outError = this->mLastError;
			return false;
		}
		outError.clear();
		return true;
	}
	//---------------------------------------------------------
	AnimationPreviewInfo AnimationPreviewStage::getInfo() const
	{
		AnimationPreviewInfo info;
		if (!this->isLoaded())
		{
			return info;
		}
		VectorAnimAsset::Document const & doc = this->mEval.document();
		info.fps = doc.fps;
		info.durationFrames = doc.duration;
		info.layerCount = static_cast<int>(doc.layers.size());
		info.shapeCount = static_cast<int>(this->mEval.shapeCount());
		info.vertexCount = this->mVertexCount;
		info.visiblePixelCount = this->mVisiblePixelCount;
		info.colouredPixelCount = this->mColouredPixelCount;
		for (VectorAnimAsset::Clip const & clip : doc.clips)
		{
			info.clipNames.push_back(clip.name);
		}
		const int clip = this->resolvedClipIndex();
		info.clipIndex = clip;
		if (clip >= 0 && clip < static_cast<int>(doc.clips.size()))
		{
			VectorAnimAsset::Clip const & c = doc.clips[clip];
			info.clipName = c.name;
			info.clipDurationSeconds = doc.fps > 0.0f
				? (c.end - c.start) / doc.fps : 0.0f;
			info.frame = doc.fps > 0.0f
				? (c.start + this->mTimeSeconds * doc.fps) : c.start;
			// clamp the reported frame into the clip window (loop wraps, but the
			// readback reports the sampled point, so a plain clamp reads honest)
			if (info.frame > c.end)
			{
				info.frame = c.loop
					? (c.start + std::fmod(this->mTimeSeconds * doc.fps,
						(c.end - c.start <= 0.0f ? 1.0f : c.end - c.start)))
					: c.end;
			}
			info.atEnd = !c.loop && (this->mTimeSeconds * doc.fps) >=
				(c.end - c.start);
		}
		info.timeSeconds = this->mTimeSeconds;
		info.blending = this->mBlendClipIndex >= 0 && this->mBlendWeight > 0.0f;
		info.blendClipIndex = this->mBlendClipIndex;
		info.blendWeight = this->mBlendWeight;
		if (this->mBlendClipIndex >= 0 &&
			this->mBlendClipIndex < static_cast<int>(doc.clips.size()))
		{
			info.blendClipName = doc.clips[this->mBlendClipIndex].name;
		}
		return info;
	}
}
