/**************************************************************
	created:	2026/07/10 at 14:00
	filename: 	TraceWriter.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "core_debugnet/TraceWriter.h"

#include <cstdio>
#include <fstream>

namespace Orkige
{
	const size_t TraceWriter::DEFAULT_MAX_BYTES = 2 * 1024 * 1024;
	const size_t TraceWriter::DEFAULT_MAX_OBJECTS = 64;

	namespace
	{
		//! bytes kept in reserve so the truncation marker always fits
		const size_t MARKER_RESERVE = 128;
		//! the marker line save() appends when the byte cap was hit
		const char* const TRUNCATION_MARKER =
			"{\"truncated\":1,\"reason\":\"byte cap\"}\n";

		//---------------------------------------------------------
		//! append a JSON string literal (quoted, escaped) - the trace carries
		//! object ids and log text, which may hold quotes/newlines
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
		//---------------------------------------------------------
		//! append a compact number (integers without a decimal point)
		void appendNumber(String & out, double value)
		{
			char buffer[32];
			std::snprintf(buffer, sizeof(buffer), "%.6g", value);
			out += buffer;
		}
		//---------------------------------------------------------
		//! append `"key":[x,y,z]`
		void appendVec3(String & out, char const * key, float const value[3])
		{
			out += '"';
			out += key;
			out += "\":[";
			appendNumber(out, value[0]);
			out += ',';
			appendNumber(out, value[1]);
			out += ',';
			appendNumber(out, value[2]);
			out += ']';
		}
	}
	//---------------------------------------------------------
	TraceWriter::TraceWriter(size_t maxBytes, size_t maxObjectsPerSample)
		: mMaxBytes(maxBytes)
		, mMaxObjects(maxObjectsPerSample == 0 ? 1 : maxObjectsPerSample)
		, mTruncated(false)
	{
	}
	//---------------------------------------------------------
	bool TraceWriter::appendLine(String const & line)
	{
		if (this->mTruncated)
		{
			return false;
		}
		if (this->mBuffer.size() + line.size() + 1 + MARKER_RESERVE >
			this->mMaxBytes)
		{
			this->mTruncated = true;	// keep whole lines; drop this one
			return false;
		}
		this->mBuffer += line;
		this->mBuffer += '\n';
		return true;
	}
	//---------------------------------------------------------
	void TraceWriter::addSample(double t, unsigned long frame, double dt,
		std::vector<ObjectSample> const & objects)
	{
		String line;
		line.reserve(64 + objects.size() * 48);
		line += "{\"t\":";
		appendNumber(line, t);
		line += ",\"frame\":";
		appendNumber(line, static_cast<double>(frame));
		line += ",\"dt\":";
		appendNumber(line, dt);
		if (objects.size() > this->mMaxObjects)
		{
			line += ",\"capped\":";
			appendNumber(line, static_cast<double>(objects.size()));
		}
		line += ",\"objects\":[";
		const size_t count = objects.size() < this->mMaxObjects
			? objects.size() : this->mMaxObjects;
		for (size_t i = 0; i < count; ++i)
		{
			ObjectSample const & object = objects[i];
			if (i != 0)
			{
				line += ',';
			}
			line += "{\"id\":";
			appendJsonString(line, object.id);
			line += ",\"name\":";
			appendJsonString(line, object.name);
			line += ',';
			appendVec3(line, "pos", object.pos);
			if (object.hasVelocity)
			{
				line += ',';
				appendVec3(line, "vel", object.vel);
			}
			line += ",\"active\":";
			line += object.active ? '1' : '0';
			if (object.visible >= 0)
			{
				line += ",\"visible\":";
				line += object.visible ? '1' : '0';
			}
			line += '}';
		}
		line += "]}";
		this->appendLine(line);
	}
	//---------------------------------------------------------
	void TraceWriter::addEvent(double t, unsigned long frame,
		String const & event,
		std::vector<std::pair<String, String>> const & fields)
	{
		String line;
		line += "{\"t\":";
		appendNumber(line, t);
		line += ",\"frame\":";
		appendNumber(line, static_cast<double>(frame));
		line += ",\"event\":";
		appendJsonString(line, event);
		for (std::pair<String, String> const & field : fields)
		{
			line += ',';
			appendJsonString(line, field.first);
			line += ':';
			appendJsonString(line, field.second);
		}
		line += '}';
		this->appendLine(line);
	}
	//---------------------------------------------------------
	bool TraceWriter::save(String const & path) const
	{
		std::ofstream file(path.c_str(), std::ios::binary);
		if (!file)
		{
			return false;
		}
		file.write(this->mBuffer.data(),
			static_cast<std::streamsize>(this->mBuffer.size()));
		if (this->mTruncated)
		{
			file << TRUNCATION_MARKER;
		}
		return file.good();
	}
}
