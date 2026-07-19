/**************************************************************
	created:	2026/07/20 at 12:00
	filename: 	ControlAuthTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include <core_debugnet/ControlAuth.h>

using Orkige::String;
namespace ControlAuth = Orkige::ControlAuth;

TEST_CASE("ControlAuth: no token configured means the port is open", "[auth]")
{
	// dev convenience: an untokened control port answers every verb, reads and
	// mutations alike, authenticated or not
	CHECK(ControlAuth::verbAllowed(false, false, "get_state"));
	CHECK(ControlAuth::verbAllowed(false, false, "list_hierarchy"));
	CHECK(ControlAuth::verbAllowed(false, false, "read_project_file"));
	CHECK(ControlAuth::verbAllowed(false, false, "create_object"));
	CHECK(ControlAuth::verbAllowed(false, false, "write_project_file"));
}

TEST_CASE("ControlAuth: a configured token gates reads too", "[auth]")
{
	// the network-exfil fix: with a token, an UNAUTHENTICATED read is refused
	CHECK_FALSE(ControlAuth::verbAllowed(true, false, "get_state"));
	CHECK_FALSE(ControlAuth::verbAllowed(true, false, "list_hierarchy"));
	CHECK_FALSE(ControlAuth::verbAllowed(true, false, "read_project_file"));
	CHECK_FALSE(ControlAuth::verbAllowed(true, false, "list_project_files"));
	// mutations were always gated - still are
	CHECK_FALSE(ControlAuth::verbAllowed(true, false, "create_object"));
	CHECK_FALSE(ControlAuth::verbAllowed(true, false, "write_project_file"));
	// once authenticated, everything is allowed again
	CHECK(ControlAuth::verbAllowed(true, true, "get_state"));
	CHECK(ControlAuth::verbAllowed(true, true, "read_project_file"));
	CHECK(ControlAuth::verbAllowed(true, true, "create_object"));
}

TEST_CASE("ControlAuth: handshake/liveness verbs stay pre-auth reachable",
	"[auth]")
{
	CHECK(ControlAuth::isPreAuthVerb("hello"));
	CHECK(ControlAuth::isPreAuthVerb("ping"));
	CHECK_FALSE(ControlAuth::isPreAuthVerb("get_state"));
	CHECK_FALSE(ControlAuth::isPreAuthVerb("create_object"));
	// hello/ping pass the gate even with a token and no auth (hello then does
	// its own token check; ping reveals nothing)
	CHECK(ControlAuth::verbAllowed(true, false, "hello"));
	CHECK(ControlAuth::verbAllowed(true, false, "ping"));
}
