/********************************************************************
	created:	Friday 2026/07/31 at 12:00
	filename: 	ExportFilesTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
/*
	The filesystem moves an export makes. The one that carries real risk is the
	tree copy: a bundle's dylib loader aliases and a framework's Versions/
	Current are SYMLINKS, and copying them as file duplicates produces an app
	that is both fatter and, for the dlopen probes, wrong - so that is asserted
	directly.
*/
#include <catch2/catch_test_macros.hpp>

#include "ExportFiles.h"

#include <filesystem>

using namespace OrkigeExport;

namespace
{
	struct ScratchDir
	{
		Orkige::String path;
		explicit ScratchDir(Orkige::String const & name)
		{
			this->path = (std::filesystem::temp_directory_path() /
				("orkige_files_test_" + name)).string();
			ExportFiles::removeTree(this->path, 0);
			ExportFiles::makeDirectories(this->path, 0);
		}
		~ScratchDir() { ExportFiles::removeTree(this->path, 0); }
	};
}

TEST_CASE("directories are created and removed recursively", "[unit][export]")
{
	ScratchDir scratch("dirs");
	const Orkige::String deep =
		ExportFiles::join(ExportFiles::join(scratch.path, "a"), "b/c");
	REQUIRE(ExportFiles::makeDirectories(deep, 0));
	CHECK(ExportFiles::isDirectory(deep));
	// creating an existing directory is a success, not an error
	CHECK(ExportFiles::makeDirectories(deep, 0));

	REQUIRE(ExportFiles::writeTextFile(ExportFiles::join(deep, "f.txt"), "x",
		0));
	REQUIRE(ExportFiles::removeTree(ExportFiles::join(scratch.path, "a"), 0));
	CHECK_FALSE(ExportFiles::exists(deep));
	// removing what is already gone is a success too
	CHECK(ExportFiles::removeTree(ExportFiles::join(scratch.path, "a"), 0));
}

TEST_CASE("text files round-trip with LF preserved", "[unit][export]")
{
	ScratchDir scratch("text");
	// the parent directory does not exist yet - writing creates it
	const Orkige::String path =
		ExportFiles::join(scratch.path, "sub/dir/marker.txt");
	REQUIRE(ExportFiles::writeTextFile(path, "project\n", 0));

	Orkige::String read;
	REQUIRE(ExportFiles::readTextFile(path, read, 0));
	// exactly the bytes written: no CRLF translation on any host
	CHECK(read == "project\n");

	Orkige::String error;
	CHECK_FALSE(ExportFiles::readTextFile(
		ExportFiles::join(scratch.path, "absent"), read, &error));
	CHECK_FALSE(error.empty());
}

TEST_CASE("copyTree stages a whole tree and counts it", "[unit][export]")
{
	ScratchDir scratch("tree");
	const Orkige::String source = ExportFiles::join(scratch.path, "src");
	REQUIRE(ExportFiles::writeTextFile(
		ExportFiles::join(source, "a.txt"), "a", 0));
	REQUIRE(ExportFiles::writeTextFile(
		ExportFiles::join(source, "sub/b.txt"), "b", 0));
	REQUIRE(ExportFiles::writeTextFile(
		ExportFiles::join(source, "sub/deep/c.txt"), "c", 0));

	const Orkige::String destination = ExportFiles::join(scratch.path, "dst");
	int staged = 0;
	REQUIRE(ExportFiles::copyTree(source, destination, 0, &staged));
	CHECK(staged == 3);
	CHECK(ExportFiles::isRegularFile(
		ExportFiles::join(destination, "sub/deep/c.txt")));

	// a second copy MERGES into the existing tree (an export lays several
	// sources into one Media/ directory)
	const Orkige::String other = ExportFiles::join(scratch.path, "other");
	REQUIRE(ExportFiles::writeTextFile(
		ExportFiles::join(other, "sub/d.txt"), "d", 0));
	REQUIRE(ExportFiles::copyTree(other, destination, 0, 0));
	CHECK(ExportFiles::isRegularFile(
		ExportFiles::join(destination, "sub/d.txt")));
	CHECK(ExportFiles::isRegularFile(
		ExportFiles::join(destination, "sub/b.txt")));

	CHECK(ExportFiles::countFiles(destination) == 4);

	// copying something that is not a directory refuses with a reason
	Orkige::String error;
	CHECK_FALSE(ExportFiles::copyTree(
		ExportFiles::join(source, "a.txt"),
		ExportFiles::join(scratch.path, "nope"), &error, 0));
	CHECK_FALSE(error.empty());
}

TEST_CASE("copyTree keeps symlinks as symlinks", "[unit][export]")
{
	ScratchDir scratch("symlinks");
	const Orkige::String source = ExportFiles::join(scratch.path, "src");
	REQUIRE(ExportFiles::writeTextFile(
		ExportFiles::join(source, "libfoo.1.2.3.dylib"), "binary", 0));
	// the dlopen alias shape: an unversioned name pointing at the real file
	REQUIRE(ExportFiles::makeSymlink("libfoo.1.2.3.dylib",
		ExportFiles::join(source, "libfoo.dylib"), 0));

	const Orkige::String destination = ExportFiles::join(scratch.path, "dst");
	int staged = 0;
	REQUIRE(ExportFiles::copyTree(source, destination, 0, &staged));
	// the link is a link, not a second copy of the payload
	CHECK(std::filesystem::is_symlink(std::filesystem::path(
		ExportFiles::join(destination, "libfoo.dylib"))));
	CHECK(staged == 1);	// only the real file counts as staged content
	CHECK(ExportFiles::countFiles(destination) == 1);
}

