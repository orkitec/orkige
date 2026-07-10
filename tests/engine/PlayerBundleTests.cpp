// Unit tests for PlayerBundle (engine_runtime/PlayerRuntime.h) - the
// exported-app default-project mechanism: an exported bundle carries an
// orkige_project.txt marker next to its resources naming the bundled
// project, plus (optionally) the engine Media/ directory. Pure filesystem
// logic, exercised against a temp directory fixture; the SDL_GetBasePath
// default only kicks in for an empty baseDir and is not needed here.
#include <catch2/catch_test_macros.hpp>
#include <engine_runtime/PlayerRuntime.h>

#include <filesystem>
#include <fstream>
#ifdef _WIN32
#include <process.h>	// _getpid - unique temp fixture names (parallel ctest)
#define getpid _getpid
#else
#include <unistd.h> // getpid - unique temp fixture name
#endif

namespace
{
	//! a throwaway bundle-base directory, wiped on destruction
	struct BundleFixture
	{
		std::filesystem::path base;

		BundleFixture()
		{
			base = std::filesystem::temp_directory_path() /
				("orkige_playerbundle_test_" +
					std::to_string(::getpid()));
			std::filesystem::remove_all(base);
			std::filesystem::create_directories(base);
		}
		~BundleFixture()
		{
			std::error_code ignored;
			std::filesystem::remove_all(base, ignored);
		}
		void writeMarker(std::string const& content)
		{
			std::ofstream marker(base /
				Orkige::PlayerBundle::PROJECT_MARKER_FILE_NAME);
			marker << content;
		}
	};
}

TEST_CASE("no marker means no bundled project (the dev-run case)",
	"[engine][playerbundle]")
{
	BundleFixture fixture;
	CHECK(Orkige::PlayerBundle::findBundledProject(
		fixture.base.string()).empty());
}

TEST_CASE("marker resolves the bundled project against the base directory",
	"[engine][playerbundle]")
{
	BundleFixture fixture;
	std::filesystem::create_directories(fixture.base / "project");
	fixture.writeMarker("project\n");
	// with and without the trailing separator on the base dir
	CHECK(Orkige::PlayerBundle::findBundledProject(fixture.base.string())
		== (fixture.base / "project").string());
	CHECK(Orkige::PlayerBundle::findBundledProject(
		fixture.base.string() + "/") == (fixture.base / "project").string());
}

TEST_CASE("marker tolerates trailing whitespace and CRLF",
	"[engine][playerbundle]")
{
	BundleFixture fixture;
	std::filesystem::create_directories(fixture.base / "project");
	fixture.writeMarker("project\r\n");
	CHECK(Orkige::PlayerBundle::findBundledProject(fixture.base.string())
		== (fixture.base / "project").string());
}

TEST_CASE("marker naming a missing path is an honest miss",
	"[engine][playerbundle]")
{
	BundleFixture fixture;
	fixture.writeMarker("no_such_project\n");
	CHECK(Orkige::PlayerBundle::findBundledProject(
		fixture.base.string()).empty());
}

TEST_CASE("empty marker yields no bundled project", "[engine][playerbundle]")
{
	BundleFixture fixture;
	fixture.writeMarker("\n");
	CHECK(Orkige::PlayerBundle::findBundledProject(
		fixture.base.string()).empty());
}

TEST_CASE("bundled media overrides the fallback only when Media/Main exists",
	"[engine][playerbundle]")
{
	BundleFixture fixture;
	// no Media at all -> the build-tree fallback wins
	CHECK(Orkige::PlayerBundle::resolveMediaDirectory("/dev/fallback",
		fixture.base.string()) == "/dev/fallback");
	// a Media dir without Main/ is not the engine media - still the fallback
	std::filesystem::create_directories(fixture.base / "Media");
	CHECK(Orkige::PlayerBundle::resolveMediaDirectory("/dev/fallback",
		fixture.base.string()) == "/dev/fallback");
	// the real classic bundled layout wins (Media/Main = RTSS library)
	std::filesystem::create_directories(fixture.base / "Media" / "Main");
	CHECK(Orkige::PlayerBundle::resolveMediaDirectory("/dev/fallback",
		fixture.base.string()) == (fixture.base / "Media").string());
}

TEST_CASE("bundled next-flavor media (Media/Hlms) also overrides the fallback",
	"[engine][playerbundle]")
{
	BundleFixture fixture;
	// the Ogre-Next flavor bundles its Hlms shader templates instead of the
	// classic Main/RTShaderLib set - Media/Hlms alone marks a bundled Media
	std::filesystem::create_directories(fixture.base / "Media" / "Hlms");
	CHECK(Orkige::PlayerBundle::resolveMediaDirectory("/dev/fallback",
		fixture.base.string()) == (fixture.base / "Media").string());
}

TEST_CASE("empty base directory falls through safely",
	"[engine][playerbundle]")
{
	// an explicit "" asks for SDL_GetBasePath(); whatever it returns, a
	// test runner's directory carries no marker - the call must not throw
	// and the media resolution must keep the fallback
	CHECK(Orkige::PlayerBundle::resolveMediaDirectory("/dev/fallback")
		== "/dev/fallback");
}
