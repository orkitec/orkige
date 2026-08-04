/********************************************************************
	created:	Tuesday 2026/08/04 at 12:00
	filename: 	EditorTestsPanel.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
// EditorTestsPanel.cpp - the "Tests" dockable panel: the open project's Lua
// suite (`<project>/tests/*.test.lua`), the verdicts a run produced, and the
// failures with their file:line.
//
// It decides nothing. The suite, the filter grammar, the result model, the
// failure location and the shape of a run all come from EditorProjectTests.h
// (pure, unit-tested); running one goes through EditorTestSession.h, the ONE
// seam the MCP verbs use as well - so a person watching this panel and an
// agent polling get_project_test_results are looking at the same run.

#include "EditorApp.h"
#include "EditorTestsPanel.h"
#include "EditorProjectTests.h"
#include "EditorTabMenu.h"
#include "EditorTestSession.h"
#include "EditorTheme.h"
#include "IconsFontAwesome6.h"

#include <core_script/ScriptTestTools.h>

#include <imgui_internal.h>	// FindWindowByName + DockId (first-appearance dock)

#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace OrkigeEditor
{
	namespace
	{
		using Orkige::editorErrorTextColor;
		using Orkige::editorWarningTextColor;
		using Orkige::ScriptTestRecord;
		using Orkige::String;

		//! the panel's one-line status, recomputed each frame and kept so the
		//! selfcheck seam can read exactly what a person reads
		std::string & statusLine()
		{
			static std::string sStatus;
			return sStatus;
		}

		//! the row the user last clicked: a test key (`<file>::<name>`) or a
		//! bare file name. "" = nothing selected.
		std::string & selectedRow()
		{
			static std::string sSelected;
			return sSelected;
		}

		//! a pass is green, a refused assertion red, anything else raised
		//! amber - three verdicts, three colours, never two shades of one
		ImVec4 statusColour(ScriptTestRecord const & record)
		{
			if (record.status == "pass")
			{
				return ImVec4(0.44f, 0.78f, 0.44f, 1.0f);
			}
			return (record.status == "fail") ? editorErrorTextColor()
				: editorWarningTextColor();
		}
		//---------------------------------------------------------
		const char* statusGlyph(ScriptTestRecord const & record)
		{
			return (record.status == "pass") ? ICON_FA_CIRCLE_CHECK
				: ICON_FA_CIRCLE_XMARK;
		}
		//---------------------------------------------------------
		//! start a run through the ONE session seam, reporting a refusal where
		//! the user is already looking rather than only in the Console
		void requestRun(EditorState & state, EditorConsole & console,
			ProjectTestRunPlan const & plan)
		{
			String error;
			if (!startProjectTestRun(plan, state.project.getRootDirectory(),
				state.project.getName(), &console, error))
			{
				statusLine() = error;
			}
		}
	}
	//---------------------------------------------------------
	void drawTestsPanel(EditorState & state, ViewSettings & viewSettings,
		EditorConsole & console, bool * visible)
	{
		// dock into the bottom group (beside Console/Stats/Debug) the FIRST
		// time the panel appears, so opening it from the View menu tabs in
		// there rather than floating - the dock-builder reservation only lands
		// on a fresh/reset layout, so a user whose imgui.ini predates this
		// panel would otherwise get a free-floating window.
		for (const char* anchorName : { "Console", "Stats", "Assets###Assets",
			"Debug###Debug" })
		{
			ImGuiWindow* anchor = ImGui::FindWindowByName(anchorName);
			if (anchor && anchor->DockId != 0)
			{
				ImGui::SetNextWindowDockID(anchor->DockId,
					ImGuiCond_FirstUseEver);
				break;
			}
		}
		if (!ImGui::Begin(ICON_FA_FLASK " Tests###Tests", visible))
		{
			ImGui::End();
			return;
		}
		editorPanelTabMenu(visible);

		if (!state.project.isLoaded())
		{
			statusLine() = "Open a project to run its tests.";
			ImGui::TextDisabled("%s", statusLine().c_str());
			ImGui::End();
			return;
		}
		const std::string projectRoot = state.project.getRootDirectory();
		// keep the session pointed at the current project even when the open
		// path did not route through the explicit hook (the same idempotent
		// guard the Source Control panel carries)
		projectTestSessionOnProjectChanged(projectRoot);

		const std::vector<Orkige::ScriptTestFile> files =
			scanProjectTests(projectRoot);
		ProjectTestSessionState const session = projectTestSessionState();
		ProjectTestReport const & report = projectTestSessionReport();
		const bool running = session.state == ProjectTestRunState::Running;

		//--- the action row -----------------------------------
		ImGui::BeginDisabled(running || files.empty());
		if (ImGui::Button(ICON_FA_PLAY " Run All"))
		{
			requestRun(state, console, planAllProjectTests());
		}
		ImGui::EndDisabled();
		ImGui::SameLine();

		// "Run Selected" means whatever the selected row IS: a file row runs
		// the file, a test row runs that one test. One button, because the
		// user already said what they meant by selecting.
		const std::string selected = selectedRow();
		ProjectTestRunPlan selectedPlan;
		if (!selected.empty())
		{
			const std::size_t separator = selected.find("::");
			selectedPlan = (separator == std::string::npos)
				? planProjectTestFile(selected)
				: planProjectTest(selected.substr(0, separator),
					selected.substr(separator + 2));
		}
		ImGui::BeginDisabled(running || selectedPlan.empty());
		if (ImGui::Button(ICON_FA_PLAY " Run Selected"))
		{
			requestRun(state, console, selectedPlan);
		}
		ImGui::EndDisabled();
		ImGui::SameLine();

		const ProjectTestRunPlan rerunPlan =
			planRerunFailedProjectTests(report);
		ImGui::BeginDisabled(running || rerunPlan.empty());
		if (ImGui::Button(ICON_FA_ARROWS_ROTATE " Re-run Failed"))
		{
			requestRun(state, console, rerunPlan);
		}
		ImGui::EndDisabled();
		if (!rerunPlan.empty() && ImGui::IsItemHovered(
			ImGuiHoveredFlags_AllowWhenDisabled))
		{
			ImGui::SetTooltip("%s", ("re-run " + rerunPlan.label +
				" (one run each)").c_str());
		}
		ImGui::SameLine();

		ImGui::BeginDisabled(!running);
		if (ImGui::Button(ICON_FA_STOP " Stop"))
		{
			cancelProjectTestRun(&console);
		}
		ImGui::EndDisabled();

		//--- the filter ---------------------------------------
		// ONE grammar: the same substring the runner matches against
		// `<file>::<test name>` both narrows this list and, through Run
		// Filtered, narrows the run. A list that filtered by a different rule
		// than the run would be a quiet lie about what a button does.
		char filterBuffer[256] = { 0 };
		const std::size_t copied = std::min(viewSettings.testFilter.size(),
			sizeof(filterBuffer) - 1);
		std::copy(viewSettings.testFilter.begin(),
			viewSettings.testFilter.begin() + copied, filterBuffer);
		ImGui::SetNextItemWidth(240.0f);
		if (ImGui::InputTextWithHint("##testfilter",
			"filter (file or test name)", filterBuffer, sizeof(filterBuffer)))
		{
			viewSettings.testFilter = filterBuffer;
			viewSettings.save();
		}
		ImGui::SameLine();
		ImGui::BeginDisabled(running || viewSettings.testFilter.empty());
		if (ImGui::Button(ICON_FA_PLAY " Run Filtered"))
		{
			ProjectTestRunPlan plan;
			plan.filters.push_back(viewSettings.testFilter);
			plan.label = "filter '" + viewSettings.testFilter + "'";
			requestRun(state, console, plan);
		}
		ImGui::EndDisabled();

		//--- the status line ----------------------------------
		const Orkige::ScriptTestSummary tally = report.tally();
		if (running)
		{
			statusLine() = "running " + session.label + " (" +
				std::to_string(session.leg) + " of " +
				std::to_string(session.legCount) + ")";
		}
		else if (!session.runFailure.empty())
		{
			// a runner that never reached a verdict is NOT a failing suite,
			// and the panel says which of the two happened
			statusLine() = session.runFailure;
		}
		else if (session.state == ProjectTestRunState::Cancelled)
		{
			statusLine() = "stopped - " +
				Orkige::ScriptTestReport::summaryText(tally);
		}
		else if (session.state == ProjectTestRunState::Finished)
		{
			statusLine() = Orkige::ScriptTestReport::summaryText(tally);
		}
		else if (files.empty())
		{
			statusLine() = std::string("'") + state.project.getName() +
				"' has no test suite - a project tests itself with '" +
				Orkige::ScriptTestTools::testFileSuffix() + "' files under '" +
				Orkige::ScriptTestTools::testsDirectoryName() + "/'";
		}
		else
		{
			statusLine() = std::to_string(files.size()) +
				" test file(s) - run them to see the tests they declare";
		}
		const bool bad = !session.runFailure.empty() ||
			(!running && session.state != ProjectTestRunState::Idle &&
				tally.exitCode() != 0);
		ImGui::TextColored(bad ? Orkige::editorErrorTextColor()
			: ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled), "%s",
			statusLine().c_str());
		if (!session.runFailure.empty())
		{
			const String tail = projectTestSessionOutputTail();
			if (!tail.empty() && ImGui::TreeNode("what the runner printed"))
			{
				ImGui::TextUnformatted(tail.c_str());
				ImGui::TreePop();
			}
		}
		ImGui::Separator();

		//--- the tree -----------------------------------------
		// records grouped by their file, in the order the run produced them
		std::map<std::string, std::vector<ScriptTestRecord> > byFile;
		for (ScriptTestRecord const & record : report.records)
		{
			byFile[record.file].push_back(record);
		}
		if (!ImGui::BeginChild("##testtree", ImVec2(0.0f, 0.0f),
			ImGuiChildFlags_None))
		{
			ImGui::EndChild();
			ImGui::End();
			return;
		}
		for (Orkige::ScriptTestFile const & file : files)
		{
			std::vector<ScriptTestRecord> const & records =
				byFile[file.resourceName];
			// the file row carries the file's own tally, so a collapsed tree
			// still answers "did this file pass"
			int failed = 0;
			for (ScriptTestRecord const & record : records)
			{
				failed += projectTestPassed(record) ? 0 : 1;
			}
			ImGui::PushID(file.resourceName.c_str());
			ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen |
				ImGuiTreeNodeFlags_SpanAvailWidth;
			if (selected == file.resourceName)
			{
				flags |= ImGuiTreeNodeFlags_Selected;
			}
			std::string label = file.resourceName;
			if (!records.empty())
			{
				label += failed == 0
					? ("  (" + std::to_string(records.size()) + " passed)")
					: ("  (" + std::to_string(failed) + " of " +
						std::to_string(records.size()) + " failed)");
			}
			const bool open = ImGui::TreeNodeEx("##file", flags, "%s",
				label.c_str());
			if (ImGui::IsItemClicked())
			{
				selectedRow() = file.resourceName;
			}
			if (open)
			{
				for (ScriptTestRecord const & record : records)
				{
					if (!Orkige::ScriptTestTools::filterMatches(
						viewSettings.testFilter, record.file, record.name))
					{
						continue;
					}
					const std::string key = projectTestKey(record);
					ImGui::PushID(key.c_str());
					ImGui::TextColored(statusColour(record), "%s",
						statusGlyph(record));
					ImGui::SameLine();
					if (ImGui::Selectable(record.name.empty()
						? "(the file itself)" : record.name.c_str(),
						selected == key))
					{
						selectedRow() = key;
					}
					if (ImGui::IsItemHovered())
					{
						ImGui::SetTooltip("%.3f ms", record.ms);
					}
					if (!projectTestPassed(record))
					{
						ImGui::Indent();
						ImGui::PushStyleColor(ImGuiCol_Text,
							statusColour(record));
						ImGui::TextWrapped("%s", record.message.c_str());
						ImGui::PopStyleColor();
						const ProjectTestLocation location =
							projectTestFailureLocation(record.message);
						if (location.found())
						{
							if (ImGui::SmallButton((location.file + ":" +
								std::to_string(location.line)).c_str()))
							{
								scriptPanelOpenFile(state, viewSettings,
									location.file, location.line);
							}
						}
						ImGui::Unindent();
					}
					ImGui::PopID();
				}
				if (records.empty())
				{
					ImGui::TextDisabled(
						"not run yet - the tests a file declares are only "
						"known once it has run");
				}
				ImGui::TreePop();
			}
			ImGui::PopID();
		}
		ImGui::EndChild();
		ImGui::End();
	}
	//---------------------------------------------------------
}