TEST_CASE("makeSymlink replaces an existing entry", "[unit][export]")
{
	ScratchDir scratch("relink");
	REQUIRE(ExportFiles::writeTextFile(
		ExportFiles::join(scratch.path, "real.dylib"), "x", 0));
	const Orkige::String link = ExportFiles::join(scratch.path, "alias.dylib");
	REQUIRE(ExportFiles::makeSymlink("real.dylib", link, 0));
	// re-staging the same bundle must not fail on the link it wrote last time
	REQUIRE(ExportFiles::makeSymlink("real.dylib", link, 0));
	CHECK(std::filesystem::is_symlink(std::filesystem::path(link)));
}

TEST_CASE("treeSize sums regular files only", "[unit][export]")
{
	ScratchDir scratch("size");
	const Orkige::String root = ExportFiles::join(scratch.path, "bundle");
	REQUIRE(ExportFiles::writeTextFile(ExportFiles::join(root, "a"), "12345",
		0));
	REQUIRE(ExportFiles::writeTextFile(ExportFiles::join(root, "sub/b"), "123",
		0));
	// a symlink must not double-count the file it points at
	REQUIRE(ExportFiles::makeSymlink("a", ExportFiles::join(root, "alias"), 0));
	CHECK(ExportFiles::treeSize(root) == 8);

	// a single file reports its own size
	CHECK(ExportFiles::treeSize(ExportFiles::join(root, "a")) == 5);
	// something absent weighs nothing rather than throwing
	CHECK(ExportFiles::treeSize(ExportFiles::join(root, "absent")) == 0);
}

TEST_CASE("listFilesRecursive is relative and sorted", "[unit][export]")
{
	ScratchDir scratch("list");
	const Orkige::String root = ExportFiles::join(scratch.path, "payload");
	REQUIRE(ExportFiles::writeTextFile(ExportFiles::join(root, "z.txt"), "z",
		0));
	REQUIRE(ExportFiles::writeTextFile(ExportFiles::join(root, "a.txt"), "a",
		0));
	REQUIRE(ExportFiles::writeTextFile(ExportFiles::join(root, "sub/m.txt"),
		"m", 0));

	const std::vector<Orkige::String> files =
		ExportFiles::listFilesRecursive(root);
	REQUIRE(files.size() == 3);
	// sorted, forward-slashed - a package's contents are written in ONE order
	// on every host
	CHECK(files[0] == "a.txt");
	CHECK(files[1] == "sub/m.txt");
	CHECK(files[2] == "z.txt");
}

TEST_CASE("path helpers", "[unit][export]")
{
	CHECK(ExportFiles::stem("/a/b/pose.svg") == "pose");
	CHECK(ExportFiles::stem("plain") == "plain");
	CHECK(ExportFiles::replaceExtension("/a/ball.png", "dds") == "/a/ball.dds");
	CHECK(ExportFiles::join("", "b") == "b");
	CHECK_FALSE(ExportFiles::absolute(".").empty());
}

TEST_CASE("join speaks ONE separator all the way through", "[unit][export]")
{
	// every path literal in the exporter is forward-slashed; on a host whose
	// separator is not '/' a join that kept them would hand back a path half
	// in each form - it opens, but it never compares equal to the same path
	// the filesystem named, which is what an export asserts on
	const char separator =
		static_cast<char>(std::filesystem::path::preferred_separator);
	const char foreign = (separator == '/') ? '\\' : '/';
	const Orkige::String joined = ExportFiles::join("a/b", "c/d");
	CHECK(joined == Orkige::String("a") + separator + "b" + separator + "c" +
		separator + "d");
	CHECK(joined.find(foreign) == Orkige::String::npos);
	// the same holds for the empty-left case, which returns the right side
	CHECK(ExportFiles::join("", "x/y").find(foreign) == Orkige::String::npos);
}

TEST_CASE("copyFile creates the destination parents", "[unit][export]")
{
	ScratchDir scratch("copyfile");
	const Orkige::String source = ExportFiles::join(scratch.path, "src.txt");
	REQUIRE(ExportFiles::writeTextFile(source, "payload", 0));
	const Orkige::String destination =
		ExportFiles::join(scratch.path, "deep/nested/dst.txt");
	REQUIRE(ExportFiles::copyFile(source, destination, 0));

	Orkige::String read;
	REQUIRE(ExportFiles::readTextFile(destination, read, 0));
	CHECK(read == "payload");
	// overwriting is allowed - an export re-stages into a directory it owns
	REQUIRE(ExportFiles::copyFile(source, destination, 0));

	Orkige::String error;
	CHECK_FALSE(ExportFiles::copyFile(
		ExportFiles::join(scratch.path, "absent"), destination, &error));
	CHECK_FALSE(error.empty());
}
