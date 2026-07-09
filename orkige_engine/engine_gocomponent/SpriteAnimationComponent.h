/********************************************************************
	created:	Wednesday 2026/07/09 at 10:00
	filename: 	SpriteAnimationComponent.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __SpriteAnimationComponent_h__9_7_2026__10_00_00__
#define __SpriteAnimationComponent_h__9_7_2026__10_00_00__

#include <core_game/GameObjectComponent.h>
#include "engine_module/EnginePrerequisites.h"
#include "core_util/StringUtil.h"

#include <map>

namespace Orkige
{
	class SpriteComponent;

	//! @brief flipbook (sprite-sheet) animation driver for a sibling
	//! SpriteComponent - the 2D counterpart of AnimationComponent (which
	//! drives ModelComponent's skeletal animation).
	//! @remarks Mirrors the AnimationComponent<->ModelComponent dependency:
	//! adding this component auto-adds a SpriteComponent, and this component
	//! keeps ALL animation state - the SpriteComponent stays the static-quad
	//! primitive. The sheet is described as a uniform grid
	//! (gridColumns x gridRows of frames, row-major from the top-left) and
	//! named clips carve out a run of frames (startFrame, frameCount, fps,
	//! loop). Playback accumulates elapsed time, derives the current frame
	//! and drives the sibling SpriteComponent::setUVRect through the shared
	//! SpriteComponent::frameToUVRect primitive (with the half-texel inset
	//! against atlas-seam bleeding). A non-looping clip reaching its end
	//! stops and fires SpriteAnimationEndedEvent (clip name payload).
	//!
	//! Ticking runs through setWantsUpdates + onUpdateComponent (the
	//! AnimationComponent pattern), so playback is DORMANT in the editor
	//! (which never ticks GameObjects) - a placed sprite shows its first
	//! frame - and pauses for free when the owning GameObject goes inactive
	//! (GameObjectManager gates the tick on activeInHierarchy). Only the grid
	//! and the clip catalogue serialize; the live playback cursor
	//! (current clip / elapsed / frame) does not round-trip (logs a warning,
	//! like AnimationComponent).
	class ORKIGE_ENGINE_DLL SpriteAnimationComponent : public GameObjectComponent
	{
		OOBJECT(SpriteAnimationComponent, GameObjectComponent)
		//--- Types -------------------------------------------------
	public:
		//! @brief triggered when a non-looping clip reached its last frame and
		//! stopped; the event payload string carries the clip name
		//! @ingroup EngineEvents
		DECL_EVENTTYPE(SpriteAnimationEndedEvent);

		//! @brief a named run of grid frames
		struct Clip
		{
			inline Clip();
			inline Clip(int _startFrame, int _frameCount, float _fps, bool _loop);
			int		startFrame;		//!< first grid frame of the clip
			int		frameCount;		//!< number of frames the clip spans
			float	fps;			//!< playback rate in frames per second
			bool	loop;			//!< wrap at the end (true) or stop+event
		};
		typedef std::map<String, Clip> ClipMap;
	protected:
	private:
		//--- Variables ---------------------------------------------
	public:
	protected:
		int			mGridColumns;	//!< sheet columns (frames per row)
		int			mGridRows;		//!< sheet rows
		ClipMap		mClips;			//!< named clips (grid-frame runs)
		String		mDefaultClip;	//!< clip auto-played on the first tick
		// --- runtime playback cursor (NOT serialized) ---
		String		mCurrentClip;	//!< the playing/selected clip or empty
		float		mElapsed;		//!< seconds into the current clip
		float		mSpeed;			//!< playback speed multiplier (default 1)
		int			mCurrentFrame;	//!< last applied absolute grid frame (-1 = none)
		bool		mPlaying;		//!< is a clip advancing
		bool		mStartedDefault;	//!< the auto-play-default kick fired once
		optr<StringUtil::StringObject> mEventData;	//!< ended-clip name payload
	private:
		//--- Methods -----------------------------------------------
	public:
		//! constructor
		SpriteAnimationComponent();
		//! destructor
		virtual ~SpriteAnimationComponent();

		//! set the sheet grid (columns x rows of frames)
		void setGrid(int columns, int rows);
		//! @see mGridColumns
		inline int getGridColumns() const;
		//! @see mGridRows
		inline int getGridRows() const;

		//! @brief add (or replace) a named clip carving a run of grid frames
		void addClip(String const & name, int startFrame, int frameCount,
			float fps, bool loop);
		//! remove a clip; stops playback when the removed clip was playing
		void removeClip(String const & name);
		//! is a clip of that name defined
		bool hasClip(String const & name) const;
		//! number of defined clips
		inline int getClipCount() const;
		//! read-only access to the clip catalogue
		inline ClipMap const & getClips() const;

		//! the clip auto-played on the first tick (empty = none)
		void setDefaultClip(String const & name);
		//! @see mDefaultClip
		inline String const & getDefaultClip() const;

		//! @brief select a clip and start it from frame 0 (playing)
		//! @return false when no such clip exists (playback unchanged)
		bool play(String const & name);
		//! stop playback (keeps the current frame shown); resume with play()
		void stop();
		//! @brief select a clip and reset to its first frame WITHOUT changing
		//! the playing flag (a scrub/preview seam); false when unknown
		bool setClip(String const & name);
		//! is a clip currently advancing
		inline bool isPlaying() const;
		//! the selected/playing clip name (empty = none)
		inline String const & getCurrentClip() const;
		//! @brief jump to a frame WITHIN the current clip (0-based, clamped);
		//! applies it to the sprite immediately, leaves the playing flag alone
		void setFrame(int frameInClip);
		//! the last applied absolute grid frame (-1 = none applied yet)
		inline int getCurrentFrame() const;
		//! playback speed multiplier (1 = the clip's own fps)
		inline void setSpeed(float speed);
		//! @see mSpeed
		inline float getSpeed() const;

		//--- pure helper (headless-testable, no renderer required) ---
		//! @brief the frame index WITHIN a clip for an elapsed time
		//! @param elapsed seconds into the clip (>= 0)
		//! @param fps the clip's frame rate
		//! @param frameCount the clip length in frames
		//! @param loop wrap (true) or clamp+end (false)
		//! @param ended out: set true when a non-looping clip has passed its
		//! last frame (the caller stops and fires the end event)
		//! @return the frame index in [0, frameCount-1]
		static int frameForElapsed(float elapsed, float fps, int frameCount,
			bool loop, bool & ended);
	protected:
		//! component override gets called after the component is attached
		virtual void onAdd();
		//! component override gets called before the component is removed
		virtual void onRemove();
		//--- SERIALIZATION ---
		//! save the grid, the clip catalogue and the default clip
		//! @warning the live playback cursor does not round-trip (a warning is
		//! logged, matching AnimationComponent) - a loaded component shows its
		//! default clip's first frame once ticked
		virtual void save(optr<IArchive> const & ar);
		//! @warning restores the grid + clips only; playback state is not read
		virtual void load(optr<IArchive> const & ar);
	private:
		//! overridable to update the component (the per-frame tick)
		virtual void onUpdateComponent(float deltaTime);
		//! the sibling SpriteComponent (never NULL after onAdd's dependency)
		SpriteComponent* getSprite();
		//! compute the UV rect for an absolute grid frame and push it onto the
		//! sibling sprite (with the half-texel inset from the sprite's texture)
		void applyFrame(int absoluteFrame);
	};
	//---------------------------------------------------------------
	inline SpriteAnimationComponent::Clip::Clip()
		: startFrame(0), frameCount(1), fps(1.0f), loop(true)
	{
	}
	//---------------------------------------------------------------
	inline SpriteAnimationComponent::Clip::Clip(int _startFrame, int _frameCount, float _fps, bool _loop)
		: startFrame(_startFrame), frameCount(_frameCount), fps(_fps), loop(_loop)
	{
	}
	//---------------------------------------------------------------
	inline int SpriteAnimationComponent::getGridColumns() const
	{
		return this->mGridColumns;
	}
	//---------------------------------------------------------------
	inline int SpriteAnimationComponent::getGridRows() const
	{
		return this->mGridRows;
	}
	//---------------------------------------------------------------
	inline int SpriteAnimationComponent::getClipCount() const
	{
		return static_cast<int>(this->mClips.size());
	}
	//---------------------------------------------------------------
	inline SpriteAnimationComponent::ClipMap const & SpriteAnimationComponent::getClips() const
	{
		return this->mClips;
	}
	//---------------------------------------------------------------
	inline String const & SpriteAnimationComponent::getDefaultClip() const
	{
		return this->mDefaultClip;
	}
	//---------------------------------------------------------------
	inline bool SpriteAnimationComponent::isPlaying() const
	{
		return this->mPlaying;
	}
	//---------------------------------------------------------------
	inline String const & SpriteAnimationComponent::getCurrentClip() const
	{
		return this->mCurrentClip;
	}
	//---------------------------------------------------------------
	inline int SpriteAnimationComponent::getCurrentFrame() const
	{
		return this->mCurrentFrame;
	}
	//---------------------------------------------------------------
	inline void SpriteAnimationComponent::setSpeed(float speed)
	{
		this->mSpeed = speed;
	}
	//---------------------------------------------------------------
	inline float SpriteAnimationComponent::getSpeed() const
	{
		return this->mSpeed;
	}
}

#endif //__SpriteAnimationComponent_h__9_7_2026__10_00_00__
