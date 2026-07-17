/********************************************************************
	created:	Thursday 2026/07/17 at 12:00
	filename: 	LightBudgetTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

// Headless per-flavor unit coverage for the numeric light-budget capability
// (RenderSystem::lightBudget / defaultLightBudget) - the sibling of the boolean
// RenderCaps register, tested the same way its snapshot .inc files are: the
// committed expected value per flavor is asserted against the pure per-backend
// constant WITHOUT booting a render system (no Ogre::Root / window / GPU). The
// live boot fill (system->mImpl->lightBudget) is asserted to equal this same
// defaultLightBudget() in the headed render_facade_selfcheck, so the two cannot
// drift. Which flavor this binary carries is the build-time ORKIGE_RENDER_*
// define (PUBLIC on orkige_engine), exactly as the flavor's backend links.
#include <catch2/catch_test_macros.hpp>
#include <engine_render/RenderSystem.h>

using Orkige::RenderSystem;

TEST_CASE("the flavor reports a sane dynamic-light budget", "[engine][render][lightbudget]")
{
	const unsigned int budget = RenderSystem::defaultLightBudget();

	// a real ceiling, never zero (0 is the "no backend yet" sentinel of the
	// instance lightBudget(), never a flavor's honest capability)
	CHECK(budget > 0u);

	// a SANE whole-scene ceiling, not the raw clustered cells*lightsPerCell
	// product (which would be hundreds of thousands) nor a fixed-function
	// 8-light floor: the many-lights showcase caps its ramp at this, so it must
	// bound a real point-light count without being absurd
	CHECK(budget >= 8u);
	CHECK(budget <= 4096u);

#if defined(ORKIGE_RENDER_NEXT)
	// the clustered-forward light-list bound (setForwardClustered lightsPerCell)
	// - the per-cluster worst case, the honest concurrent dynamic-light ceiling
	CHECK(budget == 96u);
#elif defined(ORKIGE_RENDER_CLASSIC)
	// the classic forward renderer's per-pass dynamic-light headroom
	CHECK(budget == 30u);
#else
#	error "no render backend flavor selected - ORKIGE_RENDER_NEXT/CLASSIC must be defined"
#endif
}
