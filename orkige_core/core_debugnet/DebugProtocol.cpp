/**************************************************************
	created:	2026/07/07 at 23:30
	filename: 	DebugProtocol.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "core_debugnet/DebugProtocol.h"

#include <cstdio>
#include <cstdlib>

namespace Orkige
{
	namespace DebugProtocol
	{
		const int VERSION = 1;
		const unsigned int MAX_LINE_LENGTH = 64 * 1024;

		const String MSG_PAUSE				= "pause";
		const String MSG_RESUME				= "resume";
		const String MSG_STEP				= "step";
		const String MSG_QUIT				= "quit";
		const String MSG_SELECT				= "select";
		const String MSG_SET_PROPERTY		= "set_property";
		const String MSG_REQUEST_HIERARCHY	= "request_hierarchy";
		const String MSG_SET_ACTIVE			= "set_active";
		const String MSG_RELOAD_SCRIPT		= "reload_script";
		const String MSG_SET_CVAR			= "set_cvar";
		const String MSG_SCREENSHOT			= "screenshot";
		const String MSG_RECORD_START		= "record_start";
		const String MSG_RECORD_STOP		= "record_stop";

		const String MSG_HELLO				= "hello";
		const String MSG_HIERARCHY			= "hierarchy";
		const String MSG_OBJECT_STATE		= "object_state";
		const String MSG_LOG				= "log";
		const String MSG_ERROR				= "error";
		const String MSG_SCRIPT_ERROR		= "script_error";
		const String MSG_SCREENSHOT_SAVED	= "screenshot_saved";
		const String MSG_RECORD_SAVED		= "record_saved";
		const String MSG_BYE				= "bye";

		const String FIELD_ID				= "id";
		const String FIELD_COMPONENT		= "component";
		const String FIELD_PROPERTY			= "property";
		const String FIELD_VALUE			= "value";
		const String FIELD_CVAR_NAME		= "cvar";
		const String FIELD_PATH				= "path";
		const String FIELD_SECONDS			= "seconds";
		const String FIELD_EVERY			= "every";
		const String FIELD_FILTER			= "filter";
		const String FIELD_SCENE			= "scene";
		const String FIELD_MESSAGE			= "message";
		const String FIELD_LEVEL			= "level";
		const String FIELD_REQ				= "req";
		const String FIELD_TOKEN			= "token";
		const String LIST_IDS				= "ids";
		const String LIST_COMPONENTS		= "components";
		const String LIST_PARENTS			= "parents";
		const String LIST_ACTIVE			= "active";
		const String LIST_PROP_KEYS			= "pkeys";
		const String LIST_PROP_KINDS		= "pkinds";
		const String LIST_PROP_HINTS		= "phints";
		const String LIST_PROP_FLAGS		= "pflags";
	}

	namespace
	{
		const String EMPTY_STRING;
		const StringVector EMPTY_LIST;
		//---------------------------------------------------------
		//! append a JSON string literal (quoted, escaped) to out
		void jsonAppendString(String & out, String const & value)
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
						// UTF-8 bytes pass through untouched
						out += c;
					}
					break;
				}
			}
			out += '"';
		}
		//---------------------------------------------------------
		//! append a UTF-8 encoding of the given unicode code point to out
		void utf8Append(String & out, unsigned int codePoint)
		{
			if (codePoint < 0x80)
			{
				out += static_cast<char>(codePoint);
			}
			else if (codePoint < 0x800)
			{
				out += static_cast<char>(0xC0 | (codePoint >> 6));
				out += static_cast<char>(0x80 | (codePoint & 0x3F));
			}
			else if (codePoint < 0x10000)
			{
				out += static_cast<char>(0xE0 | (codePoint >> 12));
				out += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
				out += static_cast<char>(0x80 | (codePoint & 0x3F));
			}
			else
			{
				out += static_cast<char>(0xF0 | (codePoint >> 18));
				out += static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F));
				out += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
				out += static_cast<char>(0x80 | (codePoint & 0x3F));
			}
		}
		//---------------------------------------------------------
		//! minimal recursive-descent reader over the protocol's JSON subset;
		//! all methods return false on malformed input and never throw
		class JsonReader
		{
		public:
			explicit JsonReader(String const & text) : text(text), pos(0) {}
			//! @brief the reader keeps a reference to the source text, so binding
			//! it to a TEMPORARY string would dangle - forbid that at the API.
			//! decode() only ever constructs it from its own String const& line
			//! parameter (an lvalue that outlives the reader).
			JsonReader(String &&) = delete;
			//---------------------------------------------------------
			void skipWhitespace()
			{
				while (this->pos < this->text.size())
				{
					const char c = this->text[this->pos];
					if (c != ' ' && c != '\t' && c != '\r' && c != '\n')
					{
						break;
					}
					++this->pos;
				}
			}
			//---------------------------------------------------------
			bool atEnd()
			{
				this->skipWhitespace();
				return this->pos >= this->text.size();
			}
			//---------------------------------------------------------
			bool consume(char expected)
			{
				this->skipWhitespace();
				if (this->pos >= this->text.size() ||
					this->text[this->pos] != expected)
				{
					return false;
				}
				++this->pos;
				return true;
			}
			//---------------------------------------------------------
			bool peek(char & out)
			{
				this->skipWhitespace();
				if (this->pos >= this->text.size())
				{
					return false;
				}
				out = this->text[this->pos];
				return true;
			}
			//---------------------------------------------------------
			//! parse a JSON string literal (opening quote at the cursor)
			bool readString(String & out)
			{
				if (!this->consume('"'))
				{
					return false;
				}
				out.clear();
				while (this->pos < this->text.size())
				{
					const char c = this->text[this->pos++];
					if (c == '"')
					{
						return true;
					}
					if (c != '\\')
					{
						out += c;
						continue;
					}
					if (this->pos >= this->text.size())
					{
						return false; // dangling escape
					}
					const char escape = this->text[this->pos++];
					switch (escape)
					{
					case '"':	out += '"';		break;
					case '\\':	out += '\\';	break;
					case '/':	out += '/';		break;
					case 'b':	out += '\b';	break;
					case 'f':	out += '\f';	break;
					case 'n':	out += '\n';	break;
					case 'r':	out += '\r';	break;
					case 't':	out += '\t';	break;
					case 'u':
					{
						unsigned int codePoint = 0;
						if (!this->readHex4(codePoint))
						{
							return false;
						}
						// combine a UTF-16 surrogate pair when one follows
						if (codePoint >= 0xD800 && codePoint <= 0xDBFF &&
							this->pos + 1 < this->text.size() &&
							this->text[this->pos] == '\\' &&
							this->text[this->pos + 1] == 'u')
						{
							const size_t rewind = this->pos;
							this->pos += 2;
							unsigned int low = 0;
							if (this->readHex4(low) &&
								low >= 0xDC00 && low <= 0xDFFF)
							{
								codePoint = 0x10000 +
									((codePoint - 0xD800) << 10) +
									(low - 0xDC00);
							}
							else
							{
								this->pos = rewind; // lone high surrogate
							}
						}
						utf8Append(out, codePoint);
						break;
					}
					default:
						return false; // unknown escape
					}
				}
				return false; // unterminated string
			}
			//---------------------------------------------------------
			//! parse a bare JSON scalar (number/true/false/null) as its
			//! literal text; validates numbers strictly enough to reject junk
			bool readBareScalar(String & out)
			{
				this->skipWhitespace();
				const size_t start = this->pos;
				while (this->pos < this->text.size())
				{
					const char c = this->text[this->pos];
					if (c == ',' || c == '}' || c == ']' || c == ' ' ||
						c == '\t' || c == '\r' || c == '\n')
					{
						break;
					}
					++this->pos;
				}
				out = this->text.substr(start, this->pos - start);
				if (out == "true" || out == "false" || out == "null")
				{
					return true;
				}
				if (out.empty())
				{
					return false;
				}
				// number: [-]digits[.digits][(e|E)[+-]digits]
				char* end = NULL;
				std::strtod(out.c_str(), &end);
				return end == out.c_str() + out.size();
			}
			//---------------------------------------------------------
			size_t position() const { return this->pos; }
		private:
			//---------------------------------------------------------
			bool readHex4(unsigned int & out)
			{
				if (this->pos + 4 > this->text.size())
				{
					return false;
				}
				out = 0;
				for (int i = 0; i < 4; ++i)
				{
					const char c = this->text[this->pos++];
					out <<= 4;
					if (c >= '0' && c <= '9')		out |= (c - '0');
					else if (c >= 'a' && c <= 'f')	out |= (c - 'a' + 10);
					else if (c >= 'A' && c <= 'F')	out |= (c - 'A' + 10);
					else
					{
						return false;
					}
				}
				return true;
			}
			String const &	text;
			size_t			pos;
		};
	}
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	DebugMessage::DebugMessage()
	{
		this->version = DebugProtocol::VERSION;
	}
	//---------------------------------------------------------
	DebugMessage::DebugMessage(String const & messageType)
	{
		this->version = DebugProtocol::VERSION;
		this->type = messageType;
	}
	//---------------------------------------------------------
	void DebugMessage::set(String const & key, String const & value)
	{
		this->fields[key] = value;
	}
	//---------------------------------------------------------
	void DebugMessage::setFloat(String const & key, float value)
	{
		char buffer[64];
		std::snprintf(buffer, sizeof(buffer), "%.9g", value);
		this->fields[key] = buffer;
	}
	//---------------------------------------------------------
	String const & DebugMessage::get(String const & key) const
	{
		FieldMap::const_iterator it = this->fields.find(key);
		return (it == this->fields.end()) ? EMPTY_STRING : it->second;
	}
	//---------------------------------------------------------
	bool DebugMessage::has(String const & key) const
	{
		return this->fields.find(key) != this->fields.end();
	}
	//---------------------------------------------------------
	float DebugMessage::getFloat(String const & key, float defaultValue) const
	{
		FieldMap::const_iterator it = this->fields.find(key);
		if (it == this->fields.end() || it->second.empty())
		{
			return defaultValue;
		}
		char* end = NULL;
		const double value = std::strtod(it->second.c_str(), &end);
		if (end == it->second.c_str())
		{
			return defaultValue;
		}
		return static_cast<float>(value);
	}
	//---------------------------------------------------------
	void DebugMessage::setList(String const & key, StringVector const & values)
	{
		this->lists[key] = values;
	}
	//---------------------------------------------------------
	StringVector const & DebugMessage::getList(String const & key) const
	{
		ListMap::const_iterator it = this->lists.find(key);
		return (it == this->lists.end()) ? EMPTY_LIST : it->second;
	}
	//---------------------------------------------------------
	String DebugMessage::encode() const
	{
		String out;
		out.reserve(64);
		out += "{\"v\":";
		char versionBuffer[16];
		std::snprintf(versionBuffer, sizeof(versionBuffer), "%d", this->version);
		out += versionBuffer;
		out += ",\"type\":";
		jsonAppendString(out, this->type);
		for (FieldMap::value_type const & field : this->fields)
		{
			out += ',';
			jsonAppendString(out, field.first);
			out += ':';
			jsonAppendString(out, field.second);
		}
		for (ListMap::value_type const & list : this->lists)
		{
			out += ',';
			jsonAppendString(out, list.first);
			out += ":[";
			bool first = true;
			for (String const & value : list.second)
			{
				if (!first)
				{
					out += ',';
				}
				first = false;
				jsonAppendString(out, value);
			}
			out += ']';
		}
		out += '}';
		return out;
	}
	//---------------------------------------------------------
	bool DebugMessage::decode(String const & line, DebugMessage & out)
	{
		JsonReader reader(line);
		if (!reader.consume('{'))
		{
			return false;
		}
		DebugMessage decoded;
		decoded.version = 0;
		char next = 0;
		if (!reader.peek(next))
		{
			return false;
		}
		bool first = true;
		while (next != '}')
		{
			if (!first && !reader.consume(','))
			{
				return false;
			}
			first = false;
			String key;
			if (!reader.readString(key) || !reader.consume(':'))
			{
				return false;
			}
			if (!reader.peek(next))
			{
				return false;
			}
			if (next == '[')
			{
				// flat array of strings/numbers
				reader.consume('[');
				StringVector values;
				if (!reader.peek(next))
				{
					return false;
				}
				bool firstValue = true;
				while (next != ']')
				{
					if (!firstValue && !reader.consume(','))
					{
						return false;
					}
					firstValue = false;
					String value;
					if (!reader.peek(next))
					{
						return false;
					}
					if (next == '"')
					{
						if (!reader.readString(value))
						{
							return false;
						}
					}
					else if (next == '{' || next == '[')
					{
						return false; // nesting is not part of the protocol
					}
					else if (!reader.readBareScalar(value))
					{
						return false;
					}
					values.push_back(value);
					if (!reader.peek(next))
					{
						return false;
					}
				}
				reader.consume(']');
				decoded.lists[key] = values;
			}
			else
			{
				String value;
				if (next == '"')
				{
					if (!reader.readString(value))
					{
						return false;
					}
				}
				else if (next == '{')
				{
					return false; // nested objects are not part of the protocol
				}
				else if (!reader.readBareScalar(value))
				{
					return false;
				}
				if (key == "v")
				{
					decoded.version = std::atoi(value.c_str());
				}
				else if (key == "type")
				{
					decoded.type = value;
				}
				else
				{
					decoded.fields[key] = value;
				}
			}
			if (!reader.peek(next))
			{
				return false;
			}
		}
		reader.consume('}');
		if (!reader.atEnd())
		{
			return false; // trailing garbage after the object
		}
		if (decoded.type.empty())
		{
			return false; // every protocol message carries a type
		}
		out = decoded;
		return true;
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------

	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
}
