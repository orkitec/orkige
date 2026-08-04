/********************************************************************
	created:	Saturday 2026/08/01 at 10:00
	filename: 	ExportZipTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
//! @file ExportZipTests.cpp
//! @brief the zip writer, read back by the reader that has to accept it.
//!
//! The proof that matters is a ROUND TRIP through `MiniZip` - the engine's own
//! reader, compiled straight into this suite (it is renderer-free, the same
//! way the fuzz harness consumes it). A writer asserted only against its own
//! parse would be free to agree with itself about a wrong format; asserted
//! against the consumer, it cannot.

#include "ExportFiles.h"
#include "ExportZip.h"

#include <engine_filesystem/MiniZip.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

using OrkigeExport::ExportFiles;
using OrkigeExport::ExportZip;

namespace
{
	std::vector<unsigned char> bytesOf(std::string const & text)
	{
		return std::vector<unsigned char>(text.begin(), text.end());
	}
	//---------------------------------------------------------
	//! @brief a scratch directory that removes itself.
	//! @remarks named by its caller, never randomly: every test case runs in
	//! its own process and those processes run in parallel, so two of them
	//! seeded alike would draw the same "random" name and delete each other's
	//! tree. A name unique per test case cannot collide.
	class ScratchDirectory
	{
	public:
		explicit ScratchDirectory(std::string const & name)
		{
			this->mPath = (std::filesystem::temp_directory_path() /
				("orkige_zip_test_" + name)).string();
			ExportFiles::removeTree(this->mPath, 0);
			ExportFiles::makeDirectories(this->mPath, 0);
		}
		~ScratchDirectory() { ExportFiles::removeTree(this->mPath, 0); }
		std::string file(std::string const & name) const
		{
			return ExportFiles::join(this->mPath, name);
		}
	private:
		std::string mPath;
	};
	//---------------------------------------------------------
	//! @brief the external attributes @p name's central directory record
	//! carries, read straight out of the finished bytes (0 when absent)
	unsigned int centralExternalAttributes(
		std::vector<unsigned char> const & archive, std::string const & name)
	{
		const std::string text(archive.begin(), archive.end());
		std::size_t at = 0;
		while((at = text.find("\x50\x4b\x01\x02", at)) != std::string::npos)
		{
			const std::size_t nameLength = static_cast<unsigned char>(
				text[at + 28]) | (static_cast<unsigned char>(
				text[at + 29]) << 8);
			if(text.compare(at + 46, nameLength, name) == 0)
			{
				unsigned int value = 0;
				for(int index = 3; index >= 0; --index)
				{
					value = (value << 8) | static_cast<unsigned char>(
						text[at + 38 + index]);
				}
				return value;
			}
			at += 4;
		}
		return 0u;
	}
}

TEST_CASE("ExportZip round-trips STORED and DEFLATE through MiniZip",
	"[exporter][zip]")
{
	ScratchDirectory scratch("roundtrip");
	// a long, highly repetitive body so DEFLATE genuinely shrinks it (a body
	// compression cannot help is stored instead - a separate case below)
	std::string compressible;
	for(int index = 0; index < 500; ++index)
	{
		compressible += "the same line over and over again\n";
	}
	const std::string stored = "mounted in place, never inflated";

	ExportZip zip;
	Orkige::String error;
	REQUIRE(zip.addBytes("plain.txt", bytesOf(stored), ExportZip::METHOD_STORE,
		0, &error));
	REQUIRE(zip.addBytes("nested/deflated.txt", bytesOf(compressible),
		ExportZip::METHOD_DEFLATE, 0, &error));
	REQUIRE(zip.entryCount() == 2);
	const Orkige::String archive = scratch.file("round-trip.zip");
	INFO("zip write: " << error);
	REQUIRE(zip.write(archive, &error));

	Orkige::MiniZip reader;
	REQUIRE(reader.open(archive));
	REQUIRE(reader.entries().size() == 2);
	REQUIRE(reader.entries().at("plain.txt").method == 0);
	REQUIRE(reader.entries().at("nested/deflated.txt").method == 8);
	// the deflated entry really is smaller on disk than in memory
	REQUIRE(reader.entries().at("nested/deflated.txt").compressedSize <
		reader.entries().at("nested/deflated.txt").uncompressedSize);

	std::vector<unsigned char> read;
	REQUIRE(reader.read("plain.txt", read));
	REQUIRE(std::string(read.begin(), read.end()) == stored);
	REQUIRE(reader.read("nested/deflated.txt", read));
	REQUIRE(std::string(read.begin(), read.end()) == compressible);
}

TEST_CASE("ExportZip stores an entry compression would grow",
	"[exporter][zip]")
{
	ScratchDirectory scratch("incompressible");
	// eight incompressible bytes: deflate's own framing costs more than it
	// saves, and growing an entry helps nobody
	const std::vector<unsigned char> noise = { 0x9f, 0x12, 0xe4, 0x53, 0x08,
		0xba, 0x77, 0xc1 };
	ExportZip zip;
	Orkige::String error;
	REQUIRE(zip.addBytes("noise.bin", noise, ExportZip::METHOD_DEFLATE, 0,
		&error));
	const Orkige::String archive = scratch.file("noise.zip");
	REQUIRE(zip.write(archive, &error));

	Orkige::MiniZip reader;
	REQUIRE(reader.open(archive));
	REQUIRE(reader.entries().at("noise.bin").method == 0);
	std::vector<unsigned char> read;
	REQUIRE(reader.read("noise.bin", read));
	REQUIRE(read == noise);
}

TEST_CASE("ExportZip carries an entry's executable bit", "[exporter][zip]")
{
	ScratchDirectory scratch("execbit");
	const Orkige::String binary = scratch.file("PlayerBinary");
	Orkige::String error;
	REQUIRE(ExportFiles::writeTextFile(binary, "#!/bin/sh\n", &error));
	REQUIRE(ExportFiles::makeExecutable(binary, &error));
	const Orkige::String plain = scratch.file("readme.txt");
	REQUIRE(ExportFiles::writeTextFile(plain, "hello\n", &error));

	ExportZip zip;
	REQUIRE(zip.addFile("Payload/Game.app/Game", binary,
		ExportZip::METHOD_STORE, &error));
	REQUIRE(zip.addFile("Payload/Game.app/readme.txt", plain,
		ExportZip::METHOD_STORE, &error));
	std::vector<unsigned char> archive;
	REQUIRE(zip.finish(archive, &error));

	// the executable bit lives in the HIGH 16 bits of the central directory's
	// external attributes - an unpacked bundle whose binary lost it will not
	// launch, so this is the assertion that keeps an .ipa installable
	const unsigned int binaryMode =
		centralExternalAttributes(archive, "Payload/Game.app/Game") >> 16;
	const unsigned int plainMode =
		centralExternalAttributes(archive, "Payload/Game.app/readme.txt") >> 16;
	// a host that keeps no modes has none to carry: there both entries record
	// the writer's deterministic default rather than an invented bit (Windows
	// models only a read-only flag and reports every file as executable). The
	// bundles whose executable bit decides whether the artifact runs are
	// packed on macOS, which is where the executable answer is asserted.
	REQUIRE(binaryMode ==
		(OrkigeExport::hostCarriesFileModes() ? 0755u : 0644u));
	REQUIRE(plainMode == 0644u);
}

TEST_CASE("ExportZip records the mode it is given", "[exporter][zip]")
{
	// the mode MECHANISM - a requested mode reaching the central directory's
	// external attributes - is host-independent, and asserted on every host:
	// only reading a mode off a staged file needs a host that keeps modes
	ExportZip zip;
	Orkige::String error;
	REQUIRE(zip.addBytes("Payload/Game.app/Game", bytesOf("binary"),
		ExportZip::METHOD_STORE, 0755u, &error));
	REQUIRE(zip.addBytes("Payload/Game.app/readme.txt", bytesOf("hello\n"),
		ExportZip::METHOD_STORE, 0, &error));
	std::vector<unsigned char> archive;
	REQUIRE(zip.finish(archive, &error));

	CHECK((centralExternalAttributes(archive, "Payload/Game.app/Game") >> 16)
		== 0755u);
	// 0 asks for the default, which is 0644 and not "no mode at all"
	CHECK((centralExternalAttributes(archive,
		"Payload/Game.app/readme.txt") >> 16) == 0644u);
}

TEST_CASE("ExportZip refuses a duplicate entry name", "[exporter][zip]")
{
	ExportZip zip;
	Orkige::String error;
	REQUIRE(zip.addBytes("a.txt", bytesOf("one"), ExportZip::METHOD_STORE, 0,
		&error));
	REQUIRE_FALSE(zip.addBytes("a.txt", bytesOf("two"), ExportZip::METHOD_STORE,
		0, &error));
	REQUIRE(error.find("twice") != Orkige::String::npos);
	REQUIRE(zip.entryCount() == 1);
}

TEST_CASE("ExportZip writes the same bytes twice for the same input",
	"[exporter][zip]")
{
	// one fixed timestamp per entry: packaging the same tree twice yields the
	// same archive, so a rebuild is comparable
	Orkige::String error;
	std::vector<unsigned char> first;
	std::vector<unsigned char> second;
	for(int pass = 0; pass < 2; ++pass)
	{
		ExportZip zip;
		REQUIRE(zip.addBytes("a.txt", bytesOf("one"), ExportZip::METHOD_STORE,
			0, &error));
		REQUIRE(zip.addBytes("b.txt", bytesOf("two"), ExportZip::METHOD_DEFLATE,
			0, &error));
		REQUIRE(zip.finish(pass == 0 ? first : second, &error));
	}
	REQUIRE(first == second);
}

TEST_CASE("ExportZip writes an empty archive a reader still opens",
	"[exporter][zip]")
{
	ScratchDirectory scratch("empty");
	ExportZip zip;
	Orkige::String error;
	const Orkige::String archive = scratch.file("empty.zip");
	REQUIRE(zip.write(archive, &error));
	Orkige::MiniZip reader;
	REQUIRE(reader.open(archive));
	REQUIRE(reader.entries().empty());
}

//--- the reader ---------------------------------------------------

TEST_CASE("ExportZipReader reads back what ExportZip wrote",
	"[exporter][zip]")
{
	// the round trip that matters for a CONSUMED archive: a library archive
	// arrives from somewhere else, so both compression methods and an entry of
	// every awkward size have to come back exactly.
	ScratchDirectory scratch("reader_roundtrip");
	const Orkige::String archive = scratch.file("library.zip");
	Orkige::String error;
	// a payload big enough that deflate actually beats storing it
	std::string repetitive;
	for(int index = 0; index < 400; ++index)
	{
		repetitive += "the same line over and over\n";
	}
	{
		ExportZip zip;
		REQUIRE(zip.addBytes("AndroidManifest.xml", bytesOf("<manifest/>"),
			ExportZip::METHOD_STORE, 0, &error));
		REQUIRE(zip.addBytes("classes.jar", bytesOf(repetitive),
			ExportZip::METHOD_DEFLATE, 0, &error));
		REQUIRE(zip.addBytes("res/values/values.xml", bytesOf(""),
			ExportZip::METHOD_DEFLATE, 0, &error));
		REQUIRE(zip.write(archive, &error));
	}

	OrkigeExport::ExportZipReader reader;
	INFO(error);
	REQUIRE(reader.open(archive, &error));
	REQUIRE(reader.entries().size() == 3);
	CHECK(reader.entries()[0].name == "AndroidManifest.xml");
	CHECK(reader.has("classes.jar"));
	CHECK_FALSE(reader.has("libs/other.jar"));

	std::vector<unsigned char> bytes;
	REQUIRE(reader.read("AndroidManifest.xml", bytes, &error));
	CHECK(bytes == bytesOf("<manifest/>"));
	REQUIRE(reader.read("classes.jar", bytes, &error));
	CHECK(bytes == bytesOf(repetitive));
	// an EMPTY entry is a real case (a marker file, a stub resource) and the
	// one an inflate loop gets wrong
	REQUIRE(reader.read("res/values/values.xml", bytes, &error));
	CHECK(bytes.empty());

	// ...and an entry that is not there is an honest refusal, not an empty read
	CHECK_FALSE(reader.read("libs/other.jar", bytes, &error));
	CHECK(error.find("libs/other.jar") != Orkige::String::npos);
}

TEST_CASE("ExportZipReader refuses what is not an archive",
	"[exporter][zip]")
{
	ScratchDirectory scratch("reader_refusals");
	Orkige::String error;
	const Orkige::String garbage = scratch.file("garbage.zip");
	REQUIRE(ExportFiles::writeTextFile(garbage,
		"this is not a zip archive at all", &error));
	OrkigeExport::ExportZipReader reader;
	CHECK_FALSE(reader.open(garbage, &error));
	CHECK(error.find("not a zip") != Orkige::String::npos);

	CHECK_FALSE(reader.open(scratch.file("absent.zip"), &error));
}

TEST_CASE("an archive entry never unpacks outside where it was told to",
	"[exporter][zip]")
{
	// a consumed archive comes from somewhere else, so its entry names are
	// input rather than fact. Each of these writes outside the destination on
	// SOME host, which is exactly one host too many.
	using OrkigeExport::isSafeArchiveEntryName;
	CHECK(isSafeArchiveEntryName("res/values/values.xml"));
	CHECK(isSafeArchiveEntryName("classes.jar"));
	CHECK(isSafeArchiveEntryName("a..b/c.txt"));

	CHECK_FALSE(isSafeArchiveEntryName("/etc/passwd"));
	CHECK_FALSE(isSafeArchiveEntryName("../outside.txt"));
	CHECK_FALSE(isSafeArchiveEntryName("res/../../outside.txt"));
	// a backslash is a legal zip name character and a SEPARATOR on Windows
	CHECK_FALSE(isSafeArchiveEntryName("res\\..\\outside.txt"));
	CHECK_FALSE(isSafeArchiveEntryName("C:/windows/system32/a.dll"));
	CHECK_FALSE(isSafeArchiveEntryName(""));
}
