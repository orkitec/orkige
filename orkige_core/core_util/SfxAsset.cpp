/**************************************************************
	created:	2026/07/29 at 10:20
	filename: 	SfxAsset.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

//! @file SfxAsset.cpp
//! @brief the `.osfx` text grammar and the standard `.sfs` binary reader
//! (@see SfxAsset.h)

#include "core_util/SfxAsset.h"
#include "core_util/StringUtil.h"

#include <cmath>
#include <cstring>
#include <set>
#include <sstream>
#include <vector>

namespace Orkige
{
	char const * const SfxAsset::TEXT_EXTENSION = ".osfx";
	char const * const SfxAsset::BINARY_EXTENSION = ".sfs";

	namespace
	{
		//! report "line N: what" into the optional error string
		bool fail(String * outError, int line, String const & what)
		{
			if(outError)
			{
				*outError = "line " + std::to_string(line) + ": " + what;
			}
			return false;
		}
		//! report a whole-file problem (the binary reader has no lines)
		bool failFile(String * outError, String const & what)
		{
			if(outError)
			{
				*outError = what;
			}
			return false;
		}

		//! @brief read one finite float
		//! @remarks THE division of labour (@see SfxAsset.h): the parser
		//! rejects what is not a number, and leaves every out-of-RANGE value to
		//! SfxSynth::sanitize, which clamps it and says so.
		bool readFloat(std::istringstream & tokens, float & out)
		{
			if(!(tokens >> out))
			{
				return false;
			}
			return std::isfinite(out);
		}
		//! a directive line must be spent - trailing tokens are an error
		bool lineDone(std::istringstream & tokens)
		{
			String extra;
			return !(tokens >> extra);
		}

		//! one tokenized directive line
		struct Directive
		{
			int		line = 0;		//!< 1-based line number (for the error text)
			String	keyword;		//!< the directive name
			String	values;			//!< everything after the keyword
		};

		//! a short, round-trippable float form: fixed 4 decimals with trailing
		//! zeros (and a bare trailing dot) trimmed - "0.5", "1", "0.2500" ->
		//! "0.25" (the MaterialAsset serializer's form, so the whole text-asset
		//! family writes numbers the same way)
		String shortFloat(float value)
		{
			std::ostringstream out;
			out.setf(std::ios::fixed);
			out.precision(4);
			out << value;
			String text = out.str();
			const std::size_t dot = text.find('.');
			if(dot != String::npos)
			{
				std::size_t last = text.find_last_not_of('0');
				if(last == dot)		// nothing but zeros after the point
				{
					--last;
				}
				text.erase(last + 1);
			}
			return text;
		}

		//! @brief the float directives, as a name -> member table so the parser,
		//! the serializer and the template all walk the SAME list in the SAME
		//! order (the grammar's declaration order)
		struct FloatField
		{
			char const *	name;
			float SfxDesc::*member;
		};
		FloatField const * floatFields(std::size_t & outCount)
		{
			static FloatField const FIELDS[] = {
				{ "soundVolume",		&SfxDesc::soundVolume },
				{ "masterVolume",		&SfxDesc::masterVolume },
				{ "baseFreq",			&SfxDesc::baseFreq },
				{ "freqLimit",			&SfxDesc::freqLimit },
				{ "freqRamp",			&SfxDesc::freqRamp },
				{ "freqDeltaRamp",		&SfxDesc::freqDeltaRamp },
				{ "duty",				&SfxDesc::duty },
				{ "dutyRamp",			&SfxDesc::dutyRamp },
				{ "vibratoStrength",	&SfxDesc::vibratoStrength },
				{ "vibratoSpeed",		&SfxDesc::vibratoSpeed },
				{ "vibratoDelay",		&SfxDesc::vibratoDelay },
				{ "attack",				&SfxDesc::attack },
				{ "sustain",			&SfxDesc::sustain },
				{ "decay",				&SfxDesc::decay },
				{ "punch",				&SfxDesc::punch },
				{ "lpfFreq",			&SfxDesc::lpfFreq },
				{ "lpfRamp",			&SfxDesc::lpfRamp },
				{ "lpfResonance",		&SfxDesc::lpfResonance },
				{ "hpfFreq",			&SfxDesc::hpfFreq },
				{ "hpfRamp",			&SfxDesc::hpfRamp },
				{ "phaserOffset",		&SfxDesc::phaserOffset },
				{ "phaserRamp",			&SfxDesc::phaserRamp },
				{ "repeatSpeed",		&SfxDesc::repeatSpeed },
				{ "arpSpeed",			&SfxDesc::arpSpeed },
				{ "arpMod",				&SfxDesc::arpMod },
			};
			outCount = sizeof(FIELDS) / sizeof(FIELDS[0]);
			return FIELDS;
		}
		//! the field table entry for a directive name (NULL when it names none)
		FloatField const * findFloatField(String const & keyword)
		{
			std::size_t count = 0;
			FloatField const * fields = floatFields(count);
			for(std::size_t i = 0; i < count; ++i)
			{
				if(keyword == fields[i].name)
				{
					return &fields[i];
				}
			}
			return NULL;
		}

		//--- the standard binary parameter file ------------------------------
		//! @brief a little-endian byte cursor over the file's bytes
		//! @remarks Reads explicitly byte by byte rather than memcpy-ing a
		//! float: the format is little-endian on every platform that writes it,
		//! so a big-endian host must assemble the value itself.
		struct ByteReader
		{
			unsigned char const *	bytes;
			std::size_t				size;
			std::size_t				at = 0;
			bool					ok = true;

			ByteReader(void const * data, std::size_t length)
				: bytes(static_cast<unsigned char const *>(data)), size(length)
			{
			}
			//! remaining bytes
			std::size_t left() const
			{
				return (this->at < this->size) ? (this->size - this->at) : 0u;
			}
			unsigned int readU32()
			{
				if(this->left() < 4u)
				{
					this->ok = false;
					return 0u;
				}
				const unsigned int value =
					static_cast<unsigned int>(this->bytes[this->at]) |
					(static_cast<unsigned int>(this->bytes[this->at + 1]) << 8) |
					(static_cast<unsigned int>(this->bytes[this->at + 2]) << 16) |
					(static_cast<unsigned int>(this->bytes[this->at + 3]) << 24);
				this->at += 4u;
				return value;
			}
			int readI32()
			{
				return static_cast<int>(this->readU32());
			}
			//! an IEEE-754 single, little-endian (what every writer of this
			//! format emits)
			float readF32()
			{
				const unsigned int bits = this->readU32();
				if(!this->ok)
				{
					return 0.0f;
				}
				float value = 0.0f;
				std::memcpy(&value, &bits, sizeof(value));
				return std::isfinite(value) ? value : 0.0f;
			}
			//! the format's one-byte boolean
			bool readBool()
			{
				if(this->left() < 1u)
				{
					this->ok = false;
					return false;
				}
				return this->bytes[this->at++] != 0;
			}
		};
	}
	//---------------------------------------------------------
	bool SfxAsset::isTextName(String const & name)
	{
		return StringUtil::to_lower_copy(name).ends_with(
			SfxAsset::TEXT_EXTENSION);
	}
	//---------------------------------------------------------
	bool SfxAsset::isBinaryName(String const & name)
	{
		return StringUtil::to_lower_copy(name).ends_with(
			SfxAsset::BINARY_EXTENSION);
	}
	//---------------------------------------------------------
	bool SfxAsset::isSfxName(String const & name)
	{
		return SfxAsset::isTextName(name) || SfxAsset::isBinaryName(name);
	}
	//---------------------------------------------------------
	bool SfxAsset::parse(String const & text, SfxDesc & out, String * outError)
	{
		if(outError)
		{
			outError->clear();
		}

		// --- tokenize every line first: the `preset` seed has to be applied
		// --- BEFORE any explicit directive regardless of where its line sits
		// --- (the structural precedence promised in SfxAsset.h)
		std::vector<Directive> directives;
		std::set<String> seen;			//!< every directive may appear once
		int lineNumber = 0;
		std::istringstream lines(text);
		String rawLine;
		while(std::getline(lines, rawLine))
		{
			++lineNumber;
			const std::size_t hash = rawLine.find('#');
			if(hash != String::npos)
			{
				rawLine.erase(hash);
			}
			std::istringstream head(rawLine);
			Directive directive;
			directive.line = lineNumber;
			if(!(head >> directive.keyword) || directive.keyword.empty())
			{
				continue;	// blank / comment-only line
			}
			if(!seen.insert(directive.keyword).second)
			{
				return fail(outError, lineNumber,
					"duplicate directive '" + directive.keyword + "'");
			}
			std::getline(head, directive.values);
			directives.push_back(directive);
		}

		if(directives.empty())
		{
			return fail(outError, lineNumber,
				"empty sound (no directives found)");
		}

		// --- pass 1: version + preset (the seed) ---
		SfxDesc parsed;
		bool seeded = false;
		SfxPreset::Archetype archetype = SfxPreset::SA_PICKUP_COIN;
		for(std::size_t i = 0; i < directives.size(); ++i)
		{
			Directive const & directive = directives[i];
			if(directive.keyword == "version")
			{
				std::istringstream tokens(directive.values);
				int version = 0;
				if(!(tokens >> version) || !lineDone(tokens))
				{
					return fail(outError, directive.line,
						"version takes one integer");
				}
				if(version != 1)
				{
					return fail(outError, directive.line,
						"unsupported version " + std::to_string(version) +
						" (this engine reads version 1)");
				}
			}
			else if(directive.keyword == "preset")
			{
				std::istringstream tokens(directive.values);
				String name;
				if(!(tokens >> name) || name.empty() || !lineDone(tokens))
				{
					return fail(outError, directive.line,
						"preset takes one archetype name");
				}
				if(!SfxPreset::parseArchetype(name, archetype))
				{
					return fail(outError, directive.line,
						"unknown preset '" + name + "' (coin, laser, "
						"explosion, powerup, hit, jump, blip)");
				}
				seeded = true;
			}
		}
		// an explicit `seed` has to reach the GENERATOR too (it picks which
		// member of the archetype's family this file names), so it is resolved
		// before the preset is drawn
		for(std::size_t i = 0; i < directives.size(); ++i)
		{
			if(directives[i].keyword != "seed")
			{
				continue;
			}
			std::istringstream tokens(directives[i].values);
			int value = 0;
			if(!(tokens >> value) || value < 0 || !lineDone(tokens))
			{
				return fail(outError, directives[i].line,
					"seed takes one non-negative integer");
			}
			parsed.seed = static_cast<unsigned int>(value);
		}
		if(seeded)
		{
			parsed = SfxPreset::forArchetype(archetype, parsed.seed);
		}

		// --- pass 2: the explicit directives override the seeded fields ---
		for(std::size_t i = 0; i < directives.size(); ++i)
		{
			Directive const & directive = directives[i];
			String const & keyword = directive.keyword;
			const int line = directive.line;
			std::istringstream tokens(directive.values);

			if(keyword == "version" || keyword == "preset" || keyword == "seed")
			{
				continue;	// spent in pass 1
			}
			else if(keyword == "wave")
			{
				String name;
				if(!(tokens >> name) || name.empty() || !lineDone(tokens))
				{
					return fail(outError, line, "wave takes one shape name");
				}
				if(!SfxWave::parseType(name, parsed.waveType))
				{
					return fail(outError, line, "unknown wave '" + name +
						"' (square, saw, sine, noise)");
				}
			}
			else if(keyword == "filter")
			{
				int value = 0;
				if(!(tokens >> value) || (value != 0 && value != 1) ||
					!lineDone(tokens))
				{
					return fail(outError, line, "filter takes 0 or 1");
				}
				parsed.filterOn = (value == 1);
			}
			else if(FloatField const * field = findFloatField(keyword))
			{
				float value = 0.0f;
				if(!readFloat(tokens, value) || !lineDone(tokens))
				{
					return fail(outError, line, keyword +
						" takes one number");
				}
				parsed.*(field->member) = value;
			}
			else
			{
				// no reserved-word tolerance (the `.omat` rule): a typo'd
				// directive silently ignored would ship a wrong sound
				return fail(outError, line,
					"unknown directive '" + keyword + "'");
			}
		}

		out = parsed;
		return true;
	}
	//---------------------------------------------------------
	bool SfxAsset::parseBinary(void const * data, std::size_t size,
		SfxDesc & out, String * outError)
	{
		if(outError)
		{
			outError->clear();
		}
		if(!data || size < 8u)
		{
			return failFile(outError, "not a sound parameter file (too short)");
		}

		ByteReader reader(data, size);
		const int version = reader.readI32();
		if(version != 100 && version != 101 && version != 102)
		{
			return failFile(outError, "unsupported parameter file version " +
				std::to_string(version) + " (this engine reads 100, 101 and "
				"102)");
		}

		SfxDesc parsed;
		const int waveNumber = reader.readI32();
		if(!SfxWave::fromNumber(waveNumber, parsed.waveType))
		{
			return failFile(outError, "the file names wave type " +
				std::to_string(waveNumber) + ", which the format does not "
				"define");
		}
		// the per-sound volume joined the layout in version 102
		if(version >= 102)
		{
			parsed.soundVolume = reader.readF32();
		}
		parsed.baseFreq = reader.readF32();
		parsed.freqLimit = reader.readF32();
		parsed.freqRamp = reader.readF32();
		// the delta slide joined the layout in version 101
		if(version >= 101)
		{
			parsed.freqDeltaRamp = reader.readF32();
		}
		parsed.duty = reader.readF32();
		parsed.dutyRamp = reader.readF32();
		parsed.vibratoStrength = reader.readF32();
		parsed.vibratoSpeed = reader.readF32();
		parsed.vibratoDelay = reader.readF32();
		parsed.attack = reader.readF32();
		parsed.sustain = reader.readF32();
		parsed.decay = reader.readF32();
		parsed.punch = reader.readF32();
		// ONE byte, with no padding after it: the format is written field by
		// field, not as a struct, so every value from here on sits at an
		// offset the byte reader above handles without alignment help
		parsed.filterOn = reader.readBool();
		parsed.lpfResonance = reader.readF32();
		parsed.lpfFreq = reader.readF32();
		parsed.lpfRamp = reader.readF32();
		parsed.hpfFreq = reader.readF32();
		parsed.hpfRamp = reader.readF32();
		parsed.phaserOffset = reader.readF32();
		parsed.phaserRamp = reader.readF32();
		parsed.repeatSpeed = reader.readF32();
		// the arpeggio pair joined the layout in version 101
		if(version >= 101)
		{
			parsed.arpSpeed = reader.readF32();
			parsed.arpMod = reader.readF32();
		}

		if(!reader.ok)
		{
			return failFile(outError, "truncated parameter file (version " +
				std::to_string(version) + " needs more bytes than the " +
				std::to_string(size) + " given)");
		}

		out = parsed;
		return true;
	}
	//---------------------------------------------------------
	String SfxAsset::serialize(SfxDesc const & desc)
	{
		const SfxDesc defaults;
		std::ostringstream out;
		// the version line is always emitted so a hand read shows the grammar
		out << "version 1\n";
		if(desc.waveType != defaults.waveType)
		{
			out << "wave " << SfxWave::typeName(desc.waveType) << "\n";
		}
		std::size_t count = 0;
		FloatField const * fields = floatFields(count);
		for(std::size_t i = 0; i < count; ++i)
		{
			const float value = desc.*(fields[i].member);
			if(value != defaults.*(fields[i].member))
			{
				out << fields[i].name << " " << shortFloat(value) << "\n";
			}
		}
		if(desc.filterOn != defaults.filterOn)
		{
			out << "filter " << (desc.filterOn ? 1 : 0) << "\n";
		}
		if(desc.seed != defaults.seed)
		{
			out << "seed " << desc.seed << "\n";
		}
		return out.str();
	}
	//---------------------------------------------------------
	String SfxAsset::templateFor(SfxPreset::Archetype archetype,
		unsigned int seed)
	{
		const SfxDesc desc = SfxPreset::forArchetype(archetype, seed);
		std::ostringstream out;
		out << "# " << SfxPreset::archetypeName(archetype)
			<< " - a procedural sound effect. Every line below is one standard\n"
			<< "# sound parameter, spelled out at the value this archetype drew:\n"
			<< "# tune the numbers, save, and audition it from the asset "
			<< "browser.\n# The `preset` line seeds any parameter a line omits.\n";
		out << "version 1\n";
		out << "preset " << SfxPreset::archetypeName(archetype) << "\n";
		if(seed != SfxDesc().seed)
		{
			out << "seed " << seed << "\n";
		}
		out << "wave " << SfxWave::typeName(desc.waveType) << "\n";
		// spell EVERY parameter out (not just the ones that differ from the
		// defaults): the file is a designer's control panel, and a value they
		// cannot see is a value they cannot tune
		std::size_t count = 0;
		FloatField const * fields = floatFields(count);
		for(std::size_t i = 0; i < count; ++i)
		{
			out << fields[i].name << " "
				<< shortFloat(desc.*(fields[i].member)) << "\n";
		}
		return out.str();
	}
}
