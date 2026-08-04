/********************************************************************
	created:	2026/07/30 at 10:00
	filename: 	FileWriter.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

#include "core_filesystem/FileWriter.h"

#include "core_debug/DebugMacros.h"

#include <cstdio>
#include <fcntl.h>
#include <filesystem>
#include <string>
#include <sys/stat.h>
#include <system_error>

#ifdef _WIN32
#	include <aclapi.h>
#	include <io.h>
#	include <share.h>
#	include <vector>
#	include <windows.h>
#else
#	include <unistd.h>
#endif

namespace Orkige
{
	namespace
	{
		//! the temp-file suffix every unfinished transfer carries
		char const * const TEMP_SUFFIX = ".orkpart";

#ifdef _WIN32
		//! @brief UTF-8 -> UTF-16, so a path with non-ASCII characters reaches
		//! the wide Windows API intact (the narrow entry points would read it
		//! in the machine's code page)
		std::wstring widenPath(String const & path)
		{
			if (path.empty())
			{
				return std::wstring();
			}
			const int length = ::MultiByteToWideChar(CP_UTF8, 0, path.c_str(),
				static_cast<int>(path.size()), NULL, 0);
			if (length <= 0)
			{
				return std::wstring();
			}
			std::wstring wide(static_cast<size_t>(length), L'\0');
			::MultiByteToWideChar(CP_UTF8, 0, path.c_str(),
				static_cast<int>(path.size()), &wide[0], length);
			return wide;
		}

		//! @brief replace @p path's access control list with a PROTECTED one
		//! (inheritance from the containing directory switched OFF) granting
		//! full control to the current process token's owner and to SYSTEM, and
		//! to nobody else.
		//!
		//! WHY the platform's own security API and not a permissions() call:
		//! Windows access control is an ACL on the object, which the standard
		//! filesystem library does not model - permissions() there sets the
		//! read-only attribute and nothing more. And WHY protected: a file
		//! inherits its directory's ACL, so a project root on a secondary drive
		//! or in a shared folder hands the file whatever that tree grants; a
		//! protected DACL is the only one that does not depend on where the
		//! file happens to live.
		bool applyOwnerOnlyAcl(String const & path, String & reason)
		{
			std::wstring wide = widenPath(path);
			wide.push_back(L'\0');	// SetNamedSecurityInfoW takes a mutable LPWSTR
			HANDLE token = NULL;
			if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token))
			{
				reason = "the process token could not be opened";
				return false;
			}
			DWORD needed = 0;
			::GetTokenInformation(token, TokenUser, NULL, 0, &needed);
			std::vector<unsigned char> buffer(needed > 0 ? needed : 1);
			if (needed == 0 || !::GetTokenInformation(token, TokenUser,
				buffer.data(), needed, &needed))
			{
				::CloseHandle(token);
				reason = "the process token's owner could not be read";
				return false;
			}
			::CloseHandle(token);
			PSID owner = reinterpret_cast<TOKEN_USER *>(buffer.data())->User.Sid;
			SID_IDENTIFIER_AUTHORITY authority = SECURITY_NT_AUTHORITY;
			PSID systemSid = NULL;
			if (!::AllocateAndInitializeSid(&authority, 1,
				SECURITY_LOCAL_SYSTEM_RID, 0, 0, 0, 0, 0, 0, 0, &systemSid))
			{
				reason = "the SYSTEM account could not be resolved";
				return false;
			}
			// SYSTEM rides along because a machine's own maintenance (backup,
			// indexing, the installer) runs as it; leaving it out buys nothing
			// against a user-level attacker and breaks those instead.
			EXPLICIT_ACCESS_W entries[2];
			::ZeroMemory(entries, sizeof(entries));
			for (int i = 0; i < 2; ++i)
			{
				entries[i].grfAccessPermissions = GENERIC_ALL;
				entries[i].grfAccessMode = SET_ACCESS;
				entries[i].grfInheritance = NO_INHERITANCE;
				entries[i].Trustee.TrusteeForm = TRUSTEE_IS_SID;
				entries[i].Trustee.TrusteeType = TRUSTEE_IS_UNKNOWN;
			}
			// ptstrName, not ptstr_name: with TRUSTEE_IS_SID the field carries a
			// PSID rather than a name, which is why the cast looks wrong and is
			// not - that is the documented Win32 shape.
			entries[0].Trustee.ptstrName = reinterpret_cast<LPWSTR>(owner);
			entries[1].Trustee.ptstrName = reinterpret_cast<LPWSTR>(systemSid);
			PACL list = NULL;
			bool applied = false;
			if (::SetEntriesInAclW(2, entries, NULL, &list) == ERROR_SUCCESS)
			{
				// PROTECTED_DACL_SECURITY_INFORMATION is the half that drops
				// the inherited entries - without it the two explicit ACEs are
				// simply ADDED to whatever the directory already granted
				const DWORD result = ::SetNamedSecurityInfoW(&wide[0],
					SE_FILE_OBJECT, DACL_SECURITY_INFORMATION |
						PROTECTED_DACL_SECURITY_INFORMATION,
					NULL, NULL, list, NULL);
				applied = (result == ERROR_SUCCESS);
				if (!applied)
				{
					reason = "the filesystem refused the access control list";
				}
			}
			else
			{
				reason = "the access control list could not be built";
			}
			if (list != NULL)
			{
				::LocalFree(list);
			}
			::FreeSid(systemSid);
			return applied;
		}
#endif

		//! @brief create @p path as an EMPTY file only its owner may read and
		//! return the stdio handle to append through (NULL when the file could
		//! not be created at all).
		//! @param restricted set false when the file exists but the platform
		//! could not express the restriction, with @p reason saying why
		FILE * createOwnerOnlyFile(String const & path, bool & restricted,
			String & reason)
		{
			restricted = true;
			// a leftover temp from a crashed transfer must not be adopted: an
			// exclusive create is what makes the mode/ACL below OURS
			std::error_code removeError;
			std::filesystem::remove(path, removeError);
#ifdef _WIN32
			int handle = -1;
			const errno_t opened = ::_wsopen_s(&handle, widenPath(path).c_str(),
				_O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY, _SH_DENYNO,
				_S_IREAD | _S_IWRITE);
			if (opened != 0 || handle < 0)
			{
				return NULL;
			}
			if (!applyOwnerOnlyAcl(path, reason))
			{
				restricted = false;
			}
			FILE * file = ::_fdopen(handle, "wb");
			if (file == NULL)
			{
				::_close(handle);
			}
			return file;
#else
			const int handle = ::open(path.c_str(),
				O_WRONLY | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
			if (handle < 0)
			{
				return NULL;
			}
			// the create mode is masked by the process umask, so it can only be
			// TIGHTER than requested - never wider; this makes it exact, which
			// is what the regression test reads back
			std::error_code permissionError;
			std::filesystem::permissions(std::filesystem::path(path),
				std::filesystem::perms::owner_read |
					std::filesystem::perms::owner_write,
				std::filesystem::perm_options::replace, permissionError);
			if (permissionError)
			{
				restricted = false;
				reason = permissionError.message();
			}
			FILE * file = ::fdopen(handle, "wb");
			if (file == NULL)
			{
				::close(handle);
			}
			return file;
#endif
		}
	}
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	FileWriter::FileWriter()
	{
		this->mHandle = NULL;
		this->mWritten = 0;
	}
	//---------------------------------------------------------
	FileWriter::~FileWriter()
	{
		// an uncommitted transfer never leaves a file behind
		this->abort();
	}
	//---------------------------------------------------------
	bool FileWriter::begin(String const & path, String & error)
	{
		return this->openTransfer(path, false, error);
	}
	//---------------------------------------------------------
	bool FileWriter::beginOwnerOnly(String const & path, String & error)
	{
		return this->openTransfer(path, true, error);
	}
	//---------------------------------------------------------
	bool FileWriter::write(char const * bytes, unsigned long long count,
		String & error)
	{
		if (this->mHandle == NULL)
		{
			error = "no open transfer";
			return false;
		}
		if (count == 0)
		{
			return true;
		}
		if (bytes == NULL)
		{
			error = "no bytes to write";
			this->abort();
			return false;
		}
		FILE * handle = static_cast<FILE *>(this->mHandle);
		const size_t chunk = static_cast<size_t>(count);
		if (std::fwrite(bytes, 1, chunk, handle) != chunk)
		{
			error = "could not write to '" + this->mTempPath +
				"' (out of space?)";
			this->abort();
			return false;
		}
		this->mWritten += count;
		return true;
	}
	//---------------------------------------------------------
	bool FileWriter::commit(String & error)
	{
		if (this->mHandle == NULL)
		{
			error = "no open transfer";
			return false;
		}
		FILE * handle = static_cast<FILE *>(this->mHandle);
		const bool flushed = (std::fflush(handle) == 0);
		std::fclose(handle);
		this->mHandle = NULL;
		if (!flushed)
		{
			error = "could not flush '" + this->mTempPath + "'";
			this->abort();
			return false;
		}
		// THE atomic instant: the target either still holds its old content or
		// holds the complete new one, never a truncated mix
		std::error_code renameError;
		std::filesystem::rename(this->mTempPath, this->mPath, renameError);
		if (renameError)
		{
			error = "could not replace '" + this->mPath + "' (" +
				renameError.message() + ")";
			this->abort();
			return false;
		}
		this->mTempPath.clear();
		return true;
	}
	//---------------------------------------------------------
	void FileWriter::abort()
	{
		if (this->mHandle != NULL)
		{
			std::fclose(static_cast<FILE *>(this->mHandle));
			this->mHandle = NULL;
		}
		if (!this->mTempPath.empty())
		{
			std::error_code ignored;
			std::filesystem::remove(this->mTempPath, ignored);
			this->mTempPath.clear();
		}
		this->mPath.clear();
		this->mWritten = 0;
	}
	//---------------------------------------------------------
	bool FileWriter::writeWholeFile(String const & path, String const & bytes,
		String & error)
	{
		FileWriter writer;
		if (!writer.begin(path, error))
		{
			return false;
		}
		if (!writer.write(bytes.data(), bytes.size(), error))
		{
			return false;
		}
		return writer.commit(error);
	}
	//---------------------------------------------------------
	bool FileWriter::writeOwnerOnlyFile(String const & path,
		String const & bytes, String & error)
	{
		FileWriter writer;
		if (!writer.beginOwnerOnly(path, error))
		{
			return false;
		}
		if (!writer.write(bytes.data(), bytes.size(), error))
		{
			return false;
		}
		return writer.commit(error);
	}
	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	bool FileWriter::openTransfer(String const & path, bool ownerOnly,
		String & error)
	{
		this->abort();
		if (path.empty())
		{
			error = "the target path is empty";
			return false;
		}
		const std::filesystem::path target(path);
		if (target.has_parent_path() && !target.parent_path().empty())
		{
			std::error_code directoryError;
			std::filesystem::create_directories(target.parent_path(),
				directoryError);
			// an EXISTING directory reports no error; a real failure only
			// matters if the directory is still missing afterwards
			std::error_code existsError;
			if (!std::filesystem::is_directory(target.parent_path(), existsError))
			{
				error = "could not create the directory '" +
					target.parent_path().string() + "'";
				return false;
			}
		}
		this->mPath = path;
		this->mTempPath = path + TEMP_SUFFIX;
		if (ownerOnly)
		{
			// restricted BEFORE the first byte, so the secret never exists in a
			// file anyone else can open (@see beginOwnerOnly)
			bool restricted = true;
			String reason;
			this->mHandle = createOwnerOnlyFile(this->mTempPath, restricted,
				reason);
			if (this->mHandle != NULL && !restricted)
			{
				// attempt, warn once, do NOT refuse: a volume with no access
				// control (FAT/exFAT, many network mounts) still gets its file,
				// and the limitation is stated instead of hidden
				oDebugWarn("filesystem", 0, "'" << this->mPath << "' could not "
					"be restricted to its owner on this volume" <<
					(reason.empty() ? String() : String(" (" + reason + ")")) <<
					" - it carries a secret, so treat the volume as readable "
					"by others");
			}
		}
		else
		{
			this->mHandle = std::fopen(this->mTempPath.c_str(), "wb");
		}
		if (this->mHandle == NULL)
		{
			error = "could not open '" + this->mTempPath + "' for writing";
			this->mPath.clear();
			this->mTempPath.clear();
			return false;
		}
		this->mWritten = 0;
		return true;
	}
}
