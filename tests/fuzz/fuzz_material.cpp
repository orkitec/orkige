/********************************************************************
	created:	Monday 2026/07/21 at 10:00
	filename: 	fuzz_material.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

//! @file fuzz_material.cpp
//! @brief libFuzzer entry point over the `.omat` text parser
//! (MaterialAsset::parse). Feeds arbitrary bytes at the parser and, on a
//! well-formed material, exercises the serialize/re-parse inverse so both
//! directions are covered. The parser promises "never a crash" - this is the
//! machine that tries to break that promise (@see Docs/fuzzing.md).

#include "core_util/MaterialAsset.h"

#include <cstddef>
#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t * data, size_t size)
{
	Orkige::String text(reinterpret_cast<char const *>(data), size);

	Orkige::MaterialAsset::ParsedMaterial parsed;
	Orkige::String error;
	if(Orkige::MaterialAsset::parse(text, parsed, &error))
	{
		// a well-formed material must survive the documented round-trip; run
		// serialize + re-parse so the inverse path is fuzzed too
		Orkige::String const round = Orkige::MaterialAsset::serialize(parsed);
		Orkige::MaterialAsset::ParsedMaterial again;
		Orkige::MaterialAsset::parse(round, again, nullptr);
	}
	return 0;
}
