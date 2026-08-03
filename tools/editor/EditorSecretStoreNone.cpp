/********************************************************************
	created:	Monday 2026/08/03 at 12:00
	filename: 	EditorSecretStoreNone.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
// EditorSecretStoreNone.cpp - the platforms with no credential store the editor
// uses, which today means Linux.
//
// This is a REFUSAL, not a fallback. There is no file-based substitute here and
// there will not be one: a file holding a signing password is the exposure the
// whole secret store exists to remove, so a platform without a vault keeps
// passwords in the environment - which is where the signing step reads them
// anyway - and the settings surface says so in place, naming the variable.
//
// Why Linux has none:
//
// - The desktop keyring is a session D-Bus service reached through libsecret,
//   which pulls glib into a statically linked editor for every Linux build.
// - The Linux builds that run unattended - the CI containers, the xvfb rigs -
//   have no session bus and no unlocked keyring, so that code would refuse
//   there regardless.
// - The only password the model holds today is an Android release keystore's,
//   and the step that consumes it runs from a shell that can export the
//   variable in one line.
//
// So the dependency buys nothing here yet. When a Linux signing flow exists
// that a keyring would genuinely shorten, this file is the one place a
// libsecret backend lands - the seam above it already has every consumer.
#include "EditorSecretStore.h"

namespace OrkigeEditor
{
	//---------------------------------------------------------
	SecretVault * platformSecretVault()
	{
		return 0;
	}
}
