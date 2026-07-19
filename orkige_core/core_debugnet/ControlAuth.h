/**************************************************************
	created:	2026/07/20 at 12:00
	filename: 	ControlAuth.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __ControlAuth_h__7_20_2026__12_00_00__
#define __ControlAuth_h__7_20_2026__12_00_00__

#include "core_util/String.h"

namespace Orkige
{
	//! @brief the auth policy for the editor's network control surface (the MCP
	//! endpoint's verb handler), kept as a pure decision so a unit test pins it
	//! and the handler cannot drift from what the docs promise.
	//! @remarks The threat model: the control surface grants a remote client
	//! full editor control (scene authoring, project-file writes, script
	//! execution, play). It binds loopback only, but on a shared/multi-user
	//! host any local process could still reach it - so when a token is
	//! configured EVERY verb needs the bearer, reads included (an
	//! unauthenticated reader must not exfiltrate the project tree or source).
	//! With no token configured the port is a hand-started dev convenience and
	//! stays fully open.
	namespace ControlAuth
	{
		//! @brief verbs reachable BEFORE authentication so a client can still
		//! establish/probe the link: the handshake ("hello", which itself
		//! carries and checks the token) and the liveness "ping" (reveals
		//! nothing). Everything else is gated once a token is configured.
		inline bool isPreAuthVerb(String const & verb)
		{
			return verb == "hello" || verb == "ping";
		}

		//! @brief may this verb run given the current auth state? No token =>
		//! open (dev mode). With a token, pre-auth verbs pass; every other
		//! verb (read OR mutation) needs a valid bearer.
		inline bool verbAllowed(bool tokenConfigured, bool authenticated,
			String const & verb)
		{
			if (!tokenConfigured)
			{
				return true;	// dev convenience: an untokened port is open
			}
			if (isPreAuthVerb(verb))
			{
				return true;	// handshake / liveness stay reachable
			}
			return authenticated;	// reads included: the bearer is required
		}
	}
}

#endif //__ControlAuth_h__7_20_2026__12_00_00__
