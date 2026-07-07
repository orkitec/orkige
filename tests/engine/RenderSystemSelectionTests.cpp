// Unit tests for Engine::matchRenderSystemName - the pure matching rule
// behind Engine::setPreferredRenderSystem and the apps' ORKIGE_RENDERSYSTEM
// environment variable. Uses the real OGRE render system names as fixtures;
// no Ogre::Root is created.
#include <catch2/catch_test_macros.hpp>
#include <engine_graphic/Engine.h>

namespace
{
	const Orkige::String kGL3Plus = "OpenGL 3+ Rendering Subsystem";
	const Orkige::String kMetal = "Metal Rendering Subsystem";
	const Orkige::String kGL = "OpenGL Rendering Subsystem";
}

TEST_CASE("documented render system hints resolve", "[engine][rendersystem]")
{
	CHECK(Orkige::Engine::matchRenderSystemName(kMetal, "Metal"));
	CHECK(Orkige::Engine::matchRenderSystemName(kGL3Plus, "GL3Plus"));
	CHECK(Orkige::Engine::matchRenderSystemName(kGL3Plus, "GL3+"));
	CHECK(Orkige::Engine::matchRenderSystemName(kGL3Plus, "GL"));
	CHECK(Orkige::Engine::matchRenderSystemName(kGL, "GL"));
}

TEST_CASE("render system matching ignores case and spacing", "[engine][rendersystem]")
{
	CHECK(Orkige::Engine::matchRenderSystemName(kMetal, "metal"));
	CHECK(Orkige::Engine::matchRenderSystemName(kMetal, "METAL"));
	CHECK(Orkige::Engine::matchRenderSystemName(kGL3Plus, "opengl 3+"));
	CHECK(Orkige::Engine::matchRenderSystemName(kGL3Plus, "OpenGL 3+ Rendering Subsystem"));
}

TEST_CASE("render system hints do not match the wrong system", "[engine][rendersystem]")
{
	CHECK_FALSE(Orkige::Engine::matchRenderSystemName(kGL3Plus, "Metal"));
	CHECK_FALSE(Orkige::Engine::matchRenderSystemName(kMetal, "GL"));
	CHECK_FALSE(Orkige::Engine::matchRenderSystemName(kMetal, "GL3Plus"));
	CHECK_FALSE(Orkige::Engine::matchRenderSystemName(kMetal, "Vulkan"));
	CHECK_FALSE(Orkige::Engine::matchRenderSystemName(kGL, "GL3Plus"));
}

TEST_CASE("empty hint matches nothing (keeps first-available default)", "[engine][rendersystem]")
{
	CHECK_FALSE(Orkige::Engine::matchRenderSystemName(kMetal, ""));
	CHECK_FALSE(Orkige::Engine::matchRenderSystemName(kGL3Plus, ""));
}
