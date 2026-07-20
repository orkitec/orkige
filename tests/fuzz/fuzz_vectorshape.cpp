/********************************************************************
	created:	Monday 2026/07/21 at 10:00
	filename: 	fuzz_vectorshape.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

//! @file fuzz_vectorshape.cpp
//! @brief libFuzzer entry point over the `.oshape` tessellated-shape parser
//! (VectorShapeAsset::parse). Exercises the full form (base pose + morph
//! targets) so the contour/hole/mask/stroke/texture vocabulary is all reached
//! (@see Docs/fuzzing.md).

#include "core_util/VectorShapeAsset.h"

#include <cstddef>
#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t * data, size_t size)
{
	Orkige::String text(reinterpret_cast<char const *>(data), size);

	Orkige::VectorShapeAsset::ParsedShape parsed;
	Orkige::VectorShapeAsset::parse(text, parsed);
	return 0;
}
