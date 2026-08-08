/********************************************************************
	created:	Saturday 2026/08/08 at 10:30
	filename: 	ShaderMediaDirTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

// Headless coverage for the one rule both flavors resolve their shader source
// directory through (engine_util/ShaderMediaDir.h). It decides where a run
// reads the shader files that reload_shaders re-reads, so the rule that matters
// is that an override is all-or-nothing: half of one template set beside half
// of another would produce shaders nobody authored. Runs on both flavors - the
// function is pure and flavor-neutral.
#include <catch2/catch_test_macros.hpp>
#include <engine_util/ShaderMediaDir.h>

using Orkige::resolveShaderMediaDir;

TEST_CASE("with no override the host's path stands", "[engine][render][shaders]")
{
	// unset: the ordinary run - the baked build-tree default, or the media
	// directory a bundled/exported run carries
	CHECK(resolveShaderMediaDir("/engine/media/rtss", NULL) ==
		"/engine/media/rtss");
	CHECK(resolveShaderMediaDir("/app/Media/Hlms", NULL) == "/app/Media/Hlms");
	// an EMPTY variable is not an override - an exported shell that clears a
	// variable must not silently unregister the shader tree
	CHECK(resolveShaderMediaDir("/engine/media/rtss", "") ==
		"/engine/media/rtss");
}

TEST_CASE("an override replaces the host's path whole", "[engine][render][shaders]")
{
	// the scratch-copy case: a run reads (and may edit) a copy of the shader
	// tree instead of the engine's own media
	CHECK(resolveShaderMediaDir("/engine/media/rtss", "/scratch/rtss") ==
		"/scratch/rtss");
	// nothing is merged, appended or resolved against the host path
	const Orkige::String resolved =
		resolveShaderMediaDir("/engine/media/rtss", "relative/copy");
	CHECK(resolved == "relative/copy");
	CHECK(resolved.find("/engine/media/rtss") == Orkige::String::npos);
}

TEST_CASE("an override stands with no host path at all", "[engine][render][shaders]")
{
	// a host that derived nothing still gets the override, and an empty answer
	// is what the callers read as "register no shader directory"
	CHECK(resolveShaderMediaDir("", "/scratch/hlms") == "/scratch/hlms");
	CHECK(resolveShaderMediaDir("", NULL).empty());
	CHECK(resolveShaderMediaDir("", "").empty());
}
