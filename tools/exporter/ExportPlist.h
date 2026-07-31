/********************************************************************
	created:	Friday 2026/07/31 at 12:00
	filename: 	ExportPlist.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __ExportPlist_h__31_7_2026__12_00_00__
#define __ExportPlist_h__31_7_2026__12_00_00__

#include <core_debugnet/Json.h>
#include <core_util/String.h>

//! @file ExportPlist.h
//! @brief the Apple property lists an export writes, and the in-place edit of
//! the one it inherits.
//!
//! The VALUE MODEL is `Orkige::JsonValue` rather than a second variant type:
//! ordered object members, arrays, strings, numbers and booleans are exactly
//! the plist vocabulary an export uses, and reusing it keeps one tested
//! container instead of two. A number serializes as `<integer>` when it is
//! integral and `<real>` otherwise. `<data>` and `<date>` are deliberately
//! absent - nothing an export authors carries either.
//!
//! Two operations, because they are two different jobs:
//! - WRITE authors a plist wholesale (the macOS Info.plist, the privacy
//!   manifest, the entitlements a codesign call binds in).
//! - EDIT rewrites the identity keys of the plist a prebuilt player bundle
//!   already carries, in its own XML DOM, so every key the template holds -
//!   including any this code has never heard of - survives verbatim.
//!
//! Every plist the shipped players read is XML text (see
//! tools/player/ios/Info.plist.in), so no binary-plist codec is needed.

namespace OrkigeExport
{
	//! @brief the plist file operations (@see ExportPlist.h)
	class ExportPlist
	{
	public:
		//! @brief write @p root (an object) as an XML property list at @p path.
		//! False with an honest @p error on a non-object root or a write
		//! failure.
		static bool write(Orkige::JsonValue const & root,
			Orkige::String const & path, Orkige::String * error);

		//! @brief serialize @p root to XML plist TEXT (the byte-exact content
		//! `write` puts on disk) - the seam the tests assert through.
		static bool serialize(Orkige::JsonValue const & root,
			Orkige::String & out, Orkige::String * error);

		//! @brief read an XML property list into a JsonValue object. False
		//! with an @p error on a missing/unparseable file or a non-dict root.
		//! A `<data>`/`<date>` member reads as its raw inner text string -
		//! enough to report, never enough to round-trip, which is why the
		//! identity rewrite uses `setKeys` instead.
		static bool read(Orkige::String const & path, Orkige::JsonValue & out,
			Orkige::String * error);

		//! @brief set (replace or append) top-level keys of the plist at
		//! @p path IN ITS OWN DOM and write it back. Members of @p keys
		//! replace the value of an existing `<key>`, or are appended to the
		//! root dict; everything else in the file is untouched. False with an
		//! honest @p error on a missing/unparseable file or a write failure.
		static bool setKeys(Orkige::String const & path,
			Orkige::JsonValue const & keys, Orkige::String * error);
	};

	//--- the fixed declarations every export ships ---------------

	//! @brief App Transport Security. iOS blocks plain-http loads unless the
	//! bundle declares otherwise, which would silently defeat the engine HTTP
	//! client's per-request cleartext opt-in - whose real use is a game
	//! pointed at a service on the developer's own machine.
	//! NSAllowsLocalNetworking permits cleartext to loopback, .local names and
	//! LAN literal addresses and NOTHING else. NSAllowsArbitraryLoads stays
	//! out: it opens cleartext to the whole internet and carries a review
	//! justification a game does not have.
	Orkige::JsonValue appTransportSecurity();

	//! @brief the iOS app-bundle privacy manifest.
	//! @remarks Apple requires every submitted app to carry a
	//! PrivacyInfo.xcprivacy at the bundle root declaring collected data,
	//! tracking, and any "required reason" API use. The engine is
	//! self-contained (every dependency statically linked, no third-party SDK
	//! with a manifest of its own), collects no data and contacts no server,
	//! so this is ONE generated artifact. The accessed-API list mirrors what
	//! the shipped player binary actually imports:
	//!  - file timestamp (reason C617.1 - timestamps of files inside the app's
	//!    own container): stat/fstat via the statically linked resource and
	//!    file layers; the player reads only its bundle and its writable app
	//!    dir.
	//!  - system boot time (reason 35F9.1 - elapsed time between in-app
	//!    events): mach_absolute_time via the frame/performance timer.
	//! Nothing else on Apple's required-reason list appears in the binary, so
	//! nothing else is declared - an over- or under-declaring manifest is
	//! worse than none. Engine code adopting one of those APIs must add its
	//! category and an approved reason code here.
	Orkige::JsonValue privacyManifest();

	//! @brief the entitlements for a signed iOS build. Development builds set
	//! get-task-allow (the debugger attaches); a DISTRIBUTION build clears it
	//! (the App Store rejects get-task-allow=true).
	Orkige::JsonValue iosEntitlements(Orkige::String const & teamId,
		Orkige::String const & bundleId, bool forDistribution);
}

#endif //__ExportPlist_h__31_7_2026__12_00_00__
