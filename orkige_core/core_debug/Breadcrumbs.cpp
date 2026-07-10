/**************************************************************
	created:	2026/07/11 at 11:00
	filename: 	Breadcrumbs.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "core_debug/Breadcrumbs.h"

#include <chrono>
#include <cstdio>
#include <sstream>

namespace Orkige
{
	IMPL_OSINGLETON(Breadcrumbs);

	const size_t Breadcrumbs::DEFAULT_MAX_ENTRIES = 256;
	const size_t Breadcrumbs::DEFAULT_MAX_MESSAGE = 512;
	const size_t Breadcrumbs::DEFAULT_MAX_FILE_BYTES = 128 * 1024;

	namespace
	{
		//! monotonic seconds since some fixed epoch (steady clock; only deltas
		//! matter, so the epoch itself is irrelevant)
		double monotonicSeconds()
		{
			return std::chrono::duration<double>(
				std::chrono::steady_clock::now().time_since_epoch()).count();
		}
		//---------------------------------------------------------
		//! append a JSON string literal (quoted, escaped) - breadcrumb text may
		//! hold quotes/newlines (log lines, script error messages)
		void appendJsonString(String & out, String const & value)
		{
			out += '"';
			for (char const c : value)
			{
				switch (c)
				{
				case '"':	out += "\\\"";	break;
				case '\\':	out += "\\\\";	break;
				case '\b':	out += "\\b";	break;
				case '\f':	out += "\\f";	break;
				case '\n':	out += "\\n";	break;
				case '\r':	out += "\\r";	break;
				case '\t':	out += "\\t";	break;
				default:
					if (static_cast<unsigned char>(c) < 0x20)
					{
						char buffer[8];
						std::snprintf(buffer, sizeof(buffer), "\\u%04x",
							static_cast<unsigned>(static_cast<unsigned char>(c)));
						out += buffer;
					}
					else
					{
						out += c;	// UTF-8 bytes pass through
					}
					break;
				}
			}
			out += '"';
		}
	}
	//---------------------------------------------------------
	Breadcrumbs::Breadcrumbs(size_t maxEntries, size_t maxFileBytes)
		: mMaxEntries(maxEntries == 0 ? 1 : maxEntries)
		, mMaxMessage(DEFAULT_MAX_MESSAGE)
		, mMaxFileBytes(maxFileBytes == 0 ? 1 : maxFileBytes)
		, mFileBytes(0)
		, mStartSeconds(monotonicSeconds())
	{
	}
	//---------------------------------------------------------
	Breadcrumbs::~Breadcrumbs()
	{
		if (this->mStream.is_open())
		{
			this->mStream.flush();
			this->mStream.close();
		}
	}
	//---------------------------------------------------------
	void Breadcrumbs::setFile(String const & path)
	{
		if (this->mStream.is_open())
		{
			this->mStream.close();
		}
		this->mFile = path;
		this->mFileBytes = 0;
		// derive the rotated (previous-session) path: insert ".prev" before the
		// ".jsonl" suffix, else append ".prev"
		const std::string::size_type dot = path.rfind(".jsonl");
		if (dot != std::string::npos && dot + 6 == path.size())
		{
			this->mPrevFile = path.substr(0, dot) + ".prev.jsonl";
		}
		else if (!path.empty())
		{
			this->mPrevFile = path + ".prev";
		}
		else
		{
			this->mPrevFile.clear();
		}
	}
	//---------------------------------------------------------
	void Breadcrumbs::rotate()
	{
		if (this->mStream.is_open())
		{
			this->mStream.close();
		}
		this->mRing.clear();
		this->mFileBytes = 0;
		if (this->mFile.empty())
		{
			return;
		}
		// move the live file to the previous slot (replacing any older one) so a
		// fresh session's trail never clobbers the crash trail from the last run
		std::remove(this->mPrevFile.c_str());
		std::rename(this->mFile.c_str(), this->mPrevFile.c_str());
		this->openStream();
	}
	//---------------------------------------------------------
	void Breadcrumbs::openStream()
	{
		if (this->mFile.empty())
		{
			return;
		}
		this->mStream.open(this->mFile.c_str(),
			std::ios::binary | std::ios::app);
	}
	//---------------------------------------------------------
	void Breadcrumbs::rewriteFromRing()
	{
		if (this->mFile.empty())
		{
			return;
		}
		if (this->mStream.is_open())
		{
			this->mStream.close();
		}
		// enforce the hard byte cap: drop oldest entries until the ring fits
		// (the in-memory ring shrinks with the file so both stay bounded)
		size_t ringBytes = 0;
		for (String const & line : this->mRing)
		{
			ringBytes += line.size() + 1;
		}
		while (ringBytes > this->mMaxFileBytes && this->mRing.size() > 1)
		{
			ringBytes -= this->mRing.front().size() + 1;
			this->mRing.pop_front();
		}
		std::ofstream fresh(this->mFile.c_str(),
			std::ios::binary | std::ios::trunc);
		size_t bytes = 0;
		if (fresh)
		{
			for (String const & line : this->mRing)
			{
				fresh.write(line.data(),
					static_cast<std::streamsize>(line.size()));
				fresh.put('\n');
				bytes += line.size() + 1;
			}
			fresh.flush();
		}
		this->mFileBytes = bytes;
		// reopen the append stream for subsequent records
		this->openStream();
	}
	//---------------------------------------------------------
	void Breadcrumbs::record(String const & kind, String const & msg,
		std::vector<std::pair<String, String>> const & fields)
	{
		String text = msg;
		if (text.size() > this->mMaxMessage)
		{
			text.resize(this->mMaxMessage);
			text += "...";
		}
		String line = "{\"t\":";
		char timeBuffer[32];
		std::snprintf(timeBuffer, sizeof(timeBuffer), "%.3f",
			monotonicSeconds() - this->mStartSeconds);
		line += timeBuffer;
		line += ",\"kind\":";
		appendJsonString(line, kind);
		line += ",\"msg\":";
		appendJsonString(line, text);
		for (std::pair<String, String> const & field : fields)
		{
			line += ',';
			appendJsonString(line, field.first);
			line += ':';
			appendJsonString(line, field.second);
		}
		line += '}';

		this->mRing.push_back(line);
		while (this->mRing.size() > this->mMaxEntries)
		{
			this->mRing.pop_front();
		}

		if (this->mFile.empty())
		{
			return;	// memory-only ring (editor / no file set)
		}
		if (!this->mStream.is_open())
		{
			this->openStream();
		}
		if (this->mStream.is_open())
		{
			this->mStream.write(line.data(),
				static_cast<std::streamsize>(line.size()));
			this->mStream.put('\n');
			this->mStream.flush();	// crash survivability: on disk before we return
			this->mFileBytes += line.size() + 1;
		}
		// enforce the byte cap by rewriting from the (already bounded) ring - the
		// file then holds exactly the last N entries within the cap
		if (this->mFileBytes > this->mMaxFileBytes)
		{
			this->rewriteFromRing();
		}
	}
	//---------------------------------------------------------
	String Breadcrumbs::contents() const
	{
		String out;
		for (String const & line : this->mRing)
		{
			out += line;
			out += '\n';
		}
		return out;
	}
	//---------------------------------------------------------
	bool Breadcrumbs::loadFile(String const & path, String & outText)
	{
		std::ifstream file(path.c_str(), std::ios::binary);
		if (!file)
		{
			return false;
		}
		std::ostringstream contents;
		contents << file.rdbuf();
		outText = contents.str();
		return true;
	}
}
