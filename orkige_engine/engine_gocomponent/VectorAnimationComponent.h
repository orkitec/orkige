/********************************************************************
	created:	Saturday 2026/07/12 at 15:00
	filename: 	VectorAnimationComponent.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __VectorAnimationComponent_h__12_7_2026__15_00_00__
#define __VectorAnimationComponent_h__12_7_2026__15_00_00__

#include <core_game/GameObjectComponent.h>
#include "engine_module/EnginePrerequisites.h"
#include "engine_render/VectorMesh.h"
#include "engine_util/SceneNodeGuard.h"
#include "core_util/VectorTessellator.h"
#include "core_util/VectorAnimEval.h"
#include "core_util/StringUtil.h"

#include <set>
#include <vector>

namespace Orkige
{
	//! @brief an animated flat-colour organic vector rig in the XY plane - the
	//! animated sibling of VectorShapeComponent.
	//! @remarks Structurally the VectorShapeComponent recipe (needs a sibling
	//! TransformComponent, owns a child scene node, one facade VectorMesh, a
	//! per-instance tint / scale / zOrder / visibility, dormant-in-editor tick)
	//! wrapped around a `.oanim` rig instead of a static `.oshape`. The animation
	//! is parsed and handed to the pure VectorAnimEval (@see VectorAnimEval): per
	//! gameplay tick the evaluator advances the playing clip (both clips during a
	//! crossfade), composes the parent chain into a world-space region list, and
	//! this component tessellates that pose and pushes it through the DYNAMIC
	//! VectorMesh::updateVertices fast path. The rig's topology is fixed across
	//! frames (the cook/parser guarantee it), so the mesh keeps one index buffer;
	//! only vertex positions and colours move - the same painter-order zOrder
	//! window as sprites and shapes, one draw call per character.
	//!
	//! The playback surface mirrors SpriteAnimationComponent (the house clip
	//! idiom): named clips carved into the asset, play/stop/setClip/speed, a
	//! default clip, and a VectorAnimationEndedEvent when a non-looping clip
	//! reaches its end. Blending between clips is the pose-level crossFade
	//! (setClip with a transition time) VectorAnimEval provides - a locomotion
	//! blend that reads as one rig moving between poses.
	//!
	//! Like ScriptComponent and VectorShapeComponent's soft body, playback is
	//! DORMANT unless a runtime ticks GameObjects: the editor never advances the
	//! clip, so a placed character shows its default clip's first pose (WYSIWYG),
	//! and playback pauses for free when the owning GameObject goes inactive.
	class ORKIGE_ENGINE_DLL VectorAnimationComponent
		: public GameObjectComponent, public SceneNodeGuard
	{
		OOBJECT(VectorAnimationComponent, GameObjectComponent)
		//--- Types -------------------------------------------------
	public:
		//! @brief triggered when a non-looping clip reached its end and stopped;
		//! the event payload string carries the clip name. Mirrored onto the
		//! ScriptEventBus as "animation.ended" (a {clip=} payload) like the
		//! gui/physics engine events.
		//! @ingroup EngineEvents
		DECL_EVENTTYPE(VectorAnimationEndedEvent);

		//! zOrder 0 renders in the middle of the sprite queue window; the clamp
		//! range mirrors SpriteQuad::ZORDER_MIN/MAX (shared 2D window)
		static const int ZORDER_MIN;	//!< lowest accepted zOrder (-40)
		static const int ZORDER_MAX;	//!< highest accepted zOrder (+40)
		//--- Variables ---------------------------------------------
	protected:
		optr<VectorMesh>	mMesh;			//!< the facade mesh or NULL
		String				mAnimName;		//!< ".oanim" resource name or empty
		String				mAnimAssetId;	//!< stable asset id (rename survival)
		VectorAnimEval		mEval;			//!< the pure pose evaluator (built per asset)

		//--- reflected playback state ---
		String				mClip;			//!< the clip started on load (empty = the rig's first clip)
		float				mSpeed;			//!< playback speed multiplier (default 1)
		bool				mPlaying;		//!< is a clip advancing (default true)
		float				mTransitionTime;	//!< default crossfade seconds for setClip
		Color				mTint;			//!< multiplied over the asset's fill colours (default white)
		float				mScale;			//!< uniform rig scale in world units (default 1)
		float				mEdgeSoftness;	//!< feather width, rig-local (0 = auto from bounds)
		int					mZOrder;		//!< rig sort order (see remarks)
		bool				mVisible;		//!< rig visibility (applied to the scene node)

		//--- live playback cursor (NOT serialized) ---
		bool				mFreshBuild;	//!< a setMesh happened; defer the first dynamic upload one frame
		bool				mNeedsUpload;	//!< a playback change needs the pose re-uploaded next tick
		bool				mWasAtEnd;		//!< the last tick was already at a `once` clip's end (fire the event once)
		std::set<String>	mWarnedClips;	//!< clip names already warned about (once per name per rig; off the tick path)

		//--- reused per-frame buffers (allocation-free once warm) ---
		std::vector<VectorTessellator::Region>	mScratchRegions;	//!< composed world-space pose
		VectorTessellator::Mesh	mBuilt;		//!< tessellated pose (positions/colours/indices)
		std::vector<VectorMesh::Vertex>	mVertices;	//!< facade upload buffer (tinted)
		optr<StringUtil::StringObject>	mEventData;	//!< ended-clip name payload
	private:
		//--- Methods -----------------------------------------------
	public:
		//! constructor
		VectorAnimationComponent();
		//! destructor
		virtual ~VectorAnimationComponent();

		//! @brief load + build an `.oanim` rig (resolved across ALL resource
		//! groups, AUTODETECT - engine media and project assets both work). A
		//! missing/malformed animation is an error log, not a crash: the rig
		//! stays empty. Replaces any current rig.
		void loadAnimation(String const & animName);
		//! remove the rig mesh (keeps the node)
		void removeAnimation();
		//! @see VectorAnimationComponent::mAnimName
		inline String const & getAnimationName() const { return this->mAnimName; }
		//! is a rig currently loaded
		inline bool hasAnimation() const { return this->mMesh != nullptr; }
		//! triangles in the built pose (0 when empty) - selfcheck/introspection
		std::size_t getTriangleCount() const;
		//! vertices in the built pose (0 when empty) - selfcheck/introspection
		std::size_t getVertexCount() const;
		//! @brief a cheap deterministic signature of the CURRENT pose (the sum of
		//! |x|+|y| over the composed vertices): it changes as the pose animates,
		//! so a selfcheck detects motion / a crossfade blend by comparing it
		//! across ticks. 0 when empty.
		float getPoseSignature() const;

		//--- playback surface (SpriteAnimationComponent idiom) ---
		//! @brief select a clip and start it from its first frame (playing). An
		//! optional transition > 0 crossfades from the current clip instead of a
		//! hard cut. @return false when no such clip exists (playback unchanged)
		bool play(String const & clip);
		//! stop playback (holds the current pose shown); resume with play()
		void stop();
		//! @brief switch to a clip, optionally crossfading over transitionSeconds
		//! (<= 0 or omitted uses the default transitionTime, itself possibly 0 =
		//! a hard cut). Keeps the playing flag. @return false when unknown
		bool setClip(String const & clip, float transitionSeconds);
		//! @brief crossfade to a clip over seconds (an explicit-time setClip; the
		//! blend keeps the outgoing clip alive as the new one ramps in)
		bool crossFade(String const & clip, float seconds);
		//! @brief jump the current clip to an absolute time in seconds WITHOUT
		//! changing the playing flag (a scrub/preview seam)
		void scrub(float timeSeconds);
		//! is a clip currently advancing
		inline bool isPlaying() const { return this->mPlaying; }
		//! the selected/playing clip name (empty = none / no rig)
		String currentClip() const;
		//! number of clips the loaded rig carries
		int getClipCount() const;
		//! @brief the rig's clip names as a comma-separated list (empty = none)
		String getClipNames() const;
		//! the current absolute timeline frame of the playing clip (0 when empty)
		float currentFrame() const;
		//! has a `once` clip reached its end (loops never end)
		bool isAtEnd() const;

		//! playback speed multiplier (1 = the asset's authored rate)
		void setSpeed(float speed);
		//! @see VectorAnimationComponent::mSpeed
		inline float getSpeed() const { return this->mSpeed; }

		//--- reflected property accessors ---
		//! @brief set the animation REFERENCE by name (the reflected AssetRef
		//! setter): empty removes the rig; a name builds it when the scene node
		//! exists, otherwise records the reference (detached load).
		void setAnimationReference(String const & animName);
		//! the clip started on load (reflected)
		void setClipProperty(String const & clip) { this->mClip = clip; }
		//! @see VectorAnimationComponent::mClip
		inline String const & getClipProperty() const { return this->mClip; }
		//! reflected playing setter: turning play ON with no current clip starts
		//! the default; OFF just stops
		void setPlayingState(bool playing);
		//! default crossfade seconds used by setClip when no time is passed
		void setTransitionTime(float seconds);
		//! @see VectorAnimationComponent::mTransitionTime
		inline float getTransitionTime() const { return this->mTransitionTime; }
		//! colour tint, multiplied over the asset's fill colours (default white)
		void setTint(float red, float green, float blue, float alpha);
		//! current tint
		inline Color const & getTint() const { return this->mTint; }
		//! reflected tint setter (Color -> the four-float setTint)
		inline void setTintColor(Color const & tint)
		{
			this->setTint(tint.r, tint.g, tint.b, tint.a);
		}
		//! @brief uniform rig scale in world units (applied to the node)
		void setScale(float scale);
		//! @see VectorAnimationComponent::mScale
		inline float getScale() const { return this->mScale; }
		//! @brief feather (soft edge) width in rig-local units; <= 0 derives a
		//! default from the pose bounds
		void setEdgeSoftness(float width);
		//! @see VectorAnimationComponent::mEdgeSoftness
		inline float getEdgeSoftness() const { return this->mEdgeSoftness; }
		//! rig sort order (clamped to ZORDER_MIN..ZORDER_MAX; see remarks)
		void setZOrder(int zOrder);
		//! @see VectorAnimationComponent::mZOrder
		inline int getZOrder() const { return this->mZOrder; }
		//! show/hide the rig (the scene node's visibility)
		void setAnimationVisible(bool visible);
		//! is the rig visible (true when no mesh exists yet - it will show)
		bool isAnimationVisible() const { return this->mVisible; }
	protected:
		//! component override: create the child scene node (SpriteComponent recipe)
		virtual void onAdd();
		//! component override: drop the mesh + node
		virtual void onRemove();
		//! deactivated GameObjects hide their rig (setAnimationVisible state kept)
		virtual void onSetActive(bool activeInHierarchy);
		//! per-tick playback: advance the clip(s), compose the pose and upload
		//! the moved vertices (dormant unless a runtime ticks GameObjects)
		virtual void onUpdateComponent(float deltaTime);
		//--- SERIALIZATION ---
		//! save the animation AssetRef + playback settings (clip/speed/playing/
		//! transition) and the tint/scale/zOrder/visibility state
		virtual void save(optr<IArchive> const & ar);
		//! load the animation state from Archive (and build the rig when attached)
		virtual void load(optr<IArchive> const & ar);
	private:
		//! @brief select the clip the reflected state asks for (mClip, else the
		//! rig's first clip) and set the initial playing flag; sizes the mesh to
		//! the resulting pose
		void applyInitialClip();
		//! @brief compose the current evaluator pose into mScratchRegions,
		//! tessellate it into mBuilt and convert to the tinted mVertices upload
		//! buffer. The pose topology is fixed, so the vertex COUNT is stable.
		void buildVerticesFromPose();
		//! push the built vertices as a fresh mesh (setMesh - topology + indices)
		void pushMesh();
		//! push the built vertices through the DYNAMIC path (updateVertices),
		//! honouring the one-frame deferral after a setMesh and the fixed count
		void uploadPose();
		//! resolve a clip name to its evaluator index (-1 when unknown/no rig)
		int clipIndex(String const & clip) const;
		//! @brief warn (once per name per loaded rig) that a requested clip
		//! does not exist on this rig, naming the available clips - the typo/
		//! renamed-clip diagnostic. Setter-path only, never the tick.
		void warnUnknownClip(String const & clip);
		//! apply the EFFECTIVE visibility to the node (own flag AND owner active)
		void applyVisibility();
		//! push zOrder + node scale onto the live mesh/node
		void applyStateToMesh();
		//! fire the ended event (owned + ScriptEventBus mirror) for a clip
		void fireEnded(String const & clip);
	};
}

#endif //__VectorAnimationComponent_h__12_7_2026__15_00_00__
