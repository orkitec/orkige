/********************************************************************
	created:	Monday 2026/07/21 at 10:00
	filename: 	fuzz_vectoranim.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

//! @file fuzz_vectoranim.cpp
//! @brief libFuzzer entry point over the `.oanim` keyframed vector-animation
//! parser (VectorAnimAsset::parse). The grammar carries nested layer/shape/key
//! runs with topology-agreement rules, so it is a rich target for malformed
//! structure (@see Docs/fuzzing.md).

#include "core_util/VectorAnimAsset.h"

#include <cstddef>
#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t * data, size_t size)
{
	Orkige::String text(reinterpret_cast<char const *>(data), size);

	Orkige::VectorAnimAsset::Document document;
	Orkige::VectorAnimAsset::ParseError error;
	Orkige::VectorAnimAsset::parse(text, document, &error);
	return 0;
}
