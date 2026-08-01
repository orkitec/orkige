/********************************************************************
	created:	Saturday 2026/08/01 at 10:00
	filename: 	ExportZip.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#include "ExportZip.h"

#include "ExportFiles.h"

#include <zlib.h>

#include <filesystem>
#include <string>
#include <system_error>

namespace OrkigeExport
{
	namespace
	{
		//! the 32-bit format's ceilings - crossing one needs zip64, which
		//! nothing an export writes comes near
		const unsigned long long ZIP_SIZE_LIMIT = 0xFFFFFFFFull;
		const std::size_t ZIP_ENTRY_LIMIT = 0xFFFF;

		//! the ONE timestamp every entry carries: 1980-01-01 00:00, the
		//! earliest the DOS date encoding represents. A fixed stamp is what
		//! makes packaging the same tree twice byte-identical.
		const unsigned int DOS_TIME = 0;
		const unsigned int DOS_DATE = 0x0021;

		bool report(Orkige::String * error, Orkige::String const & message)
		{
			if(error != 0)
			{
				*error = message;
			}
			return false;
		}
		//---------------------------------------------------------
		void put16(std::vector<unsigned char> & out, unsigned int value)
		{
			out.push_back(static_cast<unsigned char>(value & 0xFF));
			out.push_back(static_cast<unsigned char>((value >> 8) & 0xFF));
		}
		//---------------------------------------------------------
		void put32(std::vector<unsigned char> & out, unsigned long long value)
		{
			out.push_back(static_cast<unsigned char>(value & 0xFF));
			out.push_back(static_cast<unsigned char>((value >> 8) & 0xFF));
			out.push_back(static_cast<unsigned char>((value >> 16) & 0xFF));
			out.push_back(static_cast<unsigned char>((value >> 24) & 0xFF));
		}
		//---------------------------------------------------------
		void putText(std::vector<unsigned char> & out,
			Orkige::String const & text)
		{
			out.insert(out.end(), text.begin(), text.end());
		}
		//---------------------------------------------------------
		//! the mode an added file records: read off the file where the host
		//! keeps modes, the deterministic default everywhere else
		unsigned int stagedFileMode(Orkige::String const & path)
		{
			if(!hostCarriesFileModes())
			{
				return 0644u;
			}
			std::error_code ignored;
			const std::filesystem::perms permissions =
				std::filesystem::status(std::filesystem::path(path),
					ignored).permissions();
			const bool executable = (permissions &
				(std::filesystem::perms::owner_exec |
				 std::filesystem::perms::group_exec |
				 std::filesystem::perms::others_exec)) !=
				std::filesystem::perms::none;
			return executable ? 0755u : 0644u;
		}
	}
	//---------------------------------------------------------
	bool hostCarriesFileModes()
	{
#ifdef _WIN32
		return false;
#else
		return true;
#endif
	}
	//---------------------------------------------------------
	bool deflateRaw(std::vector<unsigned char> const & input,
		std::vector<unsigned char> & out, Orkige::String * error)
	{
		out.clear();
		z_stream stream = {};
		// a NEGATIVE window bit count is zlib's "raw deflate" selector - the
		// headerless stream a zip entry carries
		if(deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS,
			8, Z_DEFAULT_STRATEGY) != Z_OK)
		{
			return report(error, "zlib refused a raw deflate stream");
		}
		stream.next_in = const_cast<Bytef *>(input.empty() ? Z_NULL
			: reinterpret_cast<const Bytef *>(input.data()));
		stream.avail_in = static_cast<uInt>(input.size());
		std::vector<unsigned char> chunk(64 * 1024);
		int status = Z_OK;
		do
		{
			stream.next_out = chunk.data();
			stream.avail_out = static_cast<uInt>(chunk.size());
			status = deflate(&stream, Z_FINISH);
			if(status != Z_OK && status != Z_STREAM_END && status != Z_BUF_ERROR)
			{
				deflateEnd(&stream);
				return report(error, "deflate failed (zlib status " +
					std::to_string(status) + ")");
			}
			out.insert(out.end(), chunk.data(),
				chunk.data() + (chunk.size() - stream.avail_out));
		}
		while(status != Z_STREAM_END);
		deflateEnd(&stream);
		return true;
	}
	//---------------------------------------------------------
	bool ExportZip::addBytes(Orkige::String const & archiveName,
		std::vector<unsigned char> const & bytes, Method method,
		unsigned int posixMode, Orkige::String * error)
	{
		if(archiveName.empty())
		{
			return report(error, "an archive entry needs a name");
		}
		for(Entry const & existing : this->mEntries)
		{
			if(existing.name == archiveName)
			{
				return report(error, "archive entry '" + archiveName +
					"' added twice");
			}
		}
		if(bytes.size() > ZIP_SIZE_LIMIT)
		{
			return report(error, "archive entry '" + archiveName + "' is " +
				std::to_string(bytes.size()) + " bytes - past what the zip "
				"format carries without zip64");
		}
		Entry entry;
		entry.name = archiveName;
		entry.originalSize = bytes.size();
		entry.crc = static_cast<unsigned int>(crc32(0,
			bytes.empty() ? Z_NULL : reinterpret_cast<const Bytef *>(
				bytes.data()), static_cast<uInt>(bytes.size())));
		if(method == METHOD_DEFLATE)
		{
			if(!deflateRaw(bytes, entry.data, error))
			{
				return false;
			}
			if(entry.data.size() < bytes.size())
			{
				entry.method = METHOD_DEFLATE;
			}
			else
			{
				// compression that grows the entry is a loss on both ends -
				// store it verbatim instead (what every zip writer does)
				entry.data = bytes;
				entry.method = METHOD_STORE;
			}
		}
		else
		{
			entry.data = bytes;
			entry.method = METHOD_STORE;
		}
		const unsigned int mode = (posixMode == 0) ? 0644u : posixMode;
		// the high 16 bits of the external attributes are the unix mode, which
		// is how the executable bit survives an unpack
		entry.externalAttributes = (mode & 0xFFFFu) << 16;
		this->mEntries.push_back(entry);
		return true;
	}
	//---------------------------------------------------------
	bool ExportZip::addFile(Orkige::String const & archiveName,
		Orkige::String const & path, Method method, Orkige::String * error)
	{
		std::vector<unsigned char> bytes;
		if(!ExportFiles::readBytes(path, bytes, error))
		{
			return false;
		}
		return this->addBytes(archiveName, bytes, method, stagedFileMode(path),
			error);
	}
	//---------------------------------------------------------
	bool ExportZip::finish(std::vector<unsigned char> & out,
		Orkige::String * error) const
	{
		out.clear();
		if(this->mEntries.size() > ZIP_ENTRY_LIMIT)
		{
			return report(error, "archive holds " +
				std::to_string(this->mEntries.size()) + " entries - past what "
				"the zip format carries without zip64");
		}
		std::vector<unsigned long long> offsets;
		offsets.reserve(this->mEntries.size());
		for(Entry const & entry : this->mEntries)
		{
			offsets.push_back(out.size());
			put32(out, 0x04034b50);				// local file header
			put16(out, 20);						// version needed
			put16(out, 0);						// flags
			put16(out, entry.method);
			put16(out, DOS_TIME);
			put16(out, DOS_DATE);
			put32(out, entry.crc);
			put32(out, entry.data.size());
			put32(out, entry.originalSize);
			put16(out, static_cast<unsigned int>(entry.name.size()));
			put16(out, 0);						// extra field length
			putText(out, entry.name);
			out.insert(out.end(), entry.data.begin(), entry.data.end());
			if(out.size() > ZIP_SIZE_LIMIT)
			{
				return report(error, "archive passes 4 GiB at entry '" +
					entry.name + "' - past what the zip format carries "
					"without zip64");
			}
		}
		const unsigned long long directoryOffset = out.size();
		for(std::size_t index = 0; index < this->mEntries.size(); ++index)
		{
			Entry const & entry = this->mEntries[index];
			put32(out, 0x02014b50);				// central directory header
			// the "made by" high byte is the host system: 3 = unix, which is
			// what tells an unpacker the external attributes carry a mode
			put16(out, (3 << 8) | 20);
			put16(out, 20);						// version needed
			put16(out, 0);						// flags
			put16(out, entry.method);
			put16(out, DOS_TIME);
			put16(out, DOS_DATE);
			put32(out, entry.crc);
			put32(out, entry.data.size());
			put32(out, entry.originalSize);
			put16(out, static_cast<unsigned int>(entry.name.size()));
			put16(out, 0);						// extra field length
			put16(out, 0);						// comment length
			put16(out, 0);						// disk number start
			put16(out, 0);						// internal attributes
			put32(out, entry.externalAttributes);
			put32(out, offsets[index]);
			putText(out, entry.name);
		}
		const unsigned long long directorySize = out.size() - directoryOffset;
		put32(out, 0x06054b50);					// end of central directory
		put16(out, 0);							// this disk
		put16(out, 0);							// disk with the directory
		put16(out, static_cast<unsigned int>(this->mEntries.size()));
		put16(out, static_cast<unsigned int>(this->mEntries.size()));
		put32(out, directorySize);
		put32(out, directoryOffset);
		put16(out, 0);							// comment length
		return true;
	}
	//---------------------------------------------------------
	bool ExportZip::write(Orkige::String const & path,
		Orkige::String * error) const
	{
		std::vector<unsigned char> bytes;
		if(!this->finish(bytes, error))
		{
			return false;
		}
		return ExportFiles::writeBytes(path, bytes, error);
	}
}
