/********************************************************************
	created:	Monday 2026/08/03 at 12:00
	filename: 	EditorProjectSettings.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
// EditorProjectSettings.cpp - the Project Settings window: the manifest
// Settings that describe the app, and the credentials that sign it, kept in
// two visibly different places because they belong in two different places.
//
// The window is the surface over EditorBuildSettings.h, which owns every
// decision it renders (the platform x purpose matrix, the committed manifest
// vocabulary, the machine store and its keys). What lives here is the drawing
// and the readiness reporting - and the readiness comes from the exporter's
// OWN gate functions, so what the window promises and what an export refuses
// can never drift apart.
#include "EditorApp.h"
#include "EditorBuildSettings.h"
#include <core_util/HelpLink.h>

#include <ExportAndroid.h>
#include <ExportRun.h>
#include <ExportSettings.h>

#include <algorithm>
#include <cfloat>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace
{
	using OrkigeEditor::BuildCellState;
	using OrkigeEditor::BuildCredentialSlot;
	using OrkigeEditor::BuildCredentialStorage;
	using OrkigeEditor::BuildPurpose;
	using OrkigeEditor::BuildSettingMap;
	using OrkigeEditor::BuildTargetCell;
	using OrkigeEditor::ProjectSettingKind;
	using OrkigeEditor::ProjectSettingRow;

	//! the window's own state: the machine settings it is editing, loaded once
	//! per project rather than re-read every frame
	struct ProjectSettingsUi
	{
		//! the project the map below belongs to ("" = nothing loaded yet)
		std::string		loadedFor;
		BuildSettingMap	machine;
		std::string		machinePath;
		//! the last write's refusal, shown in place ("" = fine)
		std::string		storeError;
	};
	ProjectSettingsUi gUi;

	//! @brief (re)load the machine settings when the window is showing a
	//! different project than the one they were read for
	void syncMachineSettings(std::string const& projectRoot)
	{
		if (gUi.loadedFor == projectRoot)
		{
			return;
		}
		gUi.loadedFor = projectRoot;
		gUi.machine = OrkigeEditor::loadBuildSettings(projectRoot);
		gUi.machinePath = OrkigeEditor::buildSettingsPath(projectRoot);
		gUi.storeError.clear();
	}

	//! @brief persist the machine settings after an edit. A failure is shown
	//! in the window rather than swallowed - a credential a person believes
	//! they set and that did not survive is worse than an error.
	void persistMachineSettings(std::string const& projectRoot)
	{
		Orkige::String error;
		if (!OrkigeEditor::saveBuildSettings(projectRoot, gUi.machine, &error))
		{
			gUi.storeError = error;
			oDebugError("editor.project", 0, "build credentials could not be "
				"saved - " << error);
			return;
		}
		gUi.storeError.clear();
	}

	//! @brief write one manifest Setting and save the .orkproj
	void persistProjectSetting(EditorState& state, Orkige::String const& key,
		Orkige::String const& value)
	{
		// the committed group and ONLY the committed group reaches the
		// manifest; the model draws the line and this is where it is obeyed
		if (!OrkigeEditor::isProjectSettingKey(key))
		{
			oDebugError("editor.project", 0, "'" << key << "' is not a "
				"project setting - refusing to write it into the manifest");
			return;
		}
		state.project.setSetting(key, value);
		Orkige::String saveError;
		if (!state.project.save(&saveError))
		{
			oDebugError("editor.project", 0, "Project Settings: could not "
				"save the manifest - " << saveError);
		}
	}

	//! @brief the label column of one form row, with its explanation on hover
	void drawRowLabel(std::string const& label, std::string const& hint)
	{
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(label.c_str());
		if (!hint.empty())
		{
			ImGui::SetItemTooltip("%s", hint.c_str());
		}
		ImGui::TableSetColumnIndex(1);
		ImGui::SetNextItemWidth(-FLT_MIN);
	}

	//! @brief a "more in <doc>" line for a cell, opening the published page
	void drawHelpLine(Orkige::String const& helpPage, const char* idSuffix)
	{
		const Orkige::String url = Orkige::helpUrl(helpPage);
		ImGui::TextDisabled("%s", url.c_str());
		ImGui::SameLine();
		const std::string buttonId = std::string("Open##help") + idSuffix;
		if (ImGui::SmallButton(buttonId.c_str()) && !gAutomatedRun)
		{
			SDL_OpenURL(url.c_str());
		}
	}

	//--- the committed group ---------------------------------

	//! @brief the manifest Settings for one platform group ("" = every
	//! platform). Each committed edit saves the .orkproj immediately, the way
	//! the orientation row always has.
	void drawProjectSettingGroup(EditorState& state,
		Orkige::String const& platform)
	{
		Orkige::pushPropertyGridStyle();
		const std::string tableId = "##projectsettings_" +
			(platform.empty() ? std::string("general") : std::string(platform));
		if (ImGui::BeginTable(tableId.c_str(), 2,
			ImGuiTableFlags_SizingStretchProp))
		{
			ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthStretch,
				0.34f);
			ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch,
				0.66f);
			for (ProjectSettingRow const& row : OrkigeEditor::projectSettingRows())
			{
				if (row.platform != platform)
				{
					continue;
				}
				drawRowLabel(row.label, row.hint);
				const Orkige::String current =
					state.project.getSetting(row.key, row.defaultValue);
				const std::string widgetId = "##" + std::string(row.key);
				if (row.kind == ProjectSettingKind::Choice)
				{
					int index = 0;
					for (std::size_t i = 0; i < row.choices.size(); ++i)
					{
						if (row.choices[i] == current)
						{
							index = static_cast<int>(i);
							break;
						}
					}
					std::vector<const char*> labels;
					labels.reserve(row.choices.size());
					for (Orkige::String const& choice : row.choices)
					{
						labels.push_back(choice.c_str());
					}
					if (ImGui::Combo(widgetId.c_str(), &index, labels.data(),
						static_cast<int>(labels.size())))
					{
						persistProjectSetting(state, row.key,
							row.choices[static_cast<std::size_t>(index)]);
					}
				}
				else
				{
					char buffer[512];
					SDL_strlcpy(buffer, current.c_str(), sizeof(buffer));
					const ImGuiInputTextFlags flags =
						row.kind == ProjectSettingKind::Integer
							? ImGuiInputTextFlags_CharsDecimal : 0;
					if (ImGui::InputTextWithHint(widgetId.c_str(),
						row.defaultValue.empty() ? "(derived)"
							: row.defaultValue.c_str(),
						buffer, sizeof(buffer), flags))
					{
						persistProjectSetting(state, row.key, buffer);
					}
				}
			}
			ImGui::EndTable();
		}
		Orkige::popPropertyGridStyle();
	}

	void drawProjectTab(EditorState& state)
	{
		ImGui::TextWrapped("These describe the app and live in the project "
			"manifest, so they are committed and everyone working on this "
			"project gets the same ones.");
		ImGui::Separator();
		drawProjectSettingGroup(state, "");
		for (Orkige::String const& platform : OrkigeEditor::buildPlatformOrder())
		{
			bool any = false;
			for (ProjectSettingRow const& row :
				OrkigeEditor::projectSettingRows())
			{
				if (row.platform == platform) { any = true; break; }
			}
			if (!any)
			{
				continue;
			}
			ImGui::Spacing();
			ImGui::TextDisabled("%s",
				OrkigeEditor::buildPlatformLabel(platform).c_str());
			drawProjectSettingGroup(state, platform);
		}
	}

	//--- the machine group -----------------------------------

	//! @brief does @p path name a file that is actually there? (a path
	//! credential that points at nothing fails at export time otherwise, with
	//! the failure arriving minutes later)
	bool pathExists(Orkige::String const& path)
	{
		if (path.empty())
		{
			return false;
		}
		std::error_code ignored;
		return std::filesystem::exists(path.c_str(), ignored);
	}

	//! @brief is the environment holding @p variable?
	bool environmentHas(Orkige::String const& variable)
	{
		if (variable.empty())
		{
			return false;
		}
		const char* const value = std::getenv(variable.c_str());
		return value != nullptr && value[0] != '\0';
	}

	//! @brief one credential row. A Machine slot is editable and saved on
	//! commit; a Secret slot is a STATUS, never a field - the editor stores no
	//! password, so there is nothing here to type into.
	void drawCredentialSlot(std::string const& projectRoot,
		BuildCredentialSlot const& slot, bool enabled)
	{
		drawRowLabel(slot.label, slot.hint);
		if (slot.storage == BuildCredentialStorage::Secret)
		{
			const bool present = environmentHas(slot.environmentVariable);
			ImGui::BeginDisabled();
			ImGui::TextUnformatted(present ? "set in the environment"
				: "not set");
			ImGui::EndDisabled();
			if (!slot.environmentVariable.empty())
			{
				ImGui::SetItemTooltip("passwords are never stored by the "
					"editor. Export %s in the shell that runs the build; the "
					"signing step reads it from the environment, so it never "
					"reaches a command line or a file.",
					slot.environmentVariable.c_str());
			}
			return;
		}
		ImGui::BeginDisabled(!enabled);
		char buffer[1024];
		const BuildSettingMap::const_iterator found =
			gUi.machine.find(slot.key);
		SDL_strlcpy(buffer,
			found != gUi.machine.end() ? found->second.c_str() : "",
			sizeof(buffer));
		const std::string widgetId = "##credential_" +
			(slot.key.empty() ? slot.label : slot.key);
		const bool inherited = enabled &&
			(found == gUi.machine.end() || found->second.empty()) &&
			environmentHas(slot.environmentVariable);
		if (ImGui::InputTextWithHint(widgetId.c_str(),
			inherited ? "(from the environment)" : "", buffer, sizeof(buffer))
			&& enabled)
		{
			gUi.machine[slot.key] = buffer;
			persistMachineSettings(projectRoot);
		}
		ImGui::EndDisabled();
		if (!slot.environmentVariable.empty())
		{
			ImGui::SetItemTooltip("kept on this machine only. Empty falls "
				"back to %s.", slot.environmentVariable.c_str());
		}
		if (enabled && slot.isPath && buffer[0] != '\0' &&
			!pathExists(buffer))
		{
			ImGui::PushStyleColor(ImGuiCol_Text,
				Orkige::editorWarningTextColor());
			ImGui::TextWrapped("no file at this path");
			ImGui::PopStyleColor();
		}
	}

	//! @brief what a signed Android release still needs, in the exporter's own
	//! words - asked of the exporter's own gate so the window and the refusal
	//! can never disagree
	std::vector<Orkige::String> androidReleaseGaps()
	{
		OrkigeExport::EnvironmentMap environment =
			OrkigeExport::currentEnvironment();
		const OrkigeEditor::BuildCredentials credentials =
			OrkigeEditor::buildCredentialsFrom(gUi.machine);
		const OrkigeExport::AndroidKeystore keystore =
			OrkigeExport::resolveAndroidKeystore(credentials.androidKeystore,
				credentials.androidKeyAlias, environment);
		const Orkige::String bundletool = OrkigeExport::resolveBundletool(
			credentials.bundletool, environment, 0);
		return OrkigeExport::androidSigningGaps(keystore, bundletool);
	}

	//! @brief the readiness line under an Applied cell: green when the export
	//! could run, otherwise one sentence per missing piece
	void drawCellReadiness(BuildTargetCell const& cell)
	{
		std::vector<Orkige::String> missing;
		if (cell.platform == "android")
		{
			missing = androidReleaseGaps();
		}
		else if (cell.platform == "ios")
		{
			const OrkigeExport::EnvironmentMap environment =
				OrkigeExport::currentEnvironment();
			const OrkigeEditor::BuildCredentials credentials =
				OrkigeEditor::buildCredentialsFrom(gUi.machine);
			const OrkigeExport::SigningPair pair =
				cell.purpose == BuildPurpose::Development
					? OrkigeExport::resolveIosSigning(credentials.iosIdentity,
						credentials.iosProfile, environment)
					: OrkigeExport::resolveIosDistributionSigning(
						credentials.iosDistributionIdentity,
						credentials.iosDistributionProfile, environment);
			if (pair.identity.empty())
			{
				missing.push_back("a signing identity");
			}
			if (pair.profile.empty())
			{
				missing.push_back("a provisioning profile");
			}
		}
		if (missing.empty())
		{
			ImGui::TextDisabled("Ready.");
			return;
		}
		ImGui::PushStyleColor(ImGuiCol_Text, Orkige::editorWarningTextColor());
		Orkige::String text = "Not ready - this needs ";
		for (std::size_t index = 0; index < missing.size(); ++index)
		{
			if (index > 0)
			{
				text += index + 1 == missing.size() ? " and " : ", ";
			}
			text += missing[index];
		}
		text += ". Set it above, or in the environment.";
		ImGui::TextWrapped("%s", text.c_str());
		ImGui::PopStyleColor();
	}

	void drawCell(std::string const& projectRoot, BuildTargetCell const& cell,
		int cellIndex)
	{
		ImGui::PushID(cellIndex);
		ImGui::SeparatorText((OrkigeEditor::buildPurposeLabel(cell.purpose) +
			" - " + cell.label).c_str());
		if (!cell.note.empty())
		{
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(
				ImGuiCol_TextDisabled));
			ImGui::TextWrapped("%s", cell.note.c_str());
			ImGui::PopStyleColor();
		}
		if (!cell.slots.empty())
		{
			const bool enabled = cell.state == BuildCellState::Applied;
			Orkige::pushPropertyGridStyle();
			if (ImGui::BeginTable("##credentials", 2,
				ImGuiTableFlags_SizingStretchProp))
			{
				ImGui::TableSetupColumn("label",
					ImGuiTableColumnFlags_WidthStretch, 0.34f);
				ImGui::TableSetupColumn("value",
					ImGuiTableColumnFlags_WidthStretch, 0.66f);
				for (BuildCredentialSlot const& slot : cell.slots)
				{
					drawCredentialSlot(projectRoot, slot, enabled);
				}
				ImGui::EndTable();
			}
			Orkige::popPropertyGridStyle();
			if (enabled)
			{
				drawCellReadiness(cell);
			}
		}
		drawHelpLine(cell.helpPage, "cell");
		ImGui::PopID();
	}

	void drawSigningTab(EditorState& state)
	{
		const std::string projectRoot = state.project.getRootDirectory();
		syncMachineSettings(projectRoot);
		ImGui::TextWrapped("These prove who is shipping the game. They are "
			"yours, not the project's, so they are kept on this machine only "
			"and are never written into the project or committed.");
		if (gUi.machinePath.empty())
		{
			ImGui::PushStyleColor(ImGuiCol_Text,
				Orkige::editorWarningTextColor());
			ImGui::TextWrapped("This machine has no per-user application "
				"directory, so nothing can be kept here - set the environment "
				"variables instead.");
			ImGui::PopStyleColor();
		}
		else
		{
			ImGui::TextDisabled("Kept in %s", gUi.machinePath.c_str());
			ImGui::SetItemTooltip("one file per project, readable by you "
				"alone, outside every project directory");
		}
		if (!gUi.storeError.empty())
		{
			ImGui::PushStyleColor(ImGuiCol_Text,
				Orkige::editorErrorTextColor());
			ImGui::TextWrapped("%s", gUi.storeError.c_str());
			ImGui::PopStyleColor();
		}
		ImGui::Separator();
		if (!ImGui::BeginTabBar("##signingplatforms"))
		{
			return;
		}
		const std::vector<BuildTargetCell> cells =
			OrkigeEditor::buildTargetMatrix();
		for (Orkige::String const& platform : OrkigeEditor::buildPlatformOrder())
		{
			if (!ImGui::BeginTabItem(
				OrkigeEditor::buildPlatformLabel(platform).c_str()))
			{
				continue;
			}
			int cellIndex = 0;
			for (BuildTargetCell const& cell : cells)
			{
				if (cell.platform == platform)
				{
					drawCell(projectRoot, cell, cellIndex);
				}
				++cellIndex;
			}
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}
}

// the Project Settings window: the two groups of build settings, side by side
// and deliberately unlike each other. The Project tab writes manifest
// `export.*` Settings, which are committed. The Signing tab writes a per-
// project file under the editor's own state directory, which is not - and a
// password writes nowhere at all (@see EditorBuildSettings.h). A no-op with no
// project open.
void drawProjectSettingsWindow(EditorState& state)
{
	if (!state.showProjectSettingsWindow)
	{
		return;
	}
	ImGui::SetNextWindowSize(ImVec2(560.0f, 0.0f), ImGuiCond_FirstUseEver);
	if (ImGui::Begin("Project Settings", &state.showProjectSettingsWindow,
		ImGuiWindowFlags_AlwaysAutoResize))
	{
		if (!state.project.isLoaded())
		{
			ImGui::TextUnformatted("Open a project to edit its settings.");
		}
		else if (ImGui::BeginTabBar("##projectsettings"))
		{
			if (ImGui::BeginTabItem("Project"))
			{
				drawProjectTab(state);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Signing"))
			{
				drawSigningTab(state);
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}
	}
	ImGui::End();
}

//---------------------------------------------------------
OrkigeExport::ExportRequest& applyBuildCredentials(
	OrkigeExport::ExportRequest& request, Orkige::String const& projectRoot)
{
	// the ONE hand-over: what this project's machine settings hold becomes the
	// export request's own credential fields, which the exporter treats
	// exactly like the CLI arguments they mirror - an explicit value wins over
	// the environment, an empty one falls through to it.
	const OrkigeEditor::BuildCredentials credentials =
		OrkigeEditor::buildCredentialsFrom(
			OrkigeEditor::loadBuildSettings(projectRoot));
	request.signingIdentity = credentials.iosIdentity;
	request.provisioningProfile = credentials.iosProfile;
	request.distributionIdentity = credentials.iosDistributionIdentity;
	request.distributionProfile = credentials.iosDistributionProfile;
	request.androidKeystore = credentials.androidKeystore;
	request.androidKeyAlias = credentials.androidKeyAlias;
	request.bundletool = credentials.bundletool;
	return request;
}
