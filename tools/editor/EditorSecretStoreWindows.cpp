/********************************************************************
	created:	Monday 2026/08/03 at 12:00
	filename: 	EditorSecretStoreWindows.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
// EditorSecretStoreWindows.cpp - the Windows credential store: one generic
// credential per project and per slot, held by the Credential Manager the
// system already runs.
//
// The sibling of EditorSecretStoreApple.mm behind the same portable seam. A
// generic credential is stored per USER and protected by their logon, which is
// the same boundary the Keychain draws: it keeps a secret out of files and
// backups, not away from a process this person has already authorised.
#include "EditorSecretStore.h"

#include <windows.h>

#include <wincred.h>

#include <cstddef>
#include <string>
#include <vector>

namespace OrkigeEditor
{
	namespace
	{
		std::wstring toWide(Orkige::String const & text)
		{
			if(text.empty())
			{
				return std::wstring();
			}
			const int length = ::MultiByteToWideChar(CP_UTF8, 0, text.data(),
				static_cast<int>(text.size()), 0, 0);
			if(length <= 0)
			{
				return std::wstring();
			}
			std::wstring wide(static_cast<std::size_t>(length), L'\0');
			::MultiByteToWideChar(CP_UTF8, 0, text.data(),
				static_cast<int>(text.size()), &wide[0], length);
			return wide;
		}

		//! the system's own words for a failure, so a person can look it up
		Orkige::String describe(DWORD error)
		{
			LPWSTR buffer = 0;
			const DWORD length = ::FormatMessageW(
				FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
				FORMAT_MESSAGE_IGNORE_INSERTS, 0, error,
				MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
				reinterpret_cast<LPWSTR>(&buffer), 0, 0);
			Orkige::String message;
			if(length > 0 && buffer != 0)
			{
				const int bytes = ::WideCharToMultiByte(CP_UTF8, 0, buffer,
					static_cast<int>(length), 0, 0, 0, 0);
				if(bytes > 0)
				{
					message.resize(static_cast<std::size_t>(bytes), '\0');
					::WideCharToMultiByte(CP_UTF8, 0, buffer,
						static_cast<int>(length), &message[0], bytes, 0, 0);
				}
			}
			if(buffer != 0)
			{
				::LocalFree(buffer);
			}
			while(!message.empty() && (message.back() == '\n' ||
				message.back() == '\r' || message.back() == ' '))
			{
				message.pop_back();
			}
			if(message.empty())
			{
				message = "credential store error " + std::to_string(error);
			}
			return message;
		}

		//! the target name one secret is filed under: the service and the
		//! per-project account, so the whole set is findable (and revocable)
		//! in the system's own credential UI
		std::wstring targetName(Orkige::String const & account)
		{
			return toWide(secretVaultService() + ":" + account);
		}

		SecretResult failure(DWORD error)
		{
			SecretResult result;
			result.status = SecretStatus::Failed;
			result.message = describe(error);
			return result;
		}

		//! the Windows Credential Manager
		class CredentialManagerVault : public SecretVault
		{
			//--- Methods ---------------------------------------
		public:
			Orkige::String name() const override
			{
				return "Credential Manager";
			}

			SecretResult read(Orkige::String const & account) const override
			{
				SecretResult result;
				std::wstring target = targetName(account);
				PCREDENTIALW credential = 0;
				if(!::CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0,
					&credential))
				{
					const DWORD error = ::GetLastError();
					if(error == ERROR_NOT_FOUND)
					{
						result.status = SecretStatus::Missing;
						return result;
					}
					return failure(error);
				}
				if(credential->CredentialBlobSize > 0 &&
					credential->CredentialBlob != 0)
				{
					result.value.assign(
						reinterpret_cast<const char *>(
							credential->CredentialBlob),
						static_cast<std::size_t>(
							credential->CredentialBlobSize));
					// the system's copy goes back to zero before it is freed
					::SecureZeroMemory(credential->CredentialBlob,
						credential->CredentialBlobSize);
				}
				::CredFree(credential);
				result.status = result.value.empty() ? SecretStatus::Missing
					: SecretStatus::Ok;
				return result;
			}

			SecretResult write(Orkige::String const & account,
				Orkige::String const & secret) override
			{
				SecretResult result;
				std::wstring target = targetName(account);
				std::wstring user = toWide(account);
				std::vector<char> blob(secret.begin(), secret.end());
				CREDENTIALW credential = {};
				credential.Type = CRED_TYPE_GENERIC;
				credential.TargetName = &target[0];
				credential.UserName = user.empty() ? 0 : &user[0];
				credential.CredentialBlobSize =
					static_cast<DWORD>(blob.size());
				credential.CredentialBlob = blob.empty() ? 0
					: reinterpret_cast<LPBYTE>(blob.data());
				// this machine, this user - never the roaming store, which
				// would carry a signing password onto other machines
				credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
				const BOOL written = ::CredWriteW(&credential, 0);
				const DWORD error = written ? 0 : ::GetLastError();
				if(!blob.empty())
				{
					::SecureZeroMemory(blob.data(), blob.size());
				}
				if(!written)
				{
					return failure(error);
				}
				result.status = SecretStatus::Ok;
				return result;
			}

			SecretResult erase(Orkige::String const & account) override
			{
				SecretResult result;
				std::wstring target = targetName(account);
				if(!::CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0))
				{
					const DWORD error = ::GetLastError();
					if(error != ERROR_NOT_FOUND)
					{
						return failure(error);
					}
					// removing what is not there is what the caller asked for
				}
				result.status = SecretStatus::Ok;
				return result;
			}
		};
	}
	//---------------------------------------------------------
	SecretVault * platformSecretVault()
	{
		static CredentialManagerVault vault;
		return &vault;
	}
}
