/********************************************************************
	created:	Saturday 2026/07/12 at 12:00
	filename: 	EditorGuiPreviewPanel.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

//! @file EditorGuiPreviewPanel.cpp
//! @brief the editor's GUI Preview tab: pick a project `.oui` screen and a
//! simulated device context (resolution / content scale / safe-area notch)
//! and see the REAL gui rendered into an offscreen target here. Watches the
//! previewed file's mtime and reloads on change, so an agent that edits the
//! `.oui` over MCP (write_project_file) is reflected live. Shares its
//! GuiPreviewStage with the preview_ui MCP verb - the collaborative loop.

#include "EditorApp.h"
#include "GuiPreviewStage.h"
#include "ImGuiFacadeRenderer.h"

#include <engine_render/RenderTexture.h>

#include <imgui.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>
#include <chrono>

namespace
{
	//! a named simulated device the presets combo offers
	struct DevicePreset
	{
		const char*					label;
		unsigned int				width;
		unsigned int				height;
		float						scale;
		Orkige::SafeAreaInsets		insets;
	};

	//! insets bundled per preset (top/bottom for a portrait notch + home bar)
	Orkige::SafeAreaInsets makeInsets(unsigned int l, unsigned int t,
		unsigned int r, unsigned int b)
	{
		Orkige::SafeAreaInsets insets;
		insets.mLeft = l;
		insets.mTop = t;
		insets.mRight = r;
		insets.mBottom = b;
		return insets;
	}

	std::vector<DevicePreset> const& devicePresets()
	{
		static const std::vector<DevicePreset> presets = {
			{ "Phone portrait (1179x2556)", 1179, 2556, 3.0f, makeInsets(0, 141, 0, 102) },
			{ "Phone landscape (2556x1179)", 2556, 1179, 3.0f, makeInsets(141, 0, 141, 63) },
			{ "Tablet landscape (2048x1536)", 2048, 1536, 2.0f, makeInsets(0, 0, 0, 0) },
			{ "Tablet portrait (1536x2048)", 1536, 2048, 2.0f, makeInsets(0, 0, 0, 0) },
			{ "Desktop (1280x720)", 1280, 720, 1.0f, makeInsets(0, 0, 0, 0) },
			{ "Custom", 1080, 1920, 2.0f, makeInsets(0, 0, 0, 0) },
		};
		return presets;
	}

	//! persistent panel UI state (one editor => one panel => a function static)
	struct PreviewPanelState
	{
		int							presetIndex = 0;
		int							customWidth = 1080;
		int							customHeight = 1920;
		float						customScale = 2.0f;
		int							customInset[4] = { 0, 0, 0, 0 };	//!< l t r b
		bool						showNotch = true;	//!< apply the preset's insets
		std::string					selectedFile;		//!< project-relative .oui ("" = none)
		std::string					projectRoot;		//!< the project the file list is for
		std::vector<std::string>	ouiFiles;			//!< project-relative .oui paths
		bool						overlayRects = true;	//!< draw the widget rect overlay
		//! preview language tag ("" = source language); seeded from ViewSettings
		std::string					language;
		bool						languageInit = false;	//!< seeded from ini yet?
		// applied state so the stage only reconfigures on a real change
		OrkigeEditor::GuiPreviewContext	appliedContext;
		std::string					appliedFile;
		std::string					appliedLanguage;
		bool						appliedValid = false;
		// mtime watch for live reload
		std::filesystem::file_time_type	watchedMtime{};
		bool						watchArmed = false;
		std::chrono::steady_clock::time_point lastWatchPoll{};
	};

	//! scan a project for `.oui` layout files (project-relative, sorted)
	void scanOuiFiles(std::string const& root, std::vector<std::string>& out)
	{
		out.clear();
		if (root.empty())
		{
			return;
		}
		namespace fs = std::filesystem;
		std::error_code ec;
		const fs::path base(root);
		for (fs::recursive_directory_iterator it(base, ec), end;
			!ec && it != end; it.increment(ec))
		{
			if (it->is_regular_file(ec) &&
				it->path().extension() == ".oui")
			{
				out.push_back(fs::relative(it->path(), base, ec)
					.generic_string());
			}
		}
		std::sort(out.begin(), out.end());
	}

	//! resolve the current device context from the panel controls
	OrkigeEditor::GuiPreviewContext currentContext(PreviewPanelState const& ui)
	{
		OrkigeEditor::GuiPreviewContext ctx;
		DevicePreset const& preset = devicePresets()[ui.presetIndex];
		const bool custom = (ui.presetIndex ==
			static_cast<int>(devicePresets().size()) - 1);
		if (custom)
		{
			ctx.width = static_cast<unsigned int>(std::max(16, ui.customWidth));
			ctx.height = static_cast<unsigned int>(std::max(16, ui.customHeight));
			ctx.contentScale = ui.customScale;
			ctx.insets = makeInsets(
				static_cast<unsigned int>(std::max(0, ui.customInset[0])),
				static_cast<unsigned int>(std::max(0, ui.customInset[1])),
				static_cast<unsigned int>(std::max(0, ui.customInset[2])),
				static_cast<unsigned int>(std::max(0, ui.customInset[3])));
		}
		else
		{
			ctx.width = preset.width;
			ctx.height = preset.height;
			ctx.contentScale = preset.scale;
			ctx.insets = ui.showNotch ? preset.insets : makeInsets(0, 0, 0, 0);
		}
		return ctx;
	}
}

void drawGuiPreviewPanel(EditorState& state, OrkigeEditor::GuiPreviewStage& stage,
	Orkige::EditorCore& core, ViewSettings& viewSettings)
{
	(void)core;
	static PreviewPanelState ui;

	if (!ImGui::Begin("GuiPreview", &viewSettings.showGuiPreviewPanel))
	{
		ImGui::End();
		return;
	}

	const bool projectOpen = state.project.isLoaded();
	const std::string root = projectOpen ? state.project.getRootDirectory()
		: std::string();

	if (!projectOpen)
	{
		ImGui::TextDisabled("Open a project to preview its GUI screens.");
		ImGui::End();
		return;
	}

	// seed the preview language from the persisted setting once
	if (!ui.languageInit)
	{
		ui.language = viewSettings.guiPreviewLanguage;
		ui.languageInit = true;
	}

	// refresh the .oui list when the project changed (cheap; also on demand)
	if (ui.projectRoot != root)
	{
		ui.projectRoot = root;
		scanOuiFiles(root, ui.ouiFiles);
		ui.selectedFile.clear();
	}

	// load the open project's localisation directory into the stage's own
	// string table (idempotent - reloads only on a project/directory change) so
	// the language combo can enumerate languages and @keys resolve on preview
	stage.loadLocalisation(state.project);

	//--- controls row ------------------------------------------------------
	ImGui::SetNextItemWidth(240.0f);
	if (ImGui::BeginCombo("Screen", ui.selectedFile.empty()
		? "(none)" : ui.selectedFile.c_str()))
	{
		for (std::string const& file : ui.ouiFiles)
		{
			const bool sel = (file == ui.selectedFile);
			if (ImGui::Selectable(file.c_str(), sel))
			{
				ui.selectedFile = file;
			}
			if (sel)
			{
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}
	ImGui::SameLine();
	if (ImGui::Button("Refresh"))
	{
		scanOuiFiles(root, ui.ouiFiles);
	}
	ImGui::SameLine();
	if (ImGui::Button("Reload") && !ui.selectedFile.empty())
	{
		ui.appliedValid = false;	// force a re-show
	}

	ImGui::SetNextItemWidth(240.0f);
	if (ImGui::BeginCombo("Device", devicePresets()[ui.presetIndex].label))
	{
		for (int each = 0; each < static_cast<int>(devicePresets().size()); ++each)
		{
			const bool sel = (each == ui.presetIndex);
			if (ImGui::Selectable(devicePresets()[each].label, sel))
			{
				ui.presetIndex = each;
			}
		}
		ImGui::EndCombo();
	}
	const bool custom = (ui.presetIndex ==
		static_cast<int>(devicePresets().size()) - 1);
	if (custom)
	{
		ImGui::SetNextItemWidth(90.0f);
		ImGui::InputInt("W", &ui.customWidth, 0);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(90.0f);
		ImGui::InputInt("H", &ui.customHeight, 0);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(90.0f);
		ImGui::InputFloat("Scale", &ui.customScale, 0.0f, 0.0f, "%.1f");
		ImGui::SetNextItemWidth(200.0f);
		ImGui::InputInt4("Safe area (l t r b)", ui.customInset);
	}
	else
	{
		ImGui::SameLine();
		ImGui::Checkbox("Notch", &ui.showNotch);
	}
	ImGui::SameLine();
	ImGui::Checkbox("Widget rects", &ui.overlayRects);

	//--- language axis: "(source)" plus every loaded language --------------
	// the combo is present always; a project with no loc/ directory shows only
	// "(source)" (getLanguages() is empty then - the honest edge)
	const std::vector<std::string> languages = stage.getLanguages();
	const char* languageLabel = ui.language.empty() ? "(source)"
		: ui.language.c_str();
	ImGui::SetNextItemWidth(240.0f);
	if (ImGui::BeginCombo("Language", languageLabel))
	{
		if (ImGui::Selectable("(source)", ui.language.empty()))
		{
			ui.language.clear();
			viewSettings.guiPreviewLanguage = ui.language;
			viewSettings.save();
		}
		for (std::string const& lang : languages)
		{
			const bool sel = (lang == ui.language);
			if (ImGui::Selectable(lang.c_str(), sel))
			{
				ui.language = lang;
				viewSettings.guiPreviewLanguage = ui.language;
				viewSettings.save();
			}
			if (sel)
			{
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}

	//--- apply controls to the shared stage --------------------------------
	const OrkigeEditor::GuiPreviewContext ctx = currentContext(ui);
	const bool contextChanged = !ui.appliedValid ||
		ctx != ui.appliedContext;
	const bool fileChanged = !ui.appliedValid ||
		ui.selectedFile != ui.appliedFile;
	const bool languageChanged = !ui.appliedValid ||
		ui.language != ui.appliedLanguage;

	if (contextChanged || fileChanged || languageChanged)
	{
		stage.setContext(ctx);
		stage.setPreviewLanguage(ui.language);	// re-resolves @keys on show
		std::string err;
		stage.show(root, ui.selectedFile, err);	// "" file => empty state
		ui.appliedContext = ctx;
		ui.appliedFile = ui.selectedFile;
		ui.appliedLanguage = ui.language;
		ui.appliedValid = true;
		// (re)arm the mtime watch on the newly shown file
		ui.watchArmed = false;
	}

	//--- live reload on file change (write_project_file from an agent) ------
	if (!ui.selectedFile.empty())
	{
		const auto now = std::chrono::steady_clock::now();
		if (now - ui.lastWatchPoll > std::chrono::milliseconds(250))
		{
			ui.lastWatchPoll = now;
			namespace fs = std::filesystem;
			std::error_code ec;
			const fs::path abs = fs::path(root) / ui.selectedFile;
			const auto mtime = fs::last_write_time(abs, ec);
			if (!ec)
			{
				if (!ui.watchArmed)
				{
					ui.watchedMtime = mtime;
					ui.watchArmed = true;
				}
				else if (mtime != ui.watchedMtime)
				{
					ui.watchedMtime = mtime;
					std::string err;
					stage.show(root, ui.selectedFile, err);	// reload
				}
			}
		}
	}

	// tick the gui so it lays out + submits into the offscreen target
	stage.tick(ImGui::GetIO().DeltaTime);

	//--- the preview image -------------------------------------------------
	ImGui::Separator();
	if (ui.selectedFile.empty())
	{
		ImGui::TextDisabled("Pick a .oui screen above to preview it. "
			"Screens live under the project (screens/*.oui).");
		ImGui::End();
		return;
	}
	if (!stage.isLoaded() || !stage.getTarget())
	{
		ImGui::TextWrapped("Could not preview '%s': %s",
			ui.selectedFile.c_str(), stage.getLastError().c_str());
		ImGui::End();
		return;
	}

	Orkige::optr<Orkige::RenderTexture> target = stage.getTarget();
	const float targetW = static_cast<float>(target->getWidth());
	const float targetH = static_cast<float>(target->getHeight());
	const ImVec2 avail = ImGui::GetContentRegionAvail();
	// fit the device frame into the panel, preserving aspect
	float drawW = avail.x;
	float drawH = avail.x * (targetH / targetW);
	if (drawH > avail.y)
	{
		drawH = avail.y;
		drawW = avail.y * (targetW / targetH);
	}
	const float offsetX = (avail.x - drawW) * 0.5f;
	if (offsetX > 0.0f)
	{
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offsetX);
	}
	const ImVec2 imageMin = ImGui::GetCursorScreenPos();
	ImGui::Image(gImGuiRenderer->textureIdFor(target), ImVec2(drawW, drawH));

	// widget-rect overlay: outline each resolved widget (agent-visible layout)
	if (ui.overlayRects)
	{
		ImDrawList* draw = ImGui::GetWindowDrawList();
		const float sx = drawW / targetW;
		const float sy = drawH / targetH;
		std::vector<OrkigeEditor::GuiPreviewWidgetRect> rects =
			stage.getWidgetRects();
		for (OrkigeEditor::GuiPreviewWidgetRect const& r : rects)
		{
			if (!r.visible)
			{
				continue;
			}
			const ImVec2 a(imageMin.x + r.left * sx, imageMin.y + r.top * sy);
			const ImVec2 b(imageMin.x + (r.left + r.width) * sx,
				imageMin.y + (r.top + r.height) * sy);
			const ImU32 colour = r.enabled ? IM_COL32(90, 200, 120, 200)
				: IM_COL32(150, 150, 150, 160);
			draw->AddRect(a, b, colour);
		}
	}

	ImGui::End();
}
