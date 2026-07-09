/**************************************************************
	created:	2026/07/09 at 12:00
	filename: 	Json.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "core_debugnet/Json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace Orkige
{
	namespace
	{
		const String EMPTY_STRING;
		const JsonValue NULL_VALUE;
		//! deepest nesting the parser accepts (stack-exhaustion guard)
		const int MAX_DEPTH = 64;
		//---------------------------------------------------------
		//! append a JSON string literal (quoted, escaped) to out - the same
		//! escaping the debug protocol codec uses (control chars -> \uXXXX,
		//! UTF-8 bytes pass through so the line stays valid single-line JSON)
		void appendString(String & out, String const & value)
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
		//! recursive-descent JSON reader over the full (nested) grammar; every
		//! method returns false on malformed input and never throws
		class Reader
		{
		public:
			explicit Reader(String const & text) : text(text), pos(0) {}
			//! forbid binding to a temporary (the reader keeps a reference)
			Reader(String &&) = delete;
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
			bool readValue(JsonValue & out, int depth)
			{
				if (depth > MAX_DEPTH)
				{
					return false;
				}
				char next = 0;
				if (!this->peek(next))
				{
					return false;
				}
				switch (next)
				{
				case '"':
				{
					String value;
					if (!this->readString(value))
					{
						return false;
					}
					out = JsonValue(value);
					return true;
				}
				case '{':
					return this->readObject(out, depth);
				case '[':
					return this->readArray(out, depth);
				default:
					return this->readScalar(out);
				}
			}
			//---------------------------------------------------------
			size_t position() const { return this->pos; }
		private:
			//---------------------------------------------------------
			bool readObject(JsonValue & out, int depth)
			{
				if (!this->consume('{'))
				{
					return false;
				}
				JsonValue object = JsonValue::object();
				char next = 0;
				if (!this->peek(next))
				{
					return false;
				}
				bool first = true;
				while (next != '}')
				{
					if (!first && !this->consume(','))
					{
						return false;
					}
					first = false;
					String key;
					if (!this->readString(key) || !this->consume(':'))
					{
						return false;
					}
					JsonValue value;
					if (!this->readValue(value, depth + 1))
					{
						return false;
					}
					object.set(key, value);
					if (!this->peek(next))
					{
						return false;
					}
				}
				this->consume('}');
				out = object;
				return true;
			}
			//---------------------------------------------------------
			bool readArray(JsonValue & out, int depth)
			{
				if (!this->consume('['))
				{
					return false;
				}
				JsonValue array = JsonValue::array();
				char next = 0;
				if (!this->peek(next))
				{
					return false;
				}
				bool first = true;
				while (next != ']')
				{
					if (!first && !this->consume(','))
					{
						return false;
					}
					first = false;
					JsonValue value;
					if (!this->readValue(value, depth + 1))
					{
						return false;
					}
					array.push(value);
					if (!this->peek(next))
					{
						return false;
					}
				}
				this->consume(']');
				out = array;
				return true;
			}
			//---------------------------------------------------------
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
						return false;
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
								this->pos = rewind;
							}
						}
						utf8Append(out, codePoint);
						break;
					}
					default:
						return false;
					}
				}
				return false;
			}
			//---------------------------------------------------------
			//! parse a bare scalar: number / true / false / null
			bool readScalar(JsonValue & out)
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
				const String token = this->text.substr(start, this->pos - start);
				if (token == "true")
				{
					out = JsonValue(true);
					return true;
				}
				if (token == "false")
				{
					out = JsonValue(false);
					return true;
				}
				if (token == "null")
				{
					out = JsonValue();
					return true;
				}
				if (token.empty())
				{
					return false;
				}
				char* end = NULL;
				const double value = std::strtod(token.c_str(), &end);
				if (end != token.c_str() + token.size())
				{
					return false; // not a clean number
				}
				out = JsonValue(value);
				return true;
			}
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
	JsonValue::JsonValue()
		: mType(Type::Null), mBool(false), mNumber(0.0)
	{
	}
	//---------------------------------------------------------
	JsonValue::JsonValue(Type type)
		: mType(type), mBool(false), mNumber(0.0)
	{
	}
	//---------------------------------------------------------
	JsonValue::JsonValue(bool value)
		: mType(Type::Bool), mBool(value), mNumber(0.0)
	{
	}
	//---------------------------------------------------------
	JsonValue::JsonValue(double value)
		: mType(Type::Number), mBool(false), mNumber(value)
	{
	}
	//---------------------------------------------------------
	JsonValue::JsonValue(int value)
		: mType(Type::Number), mBool(false), mNumber(static_cast<double>(value))
	{
	}
	//---------------------------------------------------------
	JsonValue::JsonValue(String const & value)
		: mType(Type::String), mBool(false), mNumber(0.0), mString(value)
	{
	}
	//---------------------------------------------------------
	JsonValue::JsonValue(const char* value)
		: mType(Type::String), mBool(false), mNumber(0.0),
		  mString(value ? value : "")
	{
	}
	//---------------------------------------------------------
	JsonValue JsonValue::object()
	{
		return JsonValue(Type::Object);
	}
	//---------------------------------------------------------
	JsonValue JsonValue::array()
	{
		return JsonValue(Type::Array);
	}
	//---------------------------------------------------------
	bool JsonValue::asBool(bool defaultValue) const
	{
		return this->mType == Type::Bool ? this->mBool : defaultValue;
	}
	//---------------------------------------------------------
	double JsonValue::asNumber(double defaultValue) const
	{
		return this->mType == Type::Number ? this->mNumber : defaultValue;
	}
	//---------------------------------------------------------
	int JsonValue::asInt(int defaultValue) const
	{
		return this->mType == Type::Number
			? static_cast<int>(this->mNumber) : defaultValue;
	}
	//---------------------------------------------------------
	String const & JsonValue::asString() const
	{
		return this->mType == Type::String ? this->mString : EMPTY_STRING;
	}
	//---------------------------------------------------------
	void JsonValue::push(JsonValue value)
	{
		if (this->mType != Type::Array)
		{
			*this = JsonValue::array();
		}
		this->mArray.push_back(std::move(value));
	}
	//---------------------------------------------------------
	size_t JsonValue::size() const
	{
		if (this->mType == Type::Array)
		{
			return this->mArray.size();
		}
		if (this->mType == Type::Object)
		{
			return this->mObject.size();
		}
		return 0;
	}
	//---------------------------------------------------------
	JsonValue const & JsonValue::at(size_t index) const
	{
		if (this->mType == Type::Array && index < this->mArray.size())
		{
			return this->mArray[index];
		}
		return NULL_VALUE;
	}
	//---------------------------------------------------------
	void JsonValue::set(String const & key, JsonValue value)
	{
		if (this->mType != Type::Object)
		{
			*this = JsonValue::object();
		}
		for (std::pair<String, JsonValue> & member : this->mObject)
		{
			if (member.first == key)
			{
				member.second = std::move(value);
				return;
			}
		}
		this->mObject.emplace_back(key, std::move(value));
	}
	//---------------------------------------------------------
	bool JsonValue::has(String const & key) const
	{
		if (this->mType != Type::Object)
		{
			return false;
		}
		for (std::pair<String, JsonValue> const & member : this->mObject)
		{
			if (member.first == key)
			{
				return true;
			}
		}
		return false;
	}
	//---------------------------------------------------------
	JsonValue const & JsonValue::get(String const & key) const
	{
		if (this->mType == Type::Object)
		{
			for (std::pair<String, JsonValue> const & member : this->mObject)
			{
				if (member.first == key)
				{
					return member.second;
				}
			}
		}
		return NULL_VALUE;
	}
	//---------------------------------------------------------
	String JsonValue::serialize() const
	{
		String out;
		switch (this->mType)
		{
		case Type::Null:
			out += "null";
			break;
		case Type::Bool:
			out += this->mBool ? "true" : "false";
			break;
		case Type::Number:
		{
			char buffer[32];
			// integral values print without a decimal point (cleaner JSON-RPC
			// ids and counts); everything else keeps double round-trip
			if (std::isfinite(this->mNumber) &&
				this->mNumber == std::floor(this->mNumber) &&
				std::fabs(this->mNumber) < 1e15)
			{
				std::snprintf(buffer, sizeof(buffer), "%lld",
					static_cast<long long>(this->mNumber));
			}
			else if (!std::isfinite(this->mNumber))
			{
				// JSON has no NaN/Inf - emit null rather than invalid JSON
				out += "null";
				break;
			}
			else
			{
				std::snprintf(buffer, sizeof(buffer), "%.17g", this->mNumber);
			}
			out += buffer;
			break;
		}
		case Type::String:
			appendString(out, this->mString);
			break;
		case Type::Array:
		{
			out += '[';
			bool first = true;
			for (JsonValue const & value : this->mArray)
			{
				if (!first)
				{
					out += ',';
				}
				first = false;
				out += value.serialize();
			}
			out += ']';
			break;
		}
		case Type::Object:
		{
			out += '{';
			bool first = true;
			for (std::pair<String, JsonValue> const & member : this->mObject)
			{
				if (!first)
				{
					out += ',';
				}
				first = false;
				appendString(out, member.first);
				out += ':';
				out += member.second.serialize();
			}
			out += '}';
			break;
		}
		}
		return out;
	}
	//---------------------------------------------------------
	bool JsonValue::parse(String const & text, JsonValue & out)
	{
		Reader reader(text);
		JsonValue value;
		if (!reader.readValue(value, 0))
		{
			return false;
		}
		if (!reader.atEnd())
		{
			return false; // trailing garbage after the value
		}
		out = value;
		return true;
	}
}
