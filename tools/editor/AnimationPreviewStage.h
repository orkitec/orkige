/********************************************************************
	created:	Saturday 2026/07/12 at 17:30
	filename: 	AnimationPreviewStage.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __AnimationPreviewStage_h__12_7_2026__17_30_00__
#define __AnimationPreviewStage_h__12_7_2026__17_30_00__

//! @file AnimationPreviewStage.h
//! @brief the editor's vector-animation preview stage: a `.oanim` rig
//! evaluated on its OWN clock and CPU-rasterised into a small RGBA image.
//! ONE stage instance is shared by the Animation Preview panel (the human's
//! live scrub/blend view) and the preview_animation MCP verb (an agent's
//! screenshots) - the animation twin of GuiPreviewStage.
//!
//! Deliberately renderer-light: the editor never ticks GameObjects, so this
//! is NOT a scene render. It parses the `.oanim`, builds the pure
//! VectorAnimEval, evaluates a pose STATELESSLY (evaluateAt - a scrub, a blend
//! of a second clip), composes the region list, tessellates it once per pose
//! and fills it with VectorShapeRaster (the same flat-colour raster that draws
//! `.oshape` thumbnails). The result is a straight-RGBA8 buffer that the panel
//! uploads as a texture (createTexture2D) and the verb writes to a PNG
//! (PngWriter) - both headless-capable, both flavors, no offscreen target.

#include <core_util/VectorAnimAsset.h>
#include <core_util/VectorAnimEval.h>
#include <core_util/VectorTessellator.h>
#include <core_util/String.h>

#include <string>
#include <vector>

namespace OrkigeEditor
{
	//! @brief the introspection a preview readback returns (panel status line +
	//! the preview_animation verb's structuredContent)
	struct AnimationPreviewInfo
	{
		std::vector<std::string>	clipNames;		//!< every clip in the rig
		float		fps = 0.0f;						//!< document frame rate
		float		durationFrames = 0.0f;			//!< document timeline length
		int			clipIndex = -1;					//!< the evaluated clip
		std::string	clipName;						//!< its name ("" when none)
		float		clipDurationSeconds = 0.0f;		//!< the evaluated clip's length
		float		timeSeconds = 0.0f;				//!< time into the clip
		float		frame = 0.0f;					//!< absolute timeline frame
		int			layerCount = 0;					//!< rig layer count
		int			shapeCount = 0;					//!< rig shape-block count
		int			vertexCount = 0;				//!< vertices in the composed pose
		bool		atEnd = false;					//!< a `once` clip reached its end
		bool		blending = false;				//!< a blend clip is mixed in
		int			blendClipIndex = -1;			//!< the second clip ("" = none)
		std::string	blendClipName;
		float		blendWeight = 0.0f;				//!< the mix weight 0..1
	};

	//! @brief the shared vector-animation preview stage (@see the file comment)
	class AnimationPreviewStage
	{
	public:
		AnimationPreviewStage();
		~AnimationPreviewStage();

		//! @brief parse and build a project `.oanim` (project-relative path),
		//! replacing whatever was loaded. Resets the clock to clip 0 at time 0
		//! and drops any blend. An empty relative path clears the stage.
		//! @return false + getLastError() on a missing file / parse failure /
		//! a document that is not an animation.
		bool load(std::string const & projectRoot, std::string const & animRelPath,
			std::string & outError);
		//! @brief drop the loaded rig (the empty state)
		void clear();
		//! is a rig currently loaded and evaluable?
		bool isLoaded() const { return this->mEval.isBuilt(); }
		//! the project-relative path of the loaded rig ("" when none)
		std::string const & getLoadedFile() const { return this->mLoadedFile; }

		//! @brief the pixel size the pose is rasterised at (square). A change
		//! takes effect on the next render.
		void setSize(int side);
		int getSize() const { return this->mSide; }

		//! @brief select the primary clip by name (unknown = ignored, false).
		//! Resets the clip time to 0.
		bool setClipByName(std::string const & name);
		//! @brief select the primary clip by index (out of range = ignored).
		void setClipIndex(int index);
		int getClipIndex() const { return this->mClipIndex; }
		//! @brief scrub: set the time (seconds) into the current clip
		void setTimeSeconds(float seconds);
		float getTimeSeconds() const { return this->mTimeSeconds; }
		//! @brief the blend try-out: a SECOND clip mixed into the pose at the
		//! current time with a 0..1 weight (0 or an unset clip = no blend).
		//! name "" clears the blend. @return false on an unknown clip name.
		bool setBlend(std::string const & clipName, float weight);
		//! clear any blend clip
		void clearBlend();

		//! playing? (the panel's own clock advances the time while true)
		bool isPlaying() const { return this->mPlaying; }
		void setPlaying(bool playing) { this->mPlaying = playing; }
		//! @brief advance the OWN clock by deltaSeconds while playing (the panel
		//! calls this once per editor frame; loop/`once` handled by the clip)
		void tick(float deltaSeconds);

		//! @brief evaluate + compose + tessellate + rasterise the CURRENT pose
		//! into the internal RGBA buffer. Called by uploadTexture/renderToPng;
		//! safe to call directly (headless). No-op (transparent) when unloaded.
		void render();
		//! @brief render() then upload the buffer as a named texture the panel
		//! binds; @return the upload name ("" on failure). Idempotent name per
		//! stage, replaced each call. Needs a RenderSystem.
		std::string uploadTexture();
		//! @brief render() then write the buffer as a PNG. @return false +
		//! getLastError() when unloaded or the file cannot be written.
		bool renderToPng(std::string const & pngPath, std::string & outError);

		//! @brief the introspection for the current pose (@see AnimationPreviewInfo)
		AnimationPreviewInfo getInfo() const;
		//! the last failure message (set by load/renderToPng)
		std::string const & getLastError() const { return this->mLastError; }

	private:
		//! resolve mClipIndex into the valid range (0 when nothing selected)
		int resolvedClipIndex() const;

		Orkige::VectorAnimEval		mEval;			//!< the built rig (empty until load)
		std::string					mLoadedFile;	//!< project-relative .oanim ("" = none)
		std::string					mLastError;

		int							mSide = 256;	//!< raster side in pixels
		int							mClipIndex = 0;	//!< the primary clip
		float						mTimeSeconds = 0.0f;	//!< time into the clip
		int							mBlendClipIndex = -1;	//!< the blend clip (-1 = none)
		float						mBlendWeight = 0.0f;	//!< blend mix 0..1
		bool						mPlaying = false;	//!< the own clock advances

		// per-render scratch (sized on first use, then reused)
		Orkige::VectorAnimEval::Pose			mPoseA;		//!< primary-clip pose
		Orkige::VectorAnimEval::Pose			mPoseB;		//!< blend-clip pose
		Orkige::VectorAnimEval::Pose			mPoseBlend;	//!< blended output
		std::vector<Orkige::VectorTessellator::Region>	mRegions;	//!< composed world regions
		Orkige::VectorTessellator::Mesh			mMesh;		//!< tessellated pose
		std::vector<unsigned char>				mPixels;	//!< RGBA raster buffer
		int										mVertexCount = 0;	//!< composed pose vertices
		std::string								mUploadName;	//!< the panel texture name
	};
}

#endif //__AnimationPreviewStage_h__12_7_2026__17_30_00__
