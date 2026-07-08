/********************************************************************
	created:	Wednesday 2026/07/08 at 20:00
	filename: 	smoke_main.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

//! @file smoke_main.cpp
//! @brief render_next_smoke - the B1 bar of a NEW render backend
//! @remarks Much smaller than the render_facade_selfcheck conformance
//! suite (which needs the full content surface and stays DISABLED on
//! the next flavor until B2): backend boots into a real window, a
//! facade camera goes on the window, the window clears to a known
//! non-black colour, a facade screenshot proves it, clean shutdown,
//! exit 0. Backend-agnostic by construction (facade headers + the
//! SelfcheckBootstrap seam only); registered for the next flavor -
//! classic's selfcheck covers strictly more.

#include "SelfcheckBootstrap.h"

#include <engine_render/RenderPrerequisites.h>
#include <engine_render/RenderMath.h>
#include <engine_render/RenderSystem.h>
#include <engine_render/RenderWorld.h>
#include <engine_render/RenderNode.h>
#include <engine_render/RenderCamera.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

using namespace Orkige;

#define SMOKECHECK(condition, description) \
	do \
	{ \
		if(!(condition)) \
		{ \
			std::fprintf(stderr, "render_next_smoke: FAILED - %s " \
				"(%s:%d: %s)\n", description, __FILE__, __LINE__, \
				#condition); \
			return 1; \
		} \
		std::printf("render_next_smoke: ok - %s\n", description); \
	} while(false)

static int runSmoke(RenderSystem* renderSystem, std::string const & outDir)
{
	SMOKECHECK(renderSystem != NULL, "backend booted a render system");
	SMOKECHECK(RenderSystem::get() == renderSystem,
		"RenderSystem::get() returns the booted system");
	RenderWorld* world = renderSystem->getWorld();
	SMOKECHECK(world != NULL, "the render system carries a world");

	//--- the minimal window rig: camera on a node, shown full-window ----
	optr<RenderCamera> camera = world->createCamera("smoke.camera");
	SMOKECHECK(camera != NULL, "createCamera works");
	optr<RenderNode> cameraNode = world->createNode("smoke.cameraNode");
	SMOKECHECK(cameraNode != NULL, "createNode works");
	cameraNode->setPosition(Vec3(0, 0, 5));
	camera->attachTo(cameraNode);
	camera->setPerspective(Degree(60), Real(0.1), Real(100));
	renderSystem->showCameraOnWindow(camera);
	SMOKECHECK(renderSystem->getWindowCamera() == camera,
		"the window shows the facade camera");

	//--- clear to a known non-black colour ------------------------------
	renderSystem->setWindowBackgroundColour(Color(0.1f, 0.4f, 0.8f));
	unsigned int width = 0, height = 0;
	renderSystem->getWindowSize(width, height);
	SMOKECHECK(width > 0 && height > 0, "the window reports a real size");

	bool quitRequested = false;
	for(int frame = 0; frame < 30; ++frame)
	{
		SelfcheckBootstrap::pumpHostEvents(quitRequested);
		SMOKECHECK(renderSystem->renderOneFrame(), "a frame rendered");
	}

	//--- facade screenshot proves the clear reached the pixels ----------
	const std::string windowShot = outDir + "/smoke_window.png";
	renderSystem->saveWindowContents(windowShot);
	SMOKECHECK(std::filesystem::exists(windowShot),
		"the facade screenshot wrote a file");
	SMOKECHECK(SelfcheckBootstrap::imageHasNonBlackPixel(windowShot),
		"the window screenshot is not black (the clear colour rendered)");

	//--- clean facade teardown ------------------------------------------
	camera.reset();
	cameraNode.reset();
	std::printf("render_next_smoke: all checks passed\n");
	return 0;
}

int main(int, char**)
{
	// screenshot output directory: ctest points this at the build tree;
	// interactive runs fall back to the system temp directory
	std::string outDir;
	if(const char* outEnv = std::getenv("ORKIGE_SELFCHECK_OUT"))
	{
		outDir = outEnv;
	}
	else
	{
		outDir = (std::filesystem::temp_directory_path() /
			"render_next_smoke").string();
	}
	std::error_code errorCode;
	std::filesystem::create_directories(outDir, errorCode);

	RenderSystem* renderSystem = SelfcheckBootstrap::boot(640, 480,
		outDir + "/render_next_smoke.log");
	if(!renderSystem)
	{
		std::fprintf(stderr, "render_next_smoke: FAILED - backend boot\n");
		return 1;
	}
	// note: the window camera handle held by the render system is released
	// by the backend teardown; the handles WE created died in runSmoke
	const int result = runSmoke(renderSystem, outDir);
	SelfcheckBootstrap::shutdown();
	return result;
}
