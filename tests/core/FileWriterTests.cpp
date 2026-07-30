/**************************************************************
	created:	2026/07/30 at 10:00
	filename: 	FileWriterTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include <core_filesystem/FileWriter.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

using Orkige::FileWriter;
using Orkige::String;

namespace
{
	//! a unique scratch directory removed when the test leaves
	struct Scratch
	{
		std::filesystem::path dir;
		Scratch()
		{
			this->dir = std::filesystem::temp_directory_path() /
				("orkige_filewriter_" + std::to_string(
					static_cast<unsigned long long>(
						std::chrono::steady_clock::now()
							.time_since_epoch().count())));
			std::filesystem::create_directories(this->dir);
		}
		~Scratch()
		{
			std::error_code ignored;
			std::filesystem::remove_all(this->dir, ignored);
		}
		String path(char const * name) const
		{
			return (this->dir / name).string();
		}
	};
	//! a file's bytes ("" when absent)
	String contents(String const & path)
	{
		std::ifstream file(path, std::ios::binary);
		if (!file)
		{
			return String();
		}
		return String((std::istreambuf_iterator<char>(file)),
			std::istreambuf_iterator<char>());
	}
	bool present(String const & path)
	{
		std::error_code ignored;
		return std::filesystem::exists(path, ignored);
	}
}

TEST_CASE("FileWriter streams bytes and commits them atomically", "[filesystem]")
{
	Scratch scratch;
	const String target = scratch.path("streamed.bin");
	FileWriter writer;
	String error;

	REQUIRE(writer.begin(target, error));
	CHECK(writer.isOpen());
	CHECK(writer.getPath() == target);
	REQUIRE(writer.write("hello ", 6, error));
	REQUIRE(writer.write("world", 5, error));
	CHECK(writer.getWritten() == 11);
	// nothing exists at the target until the commit - only the temp file does
	CHECK_FALSE(present(target));
	CHECK(present(target + ".orkpart"));

	REQUIRE(writer.commit(error));
	CHECK_FALSE(writer.isOpen());
	CHECK(contents(target) == "hello world");
	CHECK_FALSE(present(target + ".orkpart"));
}

TEST_CASE("FileWriter creates missing parent directories", "[filesystem]")
{
	Scratch scratch;
	const String target = scratch.path("a/b/c/deep.txt");
	String error;
	REQUIRE(FileWriter::writeWholeFile(target, "deep", error));
	CHECK(contents(target) == "deep");
}

TEST_CASE("An aborted FileWriter leaves the previous file untouched",
	"[filesystem]")
{
	Scratch scratch;
	const String target = scratch.path("kept.txt");
	String error;
	REQUIRE(FileWriter::writeWholeFile(target, "the good content", error));

	{
		FileWriter writer;
		REQUIRE(writer.begin(target, error));
		REQUIRE(writer.write("garbage", 7, error));
		writer.abort();
		CHECK_FALSE(writer.isOpen());
	}
	// THE contract: only a commit changes the target
	CHECK(contents(target) == "the good content");
	CHECK_FALSE(present(target + ".orkpart"));

	// the destructor is an abort too - an interrupted write cannot leak a
	// half-file over a good one
	{
		FileWriter writer;
		REQUIRE(writer.begin(target, error));
		REQUIRE(writer.write("more garbage", 12, error));
	}
	CHECK(contents(target) == "the good content");
	CHECK_FALSE(present(target + ".orkpart"));
}

TEST_CASE("FileWriter refuses impossible targets with a reason", "[filesystem]")
{
	Scratch scratch;
	FileWriter writer;
	String error;

	CHECK_FALSE(writer.begin("", error));
	CHECK_FALSE(error.empty());

	// a directory path under a regular FILE can never be created
	const String blocker = scratch.path("blocker");
	REQUIRE(FileWriter::writeWholeFile(blocker, "x", error));
	error.clear();
	CHECK_FALSE(writer.begin(blocker + "/nested/file.txt", error));
	CHECK_FALSE(error.empty());

	// writing or committing without an open transfer is an honest refusal
	error.clear();
	CHECK_FALSE(writer.write("x", 1, error));
	CHECK_FALSE(error.empty());
	error.clear();
	CHECK_FALSE(writer.commit(error));
	CHECK_FALSE(error.empty());
}

TEST_CASE("FileWriter writes empty and binary payloads", "[filesystem]")
{
	Scratch scratch;
	String error;

	const String empty = scratch.path("empty.bin");
	REQUIRE(FileWriter::writeWholeFile(empty, String(), error));
	CHECK(present(empty));
	CHECK(contents(empty).empty());

	// NUL bytes are bytes, not terminators
	String binary;
	binary.push_back('a');
	binary.push_back('\0');
	binary.push_back('b');
	const String target = scratch.path("binary.bin");
	REQUIRE(FileWriter::writeWholeFile(target, binary, error));
	CHECK(contents(target) == binary);
	CHECK(contents(target).size() == 3);
}
