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

#ifdef _WIN32
#	include <aclapi.h>
#	include <windows.h>
#endif

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

#if defined(_WIN32)
	//! is @p sid one of the well-known groups whose presence in a file's access
	//! control list would make the file readable by other accounts
	bool isOpenGroup(PSID sid)
	{
		SID_IDENTIFIER_AUTHORITY world = SECURITY_WORLD_SID_AUTHORITY;
		SID_IDENTIFIER_AUTHORITY nt = SECURITY_NT_AUTHORITY;
		PSID everyone = NULL;			// S-1-1-0
		PSID users = NULL;				// S-1-5-32-545 BUILTIN\Users
		PSID authenticated = NULL;		// S-1-5-11 Authenticated Users
		::AllocateAndInitializeSid(&world, 1, SECURITY_WORLD_RID,
			0, 0, 0, 0, 0, 0, 0, &everyone);
		::AllocateAndInitializeSid(&nt, 2, SECURITY_BUILTIN_DOMAIN_RID,
			DOMAIN_ALIAS_RID_USERS, 0, 0, 0, 0, 0, 0, &users);
		::AllocateAndInitializeSid(&nt, 1, SECURITY_AUTHENTICATED_USER_RID,
			0, 0, 0, 0, 0, 0, 0, &authenticated);
		const bool open =
			(everyone != NULL && ::EqualSid(sid, everyone) != FALSE) ||
			(users != NULL && ::EqualSid(sid, users) != FALSE) ||
			(authenticated != NULL &&
				::EqualSid(sid, authenticated) != FALSE);
		if (everyone != NULL) { ::FreeSid(everyone); }
		if (users != NULL) { ::FreeSid(users); }
		if (authenticated != NULL) { ::FreeSid(authenticated); }
		return open;
	}
#endif

	//! @brief assert that @p path carries this platform's owner-only
	//! restriction. POSIX states it as mode bits and the exact value is
	//! assertable; Windows states it as an access control list, where the
	//! assertions are deliberately PROPERTIES (a list exists, it is protected
	//! from inheritance, and no well-known open group appears in it) rather
	//! than one exact security descriptor - the exact text differs between
	//! accounts, domains and machine images, and pinning it would fail on the
	//! host instead of on the code.
	void checkOwnerOnly(String const & path)
	{
#if defined(_WIN32)
		std::wstring wide = std::filesystem::path(path).wstring();
		wide.push_back(L'\0');
		PACL list = NULL;
		PSECURITY_DESCRIPTOR descriptor = NULL;
		REQUIRE(::GetNamedSecurityInfoW(&wide[0], SE_FILE_OBJECT,
			DACL_SECURITY_INFORMATION, NULL, NULL, &list, NULL,
			&descriptor) == ERROR_SUCCESS);
		// a NULL list is not "no access", it is "everyone, everything"
		CHECK(list != NULL);
		SECURITY_DESCRIPTOR_CONTROL control = 0;
		DWORD revision = 0;
		if (::GetSecurityDescriptorControl(descriptor, &control, &revision))
		{
			CHECK((control & SE_DACL_PROTECTED) != 0);
		}
		else
		{
			FAIL("the security descriptor's control flags were unreadable");
		}
		bool grantsOpenGroup = false;
		if (list != NULL)
		{
			for (DWORD i = 0; i < list->AceCount; ++i)
			{
				void * entry = NULL;
				if (!::GetAce(list, i, &entry))
				{
					continue;
				}
				if (static_cast<ACE_HEADER *>(entry)->AceType !=
					ACCESS_ALLOWED_ACE_TYPE)
				{
					continue;
				}
				ACCESS_ALLOWED_ACE * allowed =
					static_cast<ACCESS_ALLOWED_ACE *>(entry);
				if (isOpenGroup(reinterpret_cast<PSID>(&allowed->SidStart)))
				{
					grantsOpenGroup = true;
				}
			}
		}
		CHECK_FALSE(grantsOpenGroup);
		if (descriptor != NULL)
		{
			::LocalFree(descriptor);
		}
#else
		const std::filesystem::perms permissions =
			std::filesystem::status(std::filesystem::path(path)).permissions();
		CHECK(permissions == (std::filesystem::perms::owner_read |
			std::filesystem::perms::owner_write));
#endif
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

TEST_CASE("an owner-only file is restricted and still readable by its owner",
	"[filesystem]")
{
	Scratch scratch;
	String error;

	const String target = scratch.path("secret.token");
	REQUIRE(FileWriter::writeOwnerOnlyFile(target, "51234\ndeadbeef\n", error));
	CHECK(error.empty());
	CHECK(present(target));
	// the owner can still READ it: an over-tight restriction that locks this
	// process out of its own file is a different bug of the same shape, and the
	// content check is what catches it
	CHECK(contents(target) == "51234\ndeadbeef\n");
	checkOwnerOnly(target);
}

TEST_CASE("an owner-only file is restricted before its bytes exist",
	"[filesystem]")
{
	Scratch scratch;
	String error;

	// THE SEQUENCE is the guarantee. While the transfer is open the bytes live
	// in the temp file - and that file is ALREADY restricted, so there is no
	// instant at which the secret sits in something another account can open.
	const String target = scratch.path("staged.token");
	FileWriter writer;
	REQUIRE(writer.beginOwnerOnly(target, error));
	REQUIRE(writer.write("secret", 6, error));
	const String temporary = target + ".orkpart";
	REQUIRE(present(temporary));
	checkOwnerOnly(temporary);
	// ...and the restriction rides the rename onto the target
	REQUIRE(writer.commit(error));
	CHECK_FALSE(present(temporary));
	checkOwnerOnly(target);
	CHECK(contents(target) == "secret");
}

TEST_CASE("an owner-only write adopts neither a stale temp nor a permissive "
	"target", "[filesystem]")
{
	Scratch scratch;
	String error;

	// a crashed run (or a hostile local process) left a world-readable file at
	// the target AND a leftover temp beside it; neither may be written into
	const String target = scratch.path("stale.token");
	REQUIRE(FileWriter::writeWholeFile(target, "old", error));
	REQUIRE(FileWriter::writeWholeFile(target + ".orkpart", "junk", error));
#if !defined(_WIN32)
	std::error_code widened;
	std::filesystem::permissions(std::filesystem::path(target),
		std::filesystem::perms::all, std::filesystem::perm_options::replace,
		widened);
	std::filesystem::permissions(std::filesystem::path(target + ".orkpart"),
		std::filesystem::perms::all, std::filesystem::perm_options::replace,
		widened);
#endif
	REQUIRE(FileWriter::writeOwnerOnlyFile(target, "fresh", error));
	CHECK(contents(target) == "fresh");
	checkOwnerOnly(target);
}

TEST_CASE("an ordinary whole-file write is not silently restricted",
	"[filesystem]")
{
	Scratch scratch;
	String error;

	// the owner-only road is opt-in: an ordinary write keeps the process umask,
	// so scenes, saves and downloads do not quietly become unreadable to the
	// tools a person points at them
	const String target = scratch.path("plain.txt");
	REQUIRE(FileWriter::writeWholeFile(target, "plain", error));
	CHECK(contents(target) == "plain");
#if !defined(_WIN32)
	// measured against a file this process creates the ordinary way, so the
	// claim holds whatever umask the host runs under
	const String reference = scratch.path("reference.txt");
	{
		std::ofstream plain(reference, std::ios::binary);
		plain << "plain";
	}
	CHECK(std::filesystem::status(std::filesystem::path(target)).permissions()
		== std::filesystem::status(std::filesystem::path(reference))
			.permissions());
#endif
}
