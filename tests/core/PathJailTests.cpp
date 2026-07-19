/**************************************************************
	created:	2026/07/20 at 00:45
	filename: 	PathJailTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Adversarial unit tests for the PathJail containment primitive - the ONE
	guard against zip-slip (a malicious archive entry escaping extraction) and
	project-file traversal. Crafted hostile inputs (".."/absolute/drive/symlink)
	must be refused; a legitimate nested path must pass; a fuzz-style loop feeds
	random bytes and asserts "never escapes root, never crashes".
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "core_util/PathJail.h"

#include <filesystem>
#include <random>
#include <string>

using Orkige::PathJail::isSafeRelativeEntry;
using Orkige::PathJail::escapesRoot;
using Orkige::PathJail::resolveExtractPath;

TEST_CASE("PathJailSafeRelativeEntry", "[security][filesystem]")
{
	// legitimate relative names pass, including deep nesting and dotted files
	CHECK(isSafeRelativeEntry("foo.png"));
	CHECK(isSafeRelativeEntry("assets/textures/foo.png"));
	CHECK(isSafeRelativeEntry("a/b/c/d/e.bin"));
	CHECK(isSafeRelativeEntry("..foo"));			// starts with dots, not ".."
	CHECK(isSafeRelativeEntry(".hidden"));
	CHECK(isSafeRelativeEntry("dir/.keep"));
	CHECK(isSafeRelativeEntry("a/./b"));			// "." is harmless

	// the classic escapers are all refused
	CHECK_FALSE(isSafeRelativeEntry(""));
	CHECK_FALSE(isSafeRelativeEntry(".."));
	CHECK_FALSE(isSafeRelativeEntry("../evil"));
	CHECK_FALSE(isSafeRelativeEntry("../../evil"));
	CHECK_FALSE(isSafeRelativeEntry("foo/../../bar"));
	CHECK_FALSE(isSafeRelativeEntry("foo/../bar/../.."));
	CHECK_FALSE(isSafeRelativeEntry("a/b/../../../c"));

	// absolute + drive + UNC + backslash traversal
	CHECK_FALSE(isSafeRelativeEntry("/etc/evil"));
	CHECK_FALSE(isSafeRelativeEntry("/abs/x"));
	CHECK_FALSE(isSafeRelativeEntry("\\windows\\evil"));
	CHECK_FALSE(isSafeRelativeEntry("C:/evil"));
	CHECK_FALSE(isSafeRelativeEntry("c:\\evil"));
	CHECK_FALSE(isSafeRelativeEntry("\\\\server\\share\\evil"));
	CHECK_FALSE(isSafeRelativeEntry("..\\..\\evil"));	// windows-style traversal
	CHECK_FALSE(isSafeRelativeEntry("foo\\..\\..\\bar"));
}

TEST_CASE("PathJailEscapesRoot", "[security][filesystem]")
{
	namespace fs = std::filesystem;
	const fs::path root = fs::path("/project/root");

	// inside stays inside
	CHECK_FALSE(escapesRoot(root, fs::path("/project/root/a/b.png")));
	CHECK_FALSE(escapesRoot(root, root));	// the root itself is not an escape

	// outside escapes
	CHECK(escapesRoot(root, fs::path("/project/other/x")));
	CHECK(escapesRoot(root, fs::path("/etc/passwd")));
	CHECK(escapesRoot(root, fs::path("/project")));		// a parent escapes
}

TEST_CASE("PathJailEscapesRootPrefixSibling", "[security][filesystem]")
{
	namespace fs = std::filesystem;
	// "/project/rootier" must NOT be seen as contained in "/project/root"
	// (a naive string-prefix test would wrongly accept it)
	CHECK(escapesRoot(fs::path("/project/root"),
		fs::path("/project/rootier/x")));
}

TEST_CASE("PathJailResolveExtractPath", "[security][filesystem]")
{
	namespace fs = std::filesystem;
	std::error_code ec;
	const fs::path root = fs::temp_directory_path(ec) /
		fs::path("orkige_pathjail_extract_test");
	fs::remove_all(root, ec);
	fs::create_directories(root, ec);

	// a benign nested entry resolves to a path under root
	fs::path dest;
	REQUIRE(resolveExtractPath(root, "assets/textures/foo.png", dest));
	CHECK_FALSE(escapesRoot(root.lexically_normal(), dest));
	CHECK(dest == (root / "assets/textures/foo.png").lexically_normal());

	// every escaper is refused BEFORE any write, and never yields a path
	for (std::string const& evil : {
		std::string("../../evil"), std::string("/etc/evil"),
		std::string("foo/../../bar"), std::string(".."),
		std::string("C:/evil"), std::string("..\\..\\evil") })
	{
		fs::path out;
		INFO("entry: " << evil);
		CHECK_FALSE(resolveExtractPath(root, evil, out));
	}

	// symlink escape: a directory component that symlinks OUTSIDE root must be
	// caught by the canonical (symlink-resolving) re-check, even though the
	// lexical join looks contained. (Skipped where symlinks are unavailable.)
	const fs::path outside = fs::temp_directory_path(ec) /
		fs::path("orkige_pathjail_outside_test");
	fs::remove_all(outside, ec);
	fs::create_directories(outside, ec);
	fs::create_directory_symlink(outside, root / "link", ec);
	if (!ec)
	{
		fs::path throughLink;
		// lexically "link/evil" is inside root, but "link" resolves outside
		CHECK_FALSE(resolveExtractPath(root, "link/evil", throughLink));
	}

	fs::remove_all(root, ec);
	fs::remove_all(outside, ec);
}

TEST_CASE("PathJailFuzzNeverEscapes", "[security][filesystem]")
{
	namespace fs = std::filesystem;
	std::error_code ec;
	const fs::path root = (fs::temp_directory_path(ec) /
		fs::path("orkige_pathjail_fuzz_root")).lexically_normal();

	// a byte alphabet weighted toward the traversal-relevant characters
	static const char alphabet[] = "abc/\\.:..////\\\\.. ";
	std::mt19937 rng(0xC0FFEEu);
	std::uniform_int_distribution<int> lenDist(0, 24);
	std::uniform_int_distribution<int> charDist(0,
		static_cast<int>(sizeof(alphabet) - 2));

	for (int iter = 0; iter < 20000; ++iter)
	{
		std::string entry;
		const int len = lenDist(rng);
		for (int i = 0; i < len; ++i)
		{
			entry.push_back(alphabet[charDist(rng)]);
		}
		// the predicate must never throw
		const bool safe = isSafeRelativeEntry(entry);
		// resolveExtractPath must never throw and never yield an escaping path
		fs::path dest;
		const bool resolved = resolveExtractPath(root, entry, dest);
		if (resolved)
		{
			// a resolved path is ALWAYS contained (the core invariant)
			CHECK(safe);
			CHECK_FALSE(escapesRoot(root, dest.lexically_normal()));
		}
	}
	CHECK(true);	// reaching here means no exception/crash across the fuzz
}
