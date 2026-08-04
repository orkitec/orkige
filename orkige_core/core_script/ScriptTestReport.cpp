/**************************************************************
	created:	2026/08/03 at 16:00
	filename: 	ScriptTestReport.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "core_script/ScriptTestReport.h"
#include "core_debugnet/Json.h"

#include <cmath>
#include <string>

namespace Orkige
{
	namespace
	{
		//! milliseconds rounded to microsecond resolution: the artifact carries
		//! a number a reader can compare, not a full binary64 tail
		double roundedMs(double value)
		{
			return std::floor(value * 1000.0 + 0.5) / 1000.0;
		}
	}
	//---------------------------------------------------------
	String ScriptTestReport::metaLine(String const & project, String const & utc,
		String const & filter, int files)
	{
		JsonValue line = JsonValue::object();
		line.set("record", JsonValue("meta"));
		line.set("project", JsonValue(project));
		if(!utc.empty())
		{
			line.set("utc", JsonValue(utc));
		}
		line.set("filter", JsonValue(filter));
		line.set("files", JsonValue(files));
		return line.serialize();
	}
	//---------------------------------------------------------
	String ScriptTestReport::testLine(ScriptTestRecord const & record)
	{
		JsonValue line = JsonValue::object();
		line.set("record", JsonValue("test"));
		line.set("file", JsonValue(record.file));
		line.set("name", JsonValue(record.name));
		line.set("status", JsonValue(record.status));
		line.set("message", JsonValue(record.message));
		line.set("ms", JsonValue(roundedMs(record.ms)));
		return line.serialize();
	}
	//---------------------------------------------------------
	String ScriptTestReport::summaryLine(ScriptTestSummary const & summary)
	{
		JsonValue line = JsonValue::object();
		line.set("record", JsonValue("summary"));
		line.set("files", JsonValue(summary.files));
		line.set("total", JsonValue(summary.total));
		line.set("passed", JsonValue(summary.passed));
		line.set("failed", JsonValue(summary.failed));
		line.set("errors", JsonValue(summary.errors));
		line.set("filtered", JsonValue(summary.filtered));
		line.set("ms", JsonValue(roundedMs(summary.ms)));
		line.set("exitCode", JsonValue(summary.exitCode()));
		return line.serialize();
	}
	//---------------------------------------------------------
	String ScriptTestReport::summaryText(ScriptTestSummary const & summary)
	{
		return std::to_string(summary.passed) + " passed, " +
			std::to_string(summary.failed) + " failed, " +
			std::to_string(summary.errors) + " errors (" +
			std::to_string(summary.total) + " of " +
			std::to_string(summary.total + summary.filtered) +
			" tests in " + std::to_string(summary.files) + " file(s))";
	}
	//---------------------------------------------------------
}
