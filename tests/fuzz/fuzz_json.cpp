/********************************************************************
	created:	Monday 2026/07/21 at 10:00
	filename: 	fuzz_json.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

//! @file fuzz_json.cpp
//! @brief libFuzzer entry point over the hand-rolled JSON reader
//! (JsonValue::parse) that backs the editor's MCP / JSON-RPC endpoint - the
//! parser that consumes bytes off a network socket. On a successful parse the
//! value is serialized and re-parsed so the writer is fuzzed alongside the
//! reader (@see Docs/fuzzing.md).

#include "core_debugnet/Json.h"

#include <cstddef>
#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t * data, size_t size)
{
	Orkige::String text(reinterpret_cast<char const *>(data), size);

	Orkige::JsonValue value;
	if(Orkige::JsonValue::parse(text, value))
	{
		Orkige::String const round = value.serialize();
		Orkige::JsonValue again;
		Orkige::JsonValue::parse(round, again);
	}
	return 0;
}
