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
#include "EditorSecretStore.h"
#include <core_util/HelpLink.h>

#include <ExportAndroid.h>
#include <ExportRun.h>
#include <ExportSettings.h>

#include <algorithm>
#include <cfloat>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <string>
#include <system_error>
#include <utility>
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
		//! what is being TYPED into a password field right now, per vault key.
		//! Wiped the moment it reaches the credential store and again on a
		//! project switch - a password has no reason to outlive the keystroke
		//! that sends it, and this is the only buffer one ever sits in.
		std::map<std::string, std::vector<char>>	secretDraft;
		//! where each password comes from, resolved ONCE per project rather
		//! than per frame: this window redraws continuously and a credential
		//! store is not something to interrogate sixty times a second.
		//! Invalidated whenever this editor changes one.
		std::map<std::string, OrkigeEditor::SecretState>	secretState;
	};
	ProjectSettingsUi gUi;

	//! @brief zero every password buffer in place
	void scrubSecretDrafts()
	{
		for (std::pair<const std::string, std::vector<char>>& draft :
			gUi.secretDraft)
		{
			std::fill(draft.second.begin(), draft.second.end(), '\0');
		}
		gUi.secretDraft.clear();
	}

	//! @brief where @p slot's password comes from (cached, @see
	//! ProjectSettingsUi::secretState). The RESOLUTION is
	//! EditorSecretStore.h's - this only decides how often to ask.
	OrkigeEditor::SecretState const& secretStateFor(
		std::string const& projectRoot, BuildCredentialSlot const& slot)
	{
		// a slot the editor stores nothing for has no vault key, so the label
		// is what keeps those apart
		const std::string cacheKey = slot.vaultKey.empty() ? slot.label
			: slot.vaultKey;
		const std::map<std::string, OrkigeEditor::SecretState>::iterator found =
			gUi.secretState.find(cacheKey);
		if (found != gUi.secretState.end())
		{
			return found->second;
		}
		return gUi.secretState[cacheKey] =
			OrkigeEditor::resolveSecret(slot, projectRoot);
	}

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
		gUi.secretState.clear();
		scrubSecretDrafts();
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

	//! @brief one PASSWORD row. There is never a plaintext home for what is
	//! typed here: it goes straight to the platform's credential store and the
	//! editor keeps only the key (@see EditorSecretStore.h). The row is a
	//! status - never an editable field - whenever the value is not this
	//! editor's to keep: the environment already holds one (it wins), the cell
	//! is not applied, or this machine has no vault.
	void drawSecretSlot(std::string const& projectRoot,
		BuildCredentialSlot const& slot, bool enabled)
	{
		const OrkigeEditor::SecretState secret =
			secretStateFor(projectRoot, slot);
		const bool editable = enabled && secret.storable &&
			(secret.source == OrkigeEditor::SecretSource::Missing ||
			 secret.source == OrkigeEditor::SecretSource::Vault);
		if (!editable)
		{
			const char* status = "not set";
			if (secret.source == OrkigeEditor::SecretSource::Environment)
			{
				status = "set in the environment";
			}
			else if (secret.source == OrkigeEditor::SecretSource::Unreadable)
			{
				status = "unreadable";
			}
			ImGui::BeginDisabled();
			ImGui::TextUnformatted(status);
			ImGui::EndDisabled();
			ImGui::SetItemTooltip("%s", secret.sentence.c_str());
			return;
		}
		if (secret.source == OrkigeEditor::SecretSource::Vault)
		{
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(("kept in your " + secret.vaultName).c_str());
			ImGui::SetItemTooltip("%s", secret.sentence.c_str());
			ImGui::SameLine();
			if (ImGui::SmallButton("Forget"))
			{
				Orkige::String error;
				if (!OrkigeEditor::forgetSecret(slot, projectRoot, &error))
				{
					gUi.storeError = error;
				}
				gUi.secretState.clear();	// this editor changed it: ask again
			}
			ImGui::SetItemTooltip("remove this password from your %s. The "
				"build then needs %s in its environment again.",
				secret.vaultName.c_str(), slot.environmentVariable.c_str());
			return;
		}
		// nothing set anywhere: the ONE place a password is typed, and it
		// leaves this buffer for the vault without ever passing a file
		std::vector<char>& draft = gUi.secretDraft[slot.vaultKey];
		if (draft.empty())
		{
			draft.assign(256, '\0');
		}
		const std::string widgetId = "##secret_" + std::string(slot.vaultKey);
		ImGui::SetNextItemWidth(-70.0f);
		const bool entered = ImGui::InputTextWithHint(widgetId.c_str(),
			"(not set)", draft.data(), draft.size(),
			ImGuiInputTextFlags_Password |
			ImGuiInputTextFlags_EnterReturnsTrue);
		ImGui::SetItemTooltip("%s", secret.sentence.c_str());
		ImGui::SameLine();
		const bool stored = ImGui::SmallButton("Keep");
		ImGui::SetItemTooltip("keep this password in your %s, for this "
			"project only. It is never written into a file.",
			secret.vaultName.c_str());
		if (entered || stored)
		{
			Orkige::String value(draft.data());
			Orkige::String error;
			if (!OrkigeEditor::storeSecret(slot, projectRoot, value, &error))
			{
				gUi.storeError = error;
			}
			OrkigeEditor::scrubSecret(value);
			// the typed bytes do not linger in a UI buffer for the session
			std::fill(draft.begin(), draft.end(), '\0');
			gUi.secretState.clear();	// this editor changed it: ask again
		}
	}

	//! @brief one credential row: a Machine slot is an editable name or path
	//! saved into this project's machine-settings file, a Secret slot is a
	//! password and goes nowhere near it (@see drawSecretSlot).
	void drawCredentialSlot(std::string const& projectRoot,
		BuildCredentialSlot const& slot, bool enabled)
	{
		drawRowLabel(slot.label, slot.hint);
		if (slot.storage == BuildCredentialStorage::Secret)
		{
			drawSecretSlot(projectRoot, slot, enabled);
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

	//! @brief the honest note under a cell whose password this editor keeps.
	//! The readiness above it is the EXPORTER'S gate, which reads the
	//! environment - so a stored password does not turn a shell build ready,
	//! and saying so here is the difference between a convenience and a lie.
	//! The editor deliberately does not push a stored secret into its own
	//! process environment: everything it launches - the embedded terminal
	//! included - would inherit it.
	void drawStoredSecretNote(std::string const& projectRoot,
		BuildTargetCell const& cell)
	{
		for (BuildCredentialSlot const& slot : cell.slots)
		{
			if (slot.storage != BuildCredentialStorage::Secret)
			{
				continue;
			}
			const OrkigeEditor::SecretState secret =
				secretStateFor(projectRoot, slot);
			if (secret.source != OrkigeEditor::SecretSource::Vault)
			{
				continue;
			}
			ImGui::TextDisabled("%s: kept in your %s. A build started from a "
				"shell reads %s from that shell.", slot.label.c_str(),
				secret.vaultName.c_str(), slot.environmentVariable.c_str());
			ImGui::SetItemTooltip("The editor does not put a stored password "
				"into its own environment - every process it launches, the "
				"embedded terminal included, would inherit it.");
		}
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
				drawStoredSecretNote(projectRoot, cell);
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
		// ...and passwords are not in that file, in any case
		if (OrkigeEditor::SecretVault const* const vault =
			OrkigeEditor::secretVault())
		{
			ImGui::TextDisabled("Passwords go to your %s instead - never into "
				"a file.", vault->name().c_str());
			ImGui::SetItemTooltip("one entry per project and per password, "
				"under %s. An environment variable still wins over it.",
				OrkigeEditor::secretVaultService().c_str());
		}
		else
		{
			ImGui::TextDisabled("Passwords are not kept here at all - this "
				"machine has no credential store the editor can use.");
			ImGui::SetItemTooltip("a password would only ever go to the "
				"platform's own credential store, never to a file. Where "
				"there is none, the environment is the way.");
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
	// the macOS Developer ID names travel the same way - what they DO is still
	// decided by the request's signing flags, which only the command line sets
	// (a Build menu export signs ad-hoc, @see ExportMacosSign.h)
	request.macosSigning.identity = credentials.macosIdentity;
	request.macosSigning.notaryKey = credentials.notaryKey;
	request.macosSigning.notaryKeyId = credentials.notaryKeyId;
	request.macosSigning.notaryIssuer = credentials.notaryIssuer;
	request.macosSigning.notaryAppleId = credentials.notaryAppleId;
	request.macosSigning.notaryTeamId = credentials.notaryTeamId;
	// ...and the Windows Authenticode names, under exactly the same rule: what
	// they DO is decided by the request's signing flag, which only the command
	// line sets (a Build menu export is unsigned, @see ExportWindowsSign.h)
	request.windowsSigning.certificate = credentials.windowsCertificate;
	request.windowsSigning.thumbprint = credentials.windowsThumbprint;
	request.windowsSigning.timestampUrl = credentials.windowsTimestampUrl;
	return request;
}
