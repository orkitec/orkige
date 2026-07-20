/********************************************************************
	created:	Friday 2026/07/18 at 06:00
	filename: 	MiniZip.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

#include "engine_filesystem/MiniZip.h"

#include "core_util/PathJail.h"
#include "core_debug/DebugMacros.h"

#include <zlib.h>

#include <algorithm>
#include <cstring>
#include <fstream>

namespace Orkige
{
	namespace
	{
		//! signatures (little-endian on disk)
		const std::uint32_t kEndOfCentralDirSig	= 0x06054b50;
		const std::uint32_t kCentralDirSig		= 0x02014b50;
		const std::uint32_t kLocalHeaderSig		= 0x04034b50;

		std::uint16_t readU16(unsigned char const * p)
		{
			return static_cast<std::uint16_t>(p[0]) |
				(static_cast<std::uint16_t>(p[1]) << 8);
		}
		std::uint32_t readU32(unsigned char const * p)
		{
			return static_cast<std::uint32_t>(p[0]) |
				(static_cast<std::uint32_t>(p[1]) << 8) |
				(static_cast<std::uint32_t>(p[2]) << 16) |
				(static_cast<std::uint32_t>(p[3]) << 24);
		}
	}
	//---------------------------------------------------------
	MiniZip::MiniZip()
		: mOpen(false)
	{
	}
	//---------------------------------------------------------
	MiniZip::~MiniZip()
	{
	}
	//---------------------------------------------------------
	bool MiniZip::open(std::string const & path)
	{
		this->mEntries.clear();
		this->mOpen = false;
		this->mPath = path;

		std::ifstream file(path, std::ios::binary);
		if(!file)
		{
			return false;
		}
		file.seekg(0, std::ios::end);
		const std::streamoff fileSize = file.tellg();
		if(fileSize < 22)
		{
			return false;	// smaller than an empty zip's EOCD
		}

		// find the End Of Central Directory record: scan backwards over the
		// 22-byte EOCD plus up to a 65535-byte trailing comment
		const std::streamoff maxBack =
			std::min<std::streamoff>(fileSize, 22 + 65535);
		std::vector<unsigned char> tail(static_cast<std::size_t>(maxBack));
		file.seekg(fileSize - maxBack, std::ios::beg);
		file.read(reinterpret_cast<char*>(tail.data()), maxBack);
		if(!file)
		{
			return false;
		}
		std::streamoff eocd = -1;
		for(std::streamoff i = maxBack - 22; i >= 0; --i)
		{
			if(readU32(&tail[static_cast<std::size_t>(i)]) == kEndOfCentralDirSig)
			{
				eocd = i;
				break;
			}
		}
		if(eocd < 0)
		{
			return false;	// no EOCD - not a zip
		}
		unsigned char const * e = &tail[static_cast<std::size_t>(eocd)];
		const std::uint16_t entryCount = readU16(e + 10);
		const std::uint32_t centralDirSize = readU32(e + 12);
		const std::uint32_t centralDirOffset = readU32(e + 16);

		// bound the directory against the real file: it physically lives inside
		// the archive, so an offset/size larger than the bytes on disk is a
		// malformed (or hostile) EOCD - reject BEFORE allocating, so a forged
		// record cannot drive a multi-gigabyte allocation from a tiny file (a
		// downloaded pak / an APK is untrusted on every mobile boot)
		if(centralDirOffset > static_cast<std::uint64_t>(fileSize) ||
			centralDirSize >
				static_cast<std::uint64_t>(fileSize) - centralDirOffset)
		{
			return false;
		}

		// read the whole central directory (small: one record per entry)
		std::vector<unsigned char> central(centralDirSize);
		if(centralDirSize > 0)
		{
			file.seekg(centralDirOffset, std::ios::beg);
			file.read(reinterpret_cast<char*>(central.data()), centralDirSize);
			if(!file)
			{
				return false;
			}
		}

		std::size_t cursor = 0;
		for(std::uint16_t index = 0; index < entryCount; ++index)
		{
			if(cursor + 46 > central.size() ||
				readU32(&central[cursor]) != kCentralDirSig)
			{
				return false;	// truncated / malformed central directory
			}
			unsigned char const * c = &central[cursor];
			const std::uint16_t method = readU16(c + 10);
			const std::uint32_t compressedSize = readU32(c + 20);
			const std::uint32_t uncompressedSize = readU32(c + 24);
			const std::uint16_t nameLength = readU16(c + 28);
			const std::uint16_t extraLength = readU16(c + 30);
			const std::uint16_t commentLength = readU16(c + 32);
			const std::uint32_t localOffset = readU32(c + 42);
			if(cursor + 46 + nameLength > central.size())
			{
				return false;
			}
			std::string name(reinterpret_cast<char const*>(c + 46), nameLength);
			// zip-slip guard: an entry whose name is absolute, drive-rooted or
			// carries a ".." traversal escapes the archive root - drop it here,
			// the ONE choke point, so it can never be resolved in memory NOR
			// written out by any extract-to-disk consumer (Docs/filesystem.md).
			if(!name.empty() && name.back() != '/' &&
				!PathJail::isSafeRelativeEntry(name))
			{
				oDebugWarn("filesystem", 0, "MiniZip: skipping unsafe zip "
					"entry '" << name << "' in '" << this->mPath
					<< "' (path traversal / absolute path)");
			}
			// directory entries (trailing '/') carry no data - skip them
			else if(!name.empty() && name.back() != '/')
			{
				Entry entry;
				entry.method = method;
				entry.compressedSize = compressedSize;
				entry.uncompressedSize = uncompressedSize;
				entry.localHeaderOffset = localOffset;
				this->mEntries[name] = entry;
			}
			cursor += 46u + nameLength + extraLength + commentLength;
		}
		this->mOpen = true;
		return true;
	}
	//---------------------------------------------------------
	std::vector<std::string> MiniZip::names() const
	{
		std::vector<std::string> out;
		out.reserve(this->mEntries.size());
		for(std::map<std::string, Entry>::const_iterator it =
			this->mEntries.begin(); it != this->mEntries.end(); ++it)
		{
			out.push_back(it->first);
		}
		return out;
	}
	//---------------------------------------------------------
	bool MiniZip::contains(std::string const & name) const
	{
		return this->mEntries.find(name) != this->mEntries.end();
	}
	//---------------------------------------------------------
	bool MiniZip::sizeOf(std::string const & name,
		std::uint64_t & outUncompressedSize) const
	{
		std::map<std::string, Entry>::const_iterator it =
			this->mEntries.find(name);
		if(it == this->mEntries.end())
		{
			return false;
		}
		outUncompressedSize = it->second.uncompressedSize;
		return true;
	}
	//---------------------------------------------------------
	bool MiniZip::read(std::string const & name,
		std::vector<unsigned char> & out) const
	{
		std::map<std::string, Entry>::const_iterator it =
			this->mEntries.find(name);
		if(it == this->mEntries.end())
		{
			return false;
		}
		const Entry & entry = it->second;

		std::ifstream file(this->mPath, std::ios::binary);
		if(!file)
		{
			return false;
		}
		file.seekg(0, std::ios::end);
		const std::streamoff fileSize = file.tellg();
		// the local header repeats the name/extra lengths; the data starts past
		// them (the central-directory copies of those lengths may differ)
		unsigned char localHeader[30];
		file.seekg(static_cast<std::streamoff>(entry.localHeaderOffset),
			std::ios::beg);
		file.read(reinterpret_cast<char*>(localHeader), 30);
		if(!file || readU32(localHeader) != kLocalHeaderSig)
		{
			return false;
		}
		const std::uint16_t localNameLength = readU16(localHeader + 26);
		const std::uint16_t localExtraLength = readU16(localHeader + 28);
		const std::streamoff dataOffset =
			static_cast<std::streamoff>(entry.localHeaderOffset) + 30 +
			localNameLength + localExtraLength;

		// the compressed bytes physically live in the archive; a compressedSize
		// larger than what remains of the file past the data offset is malformed
		// - reject before allocating so a forged directory record cannot drive a
		// huge allocation (the same untrusted-archive guard as open())
		if(dataOffset < 0 || dataOffset > fileSize ||
			entry.compressedSize >
				static_cast<std::uint64_t>(fileSize - dataOffset))
		{
			return false;
		}
		std::vector<unsigned char> compressed(
			static_cast<std::size_t>(entry.compressedSize));
		if(entry.compressedSize > 0)
		{
			file.seekg(dataOffset, std::ios::beg);
			file.read(reinterpret_cast<char*>(compressed.data()),
				static_cast<std::streamsize>(entry.compressedSize));
			if(!file)
			{
				return false;
			}
		}

		if(entry.method == 0)	// STORED - the mount-in-place / APK case
		{
			if(entry.compressedSize != entry.uncompressedSize)
			{
				return false;
			}
			out.swap(compressed);
			return true;
		}
		if(entry.method == 8)	// DEFLATE - inflate via zlib (raw stream)
		{
			out.clear();
			if(entry.uncompressedSize == 0)
			{
				return true;
			}
			z_stream stream;
			std::memset(&stream, 0, sizeof(stream));
			// negative window bits selects a RAW deflate stream (no zlib header)
			if(inflateInit2(&stream, -MAX_WBITS) != Z_OK)
			{
				return false;
			}
			stream.next_in = compressed.empty() ? Z_NULL : compressed.data();
			stream.avail_in = static_cast<uInt>(compressed.size());
			// inflate in chunks, GROWING the output only as bytes are actually
			// produced. The directory's uncompressedSize is treated as a CEILING
			// to verify against, NEVER a size to pre-allocate, so a forged huge
			// size can never drive the allocation - it simply fails the exact
			// size check below (the untrusted-archive DoS guard).
			out.reserve(static_cast<std::size_t>(
				std::min<std::uint64_t>(entry.uncompressedSize, 1u << 20)));
			unsigned char chunk[65536];
			int result = Z_OK;
			bool overflow = false;
			do
			{
				stream.next_out = chunk;
				stream.avail_out = static_cast<uInt>(sizeof(chunk));
				result = inflate(&stream, Z_NO_FLUSH);
				if(result != Z_OK && result != Z_STREAM_END)
				{
					break;	// Z_DATA_ERROR / Z_BUF_ERROR / ... - corrupt stream
				}
				const std::size_t produced = sizeof(chunk) - stream.avail_out;
				if(produced > entry.uncompressedSize - out.size())
				{
					overflow = true;	// more than declared - corrupt directory
					break;
				}
				out.insert(out.end(), chunk, chunk + produced);
			}
			while(result != Z_STREAM_END);
			inflateEnd(&stream);
			const bool ok = !overflow && result == Z_STREAM_END &&
				out.size() == entry.uncompressedSize;
			if(!ok)
			{
				out.clear();
			}
			return ok;
		}
		return false;	// unsupported compression method
	}
}
