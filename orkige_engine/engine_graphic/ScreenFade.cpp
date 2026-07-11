/********************************************************************
	created:	Friday 2026/07/11 at 12:00
	filename: 	ScreenFade.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

#include "engine_graphic/ScreenFade.h"
#include "engine_render/RenderSystem.h"
#include "engine_render/DrawLayer2D.h"
#include "engine_render/RenderMath.h"
#include "core_tween/EaseLibrary.h"

#include <algorithm>

namespace Orkige
{
	IMPL_OSINGLETON(ScreenFade);

	// far above the gui HUD (z ~13-14 in the reference games) and the ImGui
	// editor layer, so the fade covers everything
	const int ScreenFade::Z_FADE = 10000;
	const int ScreenFade::HOLD_FRAMES = 2;

	//---------------------------------------------------------
	ScreenFade::ScreenFade()
		: mPhase(Phase::Idle)
		, mAlpha(0.0f)
		, mElapsed(0.0f)
		, mOutSeconds(0.0f)
		, mInSeconds(0.0f)
		, mHoldRemaining(0)
		, mAutoIn(false)
		, mColorR(0.0f)
		, mColorG(0.0f)
		, mColorB(0.0f)
	{
	}
	//---------------------------------------------------------
	ScreenFade::~ScreenFade()
	{
	}
	//---------------------------------------------------------
	float ScreenFade::alphaAt(Phase phase, float elapsed, float duration)
	{
		switch(phase)
		{
		case Phase::Idle:
			return 0.0f;
		case Phase::Hold:
			return 1.0f;
		case Phase::Out:
			if(duration <= 0.0f)
			{
				return 1.0f;	// instantaneous
			}
			return Ease::sineInOut(std::clamp(elapsed / duration, 0.0f, 1.0f));
		case Phase::In:
			if(duration <= 0.0f)
			{
				return 0.0f;	// instantaneous
			}
			return 1.0f -
				Ease::sineInOut(std::clamp(elapsed / duration, 0.0f, 1.0f));
		}
		return 0.0f;
	}
	//---------------------------------------------------------
	void ScreenFade::fadeOut(float seconds)
	{
		this->mPhase = Phase::Out;
		this->mElapsed = 0.0f;
		this->mOutSeconds = seconds;
		this->mAutoIn = false;
		this->mAtOpaque = std::function<void()>();
	}
	//---------------------------------------------------------
	void ScreenFade::fadeIn(float seconds)
	{
		this->mPhase = Phase::In;
		this->mElapsed = 0.0f;
		this->mInSeconds = seconds;
		this->mAutoIn = false;
	}
	//---------------------------------------------------------
	void ScreenFade::transition(float outSeconds, float inSeconds,
		std::function<void()> atOpaque)
	{
		this->mPhase = Phase::Out;
		this->mElapsed = 0.0f;
		this->mOutSeconds = outSeconds;
		this->mInSeconds = inSeconds;
		this->mAutoIn = true;
		this->mAtOpaque = std::move(atOpaque);
	}
	//---------------------------------------------------------
	void ScreenFade::setFadeColor(float r, float g, float b)
	{
		this->mColorR = r;
		this->mColorG = g;
		this->mColorB = b;
	}
	//---------------------------------------------------------
	void ScreenFade::update(float deltaTime)
	{
		switch(this->mPhase)
		{
		case Phase::Idle:
			break;
		case Phase::Out:
			this->mElapsed += deltaTime;
			this->mAlpha = alphaAt(Phase::Out, this->mElapsed, this->mOutSeconds);
			if(this->mElapsed >= this->mOutSeconds)
			{
				this->mAlpha = 1.0f;
				// fire the at-opaque hook ONCE (the deferred scene-load request),
				// then hold opaque so the switch applies while the screen is covered
				if(this->mAtOpaque)
				{
					std::function<void()> hook = this->mAtOpaque;
					this->mAtOpaque = std::function<void()>();
					hook();
				}
				this->mPhase = Phase::Hold;
				this->mHoldRemaining = HOLD_FRAMES;
			}
			break;
		case Phase::Hold:
			this->mAlpha = 1.0f;
			if(this->mHoldRemaining > 0)
			{
				--this->mHoldRemaining;
			}
			else if(this->mAutoIn)
			{
				this->mAutoIn = false;
				this->mPhase = Phase::In;
				this->mElapsed = 0.0f;
			}
			break;
		case Phase::In:
			this->mElapsed += deltaTime;
			this->mAlpha = alphaAt(Phase::In, this->mElapsed, this->mInSeconds);
			if(this->mElapsed >= this->mInSeconds)
			{
				this->mAlpha = 0.0f;
				this->mPhase = Phase::Idle;
			}
			break;
		}
		this->rebuildQuad();
	}
	//---------------------------------------------------------
	void ScreenFade::ensureLayer()
	{
		if(this->mLayer || !RenderSystem::get())
		{
			return;
		}
		this->mLayer = RenderSystem::get()->createDrawLayer2D(Z_FADE);
	}
	//---------------------------------------------------------
	void ScreenFade::rebuildQuad()
	{
		if(!RenderSystem::get())
		{
			return;	// no renderer (editor) - honest no-op
		}
		this->ensureLayer();
		if(!this->mLayer)
		{
			return;
		}
		if(this->mAlpha <= 0.0f)
		{
			// nothing to draw - drop the batch and hide the layer
			this->mLayer->clear();
			this->mLayer->setVisible(false);
			return;
		}
		unsigned int width = 0;
		unsigned int height = 0;
		RenderSystem::get()->getWindowSize(width, height);
		const Real w = static_cast<Real>(width);
		const Real h = static_cast<Real>(height);
		const Color colour(this->mColorR, this->mColorG, this->mColorB,
			this->mAlpha);
		// a full-window quad as two triangles (pixel space, top-left origin)
		const DrawLayer2D::Vertex2D vertices[6] =
		{
			DrawLayer2D::Vertex2D(0, 0, 0, 0, colour),
			DrawLayer2D::Vertex2D(w, 0, 1, 0, colour),
			DrawLayer2D::Vertex2D(w, h, 1, 1, colour),
			DrawLayer2D::Vertex2D(0, 0, 0, 0, colour),
			DrawLayer2D::Vertex2D(w, h, 1, 1, colour),
			DrawLayer2D::Vertex2D(0, h, 0, 1, colour),
		};
		this->mLayer->setVisible(true);
		this->mLayer->clear();
		this->mLayer->addTriangles(String(), vertices, 6);
	}
}
