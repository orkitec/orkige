/**************************************************************
	created:	2026/07/09 at 10:00
	filename: 	SpriteAnimationComponent.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "engine_gocomponent/SpriteAnimationComponent.h"
#include "engine_gocomponent/SpriteComponent.h"
#include <core_game/GameObject.h>

#include <algorithm>
#include <cmath>

namespace Orkige
{
	IMPL_OWNED_EVENTTYPE(SpriteAnimationComponent, SpriteAnimationEndedEvent);
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	SpriteAnimationComponent::SpriteAnimationComponent()
	{
		// mirror AnimationComponent<->ModelComponent: the flipbook needs a
		// sibling sprite to drive, so adding it auto-adds the SpriteComponent
		this->addDependency<SpriteComponent>();
		this->mGridColumns = 1;
		this->mGridRows = 1;
		this->mDefaultClip = "";
		this->mCurrentClip = "";
		this->mElapsed = 0.0f;
		this->mSpeed = 1.0f;
		this->mCurrentFrame = -1;
		this->mPlaying = false;
		this->mStartedDefault = false;
		this->mEventData = onew(new StringUtil::StringObject(StringUtil::BLANK));
		this->setWantsUpdates(true);
	}
	//---------------------------------------------------------
	SpriteAnimationComponent::~SpriteAnimationComponent()
	{
	}
	//---------------------------------------------------------
	void SpriteAnimationComponent::setGrid(int columns, int rows)
	{
		this->mGridColumns = columns;
		this->mGridRows = rows;
	}
	//---------------------------------------------------------
	void SpriteAnimationComponent::addClip(String const & name, int startFrame,
		int frameCount, float fps, bool loop)
	{
		this->mClips[name] = Clip(startFrame, frameCount, fps, loop);
	}
	//---------------------------------------------------------
	void SpriteAnimationComponent::removeClip(String const & name)
	{
		this->mClips.erase(name);
		if(this->mCurrentClip == name)
		{
			this->mPlaying = false;
			this->mCurrentClip = "";
		}
	}
	//---------------------------------------------------------
	bool SpriteAnimationComponent::hasClip(String const & name) const
	{
		return this->mClips.find(name) != this->mClips.end();
	}
	//---------------------------------------------------------
	void SpriteAnimationComponent::setDefaultClip(String const & name)
	{
		this->mDefaultClip = name;
	}
	//---------------------------------------------------------
	bool SpriteAnimationComponent::play(String const & name)
	{
		if(!this->hasClip(name))
		{
			return false;
		}
		this->mCurrentClip = name;
		this->mElapsed = 0.0f;
		this->mCurrentFrame = -1;	// force the first applyFrame through
		this->mPlaying = true;
		this->applyFrame(this->mClips[name].startFrame);
		return true;
	}
	//---------------------------------------------------------
	void SpriteAnimationComponent::stop()
	{
		this->mPlaying = false;
	}
	//---------------------------------------------------------
	bool SpriteAnimationComponent::setClip(String const & name)
	{
		if(!this->hasClip(name))
		{
			return false;
		}
		this->mCurrentClip = name;
		this->mElapsed = 0.0f;
		this->mCurrentFrame = -1;
		this->applyFrame(this->mClips[name].startFrame);
		return true;
	}
	//---------------------------------------------------------
	void SpriteAnimationComponent::setFrame(int frameInClip)
	{
		ClipMap::const_iterator it = this->mClips.find(this->mCurrentClip);
		if(it == this->mClips.end())
		{
			return;
		}
		Clip const & clip = it->second;
		const int clamped = std::clamp(frameInClip, 0,
			std::max(0, clip.frameCount - 1));
		// keep the playback cursor in step with the manual jump so an ongoing
		// play() does not immediately snap back to the time-derived frame
		this->mElapsed = (clip.fps > 0.0f) ?
			static_cast<float>(clamped) / clip.fps : 0.0f;
		this->applyFrame(clip.startFrame + clamped);
	}
	//---------------------------------------------------------
	int SpriteAnimationComponent::frameForElapsed(float elapsed, float fps,
		int frameCount, bool loop, bool & ended)
	{
		ended = false;
		if(frameCount <= 1 || fps <= 0.0f)
		{
			// a single-frame (or rate-less) clip is done as soon as any time
			// passes when it does not loop; otherwise it sits on frame 0
			ended = !loop && frameCount >= 1 && elapsed * std::max(fps, 0.0f) > 0.0f;
			return 0;
		}
		const int rawFrame = static_cast<int>(std::floor(elapsed * fps));
		if(loop)
		{
			int frame = rawFrame % frameCount;
			if(frame < 0)
			{
				frame += frameCount;
			}
			return frame;
		}
		if(rawFrame >= frameCount)
		{
			ended = true;
			return frameCount - 1;
		}
		return (rawFrame < 0) ? 0 : rawFrame;
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------
	void SpriteAnimationComponent::onAdd()
	{
		// the dependency guarantees a sibling SpriteComponent exists
		oAssert(this->getSprite());
	}
	//---------------------------------------------------------
	void SpriteAnimationComponent::onRemove()
	{
	}
	//---------------------------------------------------------
	void SpriteAnimationComponent::save(optr<IArchive> const & ar)
	{
		OParent::save(ar);
		ar << this->mGridColumns << this->mGridRows;
		ar << this->mDefaultClip;
		// the clip catalogue: count, then each clip's name + fields (the map's
		// value type is a plain struct, so it is written field by field)
		ar << static_cast<unsigned int>(this->mClips.size());
		for(ClipMap::const_iterator it = this->mClips.begin();
			it != this->mClips.end(); ++it)
		{
			String name = it->first;
			Clip clip = it->second;
			ar << name;
			ar << clip.startFrame << clip.frameCount << clip.fps << clip.loop;
		}
		// the live playback cursor (current clip / elapsed / frame) does not
		// round-trip - a loaded component replays from its default clip
		oDebugMsg("scene", 0, "SpriteAnimationComponent: playback runtime state "
			"(current clip, elapsed time, frame) is not serialized yet");
	}
	//---------------------------------------------------------
	void SpriteAnimationComponent::load(optr<IArchive> const & ar)
	{
		OParent::load(ar);
		ar >> this->mGridColumns >> this->mGridRows;
		ar >> this->mDefaultClip;
		unsigned int clipCount = 0;
		ar >> clipCount;
		this->mClips.clear();
		for(unsigned int i = 0; i < clipCount; ++i)
		{
			String name;
			Clip clip;
			ar >> name;
			ar >> clip.startFrame >> clip.frameCount >> clip.fps >> clip.loop;
			this->mClips[name] = clip;
		}
	}
	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	void SpriteAnimationComponent::onUpdateComponent(float deltaTime)
	{
		// auto-play the default clip once (the AnimationComponent behaviour);
		// dormant in the editor, which never ticks GameObjects
		if(!this->mStartedDefault)
		{
			this->mStartedDefault = true;
			if(!this->mDefaultClip.empty())
			{
				this->play(this->mDefaultClip);
			}
		}
		if(!this->mPlaying)
		{
			return;
		}
		ClipMap::const_iterator it = this->mClips.find(this->mCurrentClip);
		if(it == this->mClips.end())
		{
			this->mPlaying = false;
			return;
		}
		Clip const & clip = it->second;
		this->mElapsed += deltaTime * this->mSpeed;
		bool ended = false;
		const int frameInClip = frameForElapsed(this->mElapsed, clip.fps,
			clip.frameCount, clip.loop, ended);
		const int absoluteFrame = clip.startFrame + frameInClip;
		if(absoluteFrame != this->mCurrentFrame)
		{
			this->applyFrame(absoluteFrame);
		}
		if(ended)
		{
			// stop on the last frame and announce it (clip name payload); the
			// name is copied out first so a listener re-triggering play() sees
			// a clean cursor
			this->mPlaying = false;
			const String endedClip = this->mCurrentClip;
			this->mEventData->setValue(endedClip);
			this->getComponentOwner()->triggerEvent(
				Event(SpriteAnimationEndedEvent, this->mEventData));
		}
	}
	//---------------------------------------------------------
	SpriteComponent* SpriteAnimationComponent::getSprite()
	{
		GameObject* owner = this->getComponentOwner();
		oAssert(owner);
		return owner->getComponentPtr<SpriteComponent>();
	}
	//---------------------------------------------------------
	void SpriteAnimationComponent::applyFrame(int absoluteFrame)
	{
		SpriteComponent* sprite = this->getSprite();
		if(!sprite)
		{
			return;
		}
		this->mCurrentFrame = absoluteFrame;
		float textureWidth = 0.0f;
		float textureHeight = 0.0f;
		sprite->getTextureSize(textureWidth, textureHeight);
		float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
		// the shared pixel-rect->UV primitive (with the half-texel inset when
		// the sprite has a loaded texture)
		SpriteComponent::frameToUVRect(absoluteFrame,
			this->mGridColumns, this->mGridRows,
			textureWidth, textureHeight, u0, v0, u1, v1);
		sprite->setUVRect(u0, v0, u1, v1);
	}
	//---------------------------------------------------------
	OOBJECT_IMPL(SpriteAnimationComponent)
		GAMEOBJECTCOMPONENT()
		OFUNC(setGrid)
		OFUNC(getGridColumns)
		OFUNC(getGridRows)
		OFUNC(addClip)
		OFUNC(removeClip)
		OFUNC(hasClip)
		OFUNC(getClipCount)
		OFUNC(setDefaultClip)
		OFUNCCR(getDefaultClip)
		OFUNC(play)
		OFUNC(stop)
		OFUNC(setClip)
		OFUNC(isPlaying)
		OFUNCCR(getCurrentClip)
		OFUNC(setFrame)
		OFUNC(getCurrentFrame)
		OFUNC(setSpeed)
		OFUNC(getSpeed)
	OOBJECT_END
}
