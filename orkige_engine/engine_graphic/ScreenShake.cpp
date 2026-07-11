/********************************************************************
	created:	Friday 2026/07/11 at 12:00
	filename: 	ScreenShake.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

#include "engine_graphic/ScreenShake.h"
#include "engine_render/RenderSystem.h"
#include "engine_render/RenderCamera.h"
#include "engine_render/RenderNode.h"

#include <algorithm>
#include <cmath>

namespace Orkige
{
	//---------------------------------------------------------
	IMPL_OSINGLETON(ScreenShake);
	//---------------------------------------------------------
	const float ScreenShake::DEFAULT_FREQUENCY = 30.0f;
	//---------------------------------------------------------
	ScreenShake::ScreenShake()
		: mActive(false), mAmplitude(0.0f), mDuration(0.0f)
		, mFrequency(DEFAULT_FREQUENCY), mElapsed(0.0f)
		, mAppliedOffset(Vec3::ZERO), mAppliedResult(Vec3::ZERO)
		, mHasApplied(false)
	{
	}
	//---------------------------------------------------------
	ScreenShake::~ScreenShake()
	{
	}
	//---------------------------------------------------------
	void ScreenShake::sampleOffset(float amplitude, float duration,
		float frequency, float elapsed, float & outX, float & outY)
	{
		outX = 0.0f;
		outY = 0.0f;
		if(amplitude <= 0.0f || duration <= 0.0f || elapsed >= duration)
		{
			return;	// finished (or never started): exact zero -> exact restore
		}
		// linear decay to zero at the end; two incommensurate sinusoids give an
		// elliptical wander that reads as a shake rather than a slide
		const float decay = 1.0f - std::clamp(elapsed / duration, 0.0f, 1.0f);
		const float phase = elapsed * frequency * 6.2831853f;	// 2*pi
		const float magnitude = amplitude * decay;
		outX = magnitude * std::sin(phase);
		outY = magnitude * std::sin(phase * 1.37f + 1.7f);
	}
	//---------------------------------------------------------
	void ScreenShake::shake(float amplitude, float duration, float frequency)
	{
		if(amplitude <= 0.0f || duration <= 0.0f)
		{
			return;	// nothing to do
		}
		if(frequency <= 0.0f)
		{
			frequency = DEFAULT_FREQUENCY;
		}
		if(this->mActive)
		{
			// stack: take the stronger amplitude and the longer remaining time so
			// a fresh hit never weakens an ongoing one
			const float remaining = this->mDuration - this->mElapsed;
			this->mAmplitude = std::max(this->mAmplitude, amplitude);
			this->mDuration = std::max(remaining, duration);
			this->mFrequency = frequency;
			this->mElapsed = 0.0f;
		}
		else
		{
			this->mActive = true;
			this->mAmplitude = amplitude;
			this->mDuration = duration;
			this->mFrequency = frequency;
			this->mElapsed = 0.0f;
		}
	}
	//---------------------------------------------------------
	void ScreenShake::stop()
	{
		this->mActive = false;
		this->mElapsed = 0.0f;
		// restore the node we last offset, if it still lives
		optr<RenderNode> node = this->mAppliedNode.lock();
		if(this->mHasApplied && node)
		{
			node->setPosition(node->getPosition() - this->mAppliedOffset);
		}
		this->mHasApplied = false;
		this->mAppliedOffset = Vec3::ZERO;
	}
	//---------------------------------------------------------
	optr<RenderNode> ScreenShake::activeCameraNode() const
	{
		if(!RenderSystem::get())
		{
			return optr<RenderNode>();
		}
		optr<RenderCamera> camera = RenderSystem::get()->getWindowCamera();
		return camera ? camera->getNode() : optr<RenderNode>();
	}
	//---------------------------------------------------------
	void ScreenShake::update(float deltaTime)
	{
		optr<RenderNode> node = this->activeCameraNode();
		if(!node)
		{
			// no camera to shake (headless / UI-only window): drop any bookkeeping
			this->mHasApplied = false;
			return;
		}
		// (1) recover the base pose: if the node still holds EXACTLY what we left
		// last frame, it was untouched (the player's static default camera) so we
		// subtract our offset to get the base; otherwise the camera's own
		// placement rewrote it this frame (a CameraComponent rig / a script) and
		// the current position already IS the base.
		optr<RenderNode> lastNode = this->mAppliedNode.lock();
		Vec3 base = node->getPosition();
		if(this->mHasApplied && lastNode == node &&
			node->getPosition() == this->mAppliedResult)
		{
			base = node->getPosition() - this->mAppliedOffset;
		}
		this->mHasApplied = false;
		this->mAppliedOffset = Vec3::ZERO;

		if(!this->mActive)
		{
			// idle: leave the node at its base (already there) and stop tracking
			node->setPosition(base);
			return;
		}

		this->mElapsed += deltaTime;
		float localX = 0.0f;
		float localY = 0.0f;
		ScreenShake::sampleOffset(this->mAmplitude, this->mDuration,
			this->mFrequency, this->mElapsed, localX, localY);
		if(this->mElapsed >= this->mDuration)
		{
			// the shake just ended: base restored above, go idle (exact rest pose)
			this->mActive = false;
			node->setPosition(base);
			return;
		}
		// (2) apply the wobble along the camera's local right/up axes (camera-space
		// noise), so it reads the same whatever way the camera faces
		const Vec3 localOffset(localX, localY, 0.0f);
		const Vec3 worldOffset = node->getOrientation() * localOffset;
		const Vec3 result = base + worldOffset;
		node->setPosition(result);
		this->mAppliedNode = node;
		this->mAppliedOffset = worldOffset;
		this->mAppliedResult = result;
		this->mHasApplied = true;
	}
	//---------------------------------------------------------
}
