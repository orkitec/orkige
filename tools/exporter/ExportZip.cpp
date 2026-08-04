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

#include <cstddef>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

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

		//! the largest archive this reads. A library archive is a handful of
		//! megabytes; anything past this is not one, and holding it whole in
		//! memory would be the wrong shape for whatever it is instead.
		const unsigned long long ZIP_READ_LIMIT = 512ull * 1024ull * 1024ull;

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
		//! the 16-bit little-endian value at @p offset (0 past the end)
		unsigned int get16(std::vector<unsigned char> const & bytes,
			std::size_t offset)
		{
			if(offset + 2 > bytes.size())
			{
				return 0;
			}
			return static_cast<unsigned int>(bytes[offset]) |
				(static_cast<unsigned int>(bytes[offset + 1]) << 8);
		}
		//---------------------------------------------------------
		//! the 32-bit little-endian value at @p offset (0 past the end)
		unsigned long long get32(std::vector<unsigned char> const & bytes,
			std::size_t offset)
		{
			if(offset + 4 > bytes.size())
			{
				return 0;
			}
			return static_cast<unsigned long long>(bytes[offset]) |
				(static_cast<unsigned long long>(bytes[offset + 1]) << 8) |
				(static_cast<unsigned long long>(bytes[offset + 2]) << 16) |
				(static_cast<unsigned long long>(bytes[offset + 3]) << 24);
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

	//--- reading -----------------------------------------------

	bool inflateRaw(std::vector<unsigned char> const & input,
		unsigned long long originalSize, std::vector<unsigned char> & out,
		Orkige::String * error)
	{
		out.clear();
		z_stream stream = {};
		// the same NEGATIVE window bit count the writer selects raw deflate
		// with - the headerless stream a zip entry carries
		if(inflateInit2(&stream, -MAX_WBITS) != Z_OK)
		{
			return report(error, "zlib refused a raw inflate stream");
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
			status = inflate(&stream, Z_NO_FLUSH);
			if(status != Z_OK && status != Z_STREAM_END &&
				status != Z_BUF_ERROR)
			{
				inflateEnd(&stream);
				return report(error, "inflate failed (zlib status " +
					std::to_string(status) + ")");
			}
			out.insert(out.end(), chunk.data(),
				chunk.data() + (chunk.size() - stream.avail_out));
			if(out.size() > originalSize)
			{
				inflateEnd(&stream);
				return report(error, "an archive entry inflates past the size "
					"its directory records");
			}
			if(status == Z_BUF_ERROR && stream.avail_in == 0)
			{
				// no input left and no progress possible: a truncated stream
				break;
			}
		}
		while(status != Z_STREAM_END);
		inflateEnd(&stream);
		if(out.size() != originalSize)
		{
			return report(error, "an archive entry inflates to " +
				std::to_string(out.size()) + " bytes, not the " +
				std::to_string(originalSize) + " its directory records");
		}
		return true;
	}
	//---------------------------------------------------------
	bool isSafeArchiveEntryName(Orkige::String const & entryName)
	{
		if(entryName.empty() || entryName[0] == '/')
		{
			return false;
		}
		// a backslash is a legal character in a zip name and a SEPARATOR on
		// Windows, so an unpacker that only splits on '/' there writes to a
		// path nobody inspected. Refuse it rather than guess which it meant.
		if(entryName.find('\\') != Orkige::String::npos)
		{
			return false;
		}
		if(entryName.size() >= 2 && entryName[1] == ':')
		{
			return false;
		}
		std::size_t begin = 0;
		while(begin <= entryName.size())
		{
			const std::size_t slash = entryName.find('/', begin);
			const Orkige::String part = (slash == Orkige::String::npos)
				? entryName.substr(begin) : entryName.substr(begin, slash - begin);
			if(part == "..")
			{
				return false;
			}
			if(slash == Orkige::String::npos)
			{
				break;
			}
			begin = slash + 1;
		}
		return true;
	}
	//---------------------------------------------------------
	bool ExportZipReader::open(Orkige::String const & path,
		Orkige::String * error)
	{
		this->mPath = path;
		this->mBytes.clear();
		this->mEntries.clear();
		this->mLocated.clear();
		if(ExportFiles::treeSize(path) > ZIP_READ_LIMIT)
		{
			return report(error, "'" + path + "' is larger than the " +
				std::to_string(ZIP_READ_LIMIT / (1024ull * 1024ull)) +
				" MiB an archive this export reads may be");
		}
		if(!ExportFiles::readBytes(path, this->mBytes, error))
		{
			return false;
		}
		// the end-of-central-directory record sits at the tail, after a comment
		// of up to 64 KiB - so it is found by scanning BACK for its signature
		const std::size_t minimum = 22;
		if(this->mBytes.size() < minimum)
		{
			return report(error, "'" + path + "' is too short to be a zip "
				"archive");
		}
		std::size_t end = this->mBytes.size() - minimum;
		const std::size_t floorOffset = (this->mBytes.size() > 0xFFFF + minimum)
			? this->mBytes.size() - 0xFFFF - minimum : 0;
		bool found = false;
		for(;;)
		{
			if(get32(this->mBytes, end) == 0x06054b50)
			{
				found = true;
				break;
			}
			if(end == floorOffset)
			{
				break;
			}
			--end;
		}
		if(!found)
		{
			return report(error, "'" + path + "' is not a zip archive (no end "
				"of central directory)");
		}
		const unsigned int count = get16(this->mBytes, end + 10);
		const unsigned long long directorySize = get32(this->mBytes, end + 12);
		const unsigned long long directoryOffset = get32(this->mBytes, end + 16);
		if(count == 0xFFFF || directoryOffset == 0xFFFFFFFFull ||
			directorySize == 0xFFFFFFFFull)
		{
			return report(error, "'" + path + "' is a zip64 archive, which this "
				"export does not read");
		}
		if(directoryOffset + directorySize > this->mBytes.size())
		{
			return report(error, "'" + path + "' has a central directory past "
				"its end");
		}
		std::size_t cursor = static_cast<std::size_t>(directoryOffset);
		for(unsigned int index = 0; index < count; ++index)
		{
			if(get32(this->mBytes, cursor) != 0x02014b50)
			{
				return report(error, "'" + path + "' has a malformed central "
					"directory entry");
			}
			const unsigned int flags = get16(this->mBytes, cursor + 8);
			Located located;
			located.method = get16(this->mBytes, cursor + 10);
			located.crc = static_cast<unsigned int>(
				get32(this->mBytes, cursor + 16));
			located.compressedSize = get32(this->mBytes, cursor + 20);
			located.entry.size = get32(this->mBytes, cursor + 24);
			const unsigned int nameLength = get16(this->mBytes, cursor + 28);
			const unsigned int extraLength = get16(this->mBytes, cursor + 30);
			const unsigned int commentLength = get16(this->mBytes, cursor + 32);
			located.localHeaderOffset = get32(this->mBytes, cursor + 42);
			const std::size_t nameOffset = cursor + 46;
			if(nameOffset + nameLength > this->mBytes.size())
			{
				return report(error, "'" + path + "' has a central directory "
					"entry name past its end");
			}
			located.entry.name.assign(
				reinterpret_cast<char const *>(this->mBytes.data() + nameOffset),
				nameLength);
			// bit 0 is the encryption flag: an entry nothing here holds a key
			// for, so it is named rather than unpacked as its ciphertext
			if((flags & 0x0001u) != 0)
			{
				return report(error, "'" + path + "' holds the encrypted entry '" +
					located.entry.name + "', which this export cannot read");
			}
			if(located.entry.size == 0xFFFFFFFFull ||
				located.compressedSize == 0xFFFFFFFFull)
			{
				return report(error, "'" + path + "' stores '" +
					located.entry.name + "' with zip64 sizes, which this export "
					"does not read");
			}
			if(located.method != ExportZip::METHOD_STORE &&
				located.method != ExportZip::METHOD_DEFLATE)
			{
				return report(error, "'" + path + "' stores '" +
					located.entry.name + "' with compression method " +
					std::to_string(located.method) + " - only stored and "
					"deflated entries are read");
			}
			located.entry.directory = !located.entry.name.empty() &&
				located.entry.name[located.entry.name.size() - 1] == '/';
			if(!located.entry.directory &&
				!isSafeArchiveEntryName(located.entry.name))
			{
				return report(error, "'" + path + "' names the entry '" +
					located.entry.name + "', which would unpack outside the "
					"directory it is unpacked into");
			}
			this->mEntries.push_back(located.entry);
			this->mLocated.push_back(located);
			cursor += 46 + nameLength + extraLength + commentLength;
			if(cursor > this->mBytes.size())
			{
				return report(error, "'" + path + "' has a central directory "
					"past its end");
			}
		}
		return true;
	}
	//---------------------------------------------------------
	bool ExportZipReader::has(Orkige::String const & name) const
	{
		for(Entry const & entry : this->mEntries)
		{
			if(entry.name == name)
			{
				return true;
			}
		}
		return false;
	}
	//---------------------------------------------------------
	bool ExportZipReader::read(Orkige::String const & name,
		std::vector<unsigned char> & out, Orkige::String * error) const
	{
		out.clear();
		for(Located const & located : this->mLocated)
		{
			if(located.entry.name != name)
			{
				continue;
			}
			// the local header repeats the name and carries its OWN extra
			// field, so where the bytes start is read from it rather than
			// assumed to match the central directory's
			const std::size_t header =
				static_cast<std::size_t>(located.localHeaderOffset);
			if(get32(this->mBytes, header) != 0x04034b50)
			{
				return report(error, "'" + this->mPath + "' has no local header "
					"for '" + name + "'");
			}
			const std::size_t start = header + 30 +
				get16(this->mBytes, header + 26) +
				get16(this->mBytes, header + 28);
			if(start + located.compressedSize > this->mBytes.size())
			{
				return report(error, "'" + this->mPath + "' has the entry '" +
					name + "' past its end");
			}
			const std::vector<unsigned char> stored(
				this->mBytes.begin() + static_cast<std::ptrdiff_t>(start),
				this->mBytes.begin() + static_cast<std::ptrdiff_t>(
					start + located.compressedSize));
			if(located.method == ExportZip::METHOD_STORE)
			{
				out = stored;
			}
			else if(!inflateRaw(stored, located.entry.size, out, error))
			{
				return false;
			}
			const unsigned int actual = static_cast<unsigned int>(crc32(0,
				out.empty() ? Z_NULL
					: reinterpret_cast<const Bytef *>(out.data()),
				static_cast<uInt>(out.size())));
			if(actual != located.crc)
			{
				out.clear();
				return report(error, "'" + this->mPath + "' entry '" + name +
					"' fails its checksum - the archive is damaged");
			}
			return true;
		}
		return report(error, "'" + this->mPath + "' has no entry '" + name + "'");
	}
}
