/********************************************************************
	created:	Friday 2026/07/24 at 12:00
	filename: 	EditorGamePreviewPanel.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

//! @file EditorGamePreviewPanel.cpp
//! @brief the editor's Game Preview tab: the authored scene rendered through its
//! OWN scene camera at a DEVICE preset (resolution / content scale / safe-area),
//! with an optional `.oui` SCREEN composited on top, safe-area guides, a
//! procedurally-drawn device FRAME (our own generic silhouette - bezel, rounded-
//! corner mask, notch / punch-hole cutout, home indicator) and an ANIMATE
//! MATERIALS toggle. The world stays dormant - no scripts, no gameplay ticking
//! (the editor safety contract). Replaces the retired GUI Preview tab (clean
//! cutover); the shared GamePreviewStage also backs the preview_game MCP verb.

#include "EditorApp.h"
#include "EditorTabMenu.h"
#include "GamePreviewStage.h"
#include "EditorUiEditorPanel.h"
#include "FileFormatIcon.h"
#include "ImGuiFacadeRenderer.h"
#include "IconsFontAwesome6.h"

#include <core_util/DevicePreset.h>
#include <engine_render/RenderTexture.h>

#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

namespace
{
	//! a device-frame bezel colour that reads dark in both editor themes
	const ImU32 FRAME_BEZEL = IM_COL32(18, 19, 23, 255);
	//! a subtle edge highlight on the bezel rim
	const ImU32 FRAME_EDGE = IM_COL32(64, 66, 74, 255);
	//! the home-indicator bar (a soft light pill over the screen)
	const ImU32 FRAME_INDICATOR = IM_COL32(228, 230, 236, 170);
	//! the safe-area guide line
	const ImU32 SAFE_AREA_GUIDE = IM_COL32(255, 196, 64, 210);

	//! persistent panel UI state (one editor => one panel => a function static)
	struct GamePanelState
	{
		bool						autoDockAttempted = false;
		bool						seededFromSettings = false;
		int							presetIndex = 0;
		int							customWidth = 1080;
		int							customHeight = 1920;
		float						customScale = 2.0f;
		int							customInset[4] = { 0, 0, 0, 0 };	//!< l t r b
		bool						showSafeArea = true;	//!< safe-area guides
		bool						animateMaterials = false;	//!< material clock
		bool						showFrame = true;		//!< procedural device frame
		bool						overlayRects = false;	//!< draw the widget rect overlay
		OrkigeEditor::UiEditSession	editSession;			//!< the edit document + selection
		std::string					editAppliedFile;		//!< the file the session holds
		bool						usingDefaultCamera = false;	//!< last frame fell back to the default camera (drives the hint)
		std::string					selectedFile;		//!< project-relative .oui ("" = none)
		std::string					projectRoot;		//!< the project the file list is for
		std::vector<std::string>	ouiFiles;			//!< project-relative .oui paths
		std::string					language;			//!< preview language ("" = source)
		bool						languageInit = false;
		// applied state so the stage only reconfigures on a real change
		OrkigeEditor::GuiPreviewContext	appliedContext;
		std::string					appliedFile;
		std::string					appliedLanguage;
		bool						appliedValid = false;
		// mtime watch for live reload of the overlay screen
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
			// never descend into reserved output / editor-private dirs
			if (it->is_directory(ec) &&
				Orkige::ProjectPaths::isReservedOutputDir(it->path()))
			{
				it.disable_recursion_pending();
				continue;
			}
			if (it->is_regular_file(ec) && it->path().extension() == ".oui")
			{
				out.push_back(fs::relative(it->path(), base, ec).generic_string());
			}
		}
		std::sort(out.begin(), out.end());
	}

	//! resolve the current device context (RTT size / scale / insets) from the
	//! controls. The Free preset is panel-sized: the caller passes the panel
	//! pixel extent through @p panelW/@p panelH.
	OrkigeEditor::GuiPreviewContext currentContext(GamePanelState const& ui,
		int panelW, int panelH)
	{
		using namespace Orkige;
		const DevicePreset::Preset& preset =
			DevicePreset::forKind(static_cast<DevicePreset::Kind>(ui.presetIndex));
		OrkigeEditor::GuiPreviewContext ctx;
		if (preset.panelSized)
		{
			// Free: follow the panel (quantised so a sub-pixel jiggle never
			// rebuilds the RTT). Content scale 1, no insets.
			ctx.width = static_cast<unsigned int>(
				std::max(16, (panelW / 4) * 4));
			ctx.height = static_cast<unsigned int>(
				std::max(16, (panelH / 4) * 4));
			ctx.contentScale = 1.0f;
			ctx.insets = DevicePreset::makeInsets(0, 0, 0, 0);
		}
		else if (preset.custom)
		{
			ctx.width = static_cast<unsigned int>(std::max(16, ui.customWidth));
			ctx.height = static_cast<unsigned int>(std::max(16, ui.customHeight));
			ctx.contentScale = ui.customScale;
			ctx.insets = DevicePreset::makeInsets(
				static_cast<unsigned int>(std::max(0, ui.customInset[0])),
				static_cast<unsigned int>(std::max(0, ui.customInset[1])),
				static_cast<unsigned int>(std::max(0, ui.customInset[2])),
				static_cast<unsigned int>(std::max(0, ui.customInset[3])));
		}
		else
		{
			ctx.width = preset.width;
			ctx.height = preset.height;
			ctx.contentScale = preset.contentScale;
			ctx.insets = preset.insets;
		}
		return ctx;
	}

}

namespace OrkigeEditor
{
	//! the Game Preview panel's last-draw seam (@see GamePreviewPanelDebug)
	GamePreviewPanelDebug& gamePreviewPanelDebug()
	{
		static GamePreviewPanelDebug debug;
		return debug;
	}
}

void drawGamePreviewPanel(EditorState& state, OrkigeEditor::GamePreviewStage& stage,
	Orkige::EditorCore& core, Orkige::GameObjectManager& world,
	ViewSettings& viewSettings)
{
	OrkigeEditor::GamePreviewPanelDebug& dbg =
		OrkigeEditor::gamePreviewPanelDebug();
	dbg = OrkigeEditor::GamePreviewPanelDebug();	// reset each draw
	using namespace Orkige;
	(void)core;
	static GamePanelState ui;

	// seed the persisted controls once
	if (!ui.seededFromSettings)
	{
		ui.presetIndex = std::clamp(viewSettings.gamePreviewPreset, 0,
			DevicePreset::count() - 1);
		ui.showSafeArea = viewSettings.gamePreviewSafeAreaGuides;
		ui.animateMaterials = viewSettings.gamePreviewAnimateMaterials;
		ui.showFrame = viewSettings.gamePreviewShowFrame;
		ui.seededFromSettings = true;
	}

	dockPreviewBesideSceneOnce("Preview", ui.autoDockAttempted);
	if (!state.requestedGuiPreviewAsset.empty())
	{
		ImGui::SetNextWindowFocus();
	}

	const bool shown =
		ImGui::Begin("Preview", &viewSettings.showPreviewPanel);
	OrkigeEditor::editorPanelTabMenu(&viewSettings.showPreviewPanel);
	// the Game Preview being the visible/active tab this frame vetoes the global
	// lighting-suppression (the real game look wins - @see shouldSuppressLighting)
	state.gamePreviewVisibleThisFrame = shown;
	if (shown && ImGui::IsWindowFocused())
	{
		state.lastFocusedGameView = OrkigeEditor::GameViewRenderer::Preview;
	}
	if (!shown)
	{
		ImGui::End();
		return;
	}

	const bool projectOpen = state.project.isLoaded();
	const std::string root = projectOpen ? state.project.getRootDirectory()
		: std::string();

	// refresh the .oui list on a project change (also honours a browser request)
	if (projectOpen && ui.projectRoot != root)
	{
		ui.projectRoot = root;
		scanOuiFiles(root, ui.ouiFiles);
		ui.selectedFile.clear();
	}
	if (!state.requestedGuiPreviewAsset.empty())
	{
		if (projectOpen)
		{
			scanOuiFiles(root, ui.ouiFiles);
			ui.selectedFile = state.requestedGuiPreviewAsset;
			ui.appliedValid = false;
		}
		state.requestedGuiPreviewAsset.clear();
	}

	// seed the preview language from the persisted setting once
	if (!ui.languageInit)
	{
		ui.language = viewSettings.guiPreviewLanguage;
		ui.languageInit = true;
	}

	// the control fields use EXACTLY the Inspector's field styling: the standard
	// theme FrameBg ladder on the plain panel surface (windowBg) plus the
	// recessed 1px field border the Inspector opts into (the global default is
	// borderless), so a combo/checkbox here is indistinguishable from a
	// component-panel field. Popped before the preview image (a content area).
	ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
	ImGui::PushStyleColor(ImGuiCol_Border, Orkige::editorFieldBorderColor());

	//--- controls row 1: device + toggles ----------------------------------
	const DevicePreset::Preset& preset =
		DevicePreset::forKind(static_cast<DevicePreset::Kind>(ui.presetIndex));
	ImGui::SetNextItemWidth(260.0f);
	if (ImGui::BeginCombo("Device", preset.label))
	{
		for (int each = 0; each < DevicePreset::count(); ++each)
		{
			const bool sel = (each == ui.presetIndex);
			if (ImGui::Selectable(DevicePreset::forKind(
				static_cast<DevicePreset::Kind>(each)).label, sel))
			{
				ui.presetIndex = each;
				viewSettings.gamePreviewPreset = each;
				viewSettings.save();
			}
			if (sel) { ImGui::SetItemDefaultFocus(); }
		}
		ImGui::EndCombo();
	}
	if (preset.custom)
	{
		ImGui::SetNextItemWidth(80.0f);
		ImGui::InputInt("W", &ui.customWidth, 0);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(80.0f);
		ImGui::InputInt("H", &ui.customHeight, 0);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(80.0f);
		ImGui::InputFloat("Scale", &ui.customScale, 0.0f, 0.0f, "%.1f");
		ImGui::SetNextItemWidth(200.0f);
		ImGui::InputInt4("Safe area (l t r b)", ui.customInset);
	}
	ImGui::SameLine();
	if (Orkige::compactCheckbox("Safe area", &ui.showSafeArea))
	{
		viewSettings.gamePreviewSafeAreaGuides = ui.showSafeArea;
		viewSettings.save();
	}
	ImGui::SameLine();
	// Free/Custom never frame (no realistic device silhouette)
	const bool canFrame = !preset.panelSized && !preset.custom;
	ImGui::BeginDisabled(!canFrame);
	if (Orkige::compactCheckbox("Device frame", &ui.showFrame))
	{
		viewSettings.gamePreviewShowFrame = ui.showFrame;
		viewSettings.save();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	if (Orkige::compactCheckbox("Animate materials", &ui.animateMaterials))
	{
		viewSettings.gamePreviewAnimateMaterials = ui.animateMaterials;
		viewSettings.save();
	}

	//--- controls row 2: overlay screen + language (project-only) ----------
	// the scene-through-a-camera preview works WITHOUT a project (a loose
	// scene); only the .oui overlay picker needs the project's screens.
	if (!projectOpen)
	{
		ui.selectedFile.clear();
		ImGui::TextDisabled("Open a project to overlay a .oui screen.");
	}
	else
	{
		ImGui::SetNextItemWidth(220.0f);
		if (ImGui::BeginCombo("Overlay", ui.selectedFile.empty()
			? "(scene only)" : ui.selectedFile.c_str()))
		{
			if (ImGui::Selectable("(scene only)", ui.selectedFile.empty()))
			{
				ui.selectedFile.clear();
			}
			// every entry here is a .oui screen, so the leading glyph + tint
			// (@see FileFormatIcon.h - the same one the asset browser draws
			// .oui rows with) is the same for the whole list
			const OrkigeEditor::FileFormatIcon ouiIcon =
				OrkigeEditor::fileFormatIcon(".oui");
			const ImU32 ouiTint =
				IM_COL32(ouiIcon.color.r, ouiIcon.color.g, ouiIcon.color.b, 255);
			for (std::string const& file : ui.ouiFiles)
			{
				const bool sel = (file == ui.selectedFile);
				ImGui::PushStyleColor(ImGuiCol_Text, ouiTint);
				const bool clicked = ImGui::Selectable(
					(std::string(ouiIcon.glyph) + "  " + file).c_str(), sel);
				ImGui::PopStyleColor();
				if (clicked)
				{
					ui.selectedFile = file;
				}
				if (sel) { ImGui::SetItemDefaultFocus(); }
			}
			ImGui::EndCombo();
		}
		ImGui::SameLine();
		if (ImGui::Button("Refresh"))
		{
			scanOuiFiles(root, ui.ouiFiles);
		}
		// the overlay needs the project's localisation directory + a language axis
		stage.loadLocalisation(state.project);
	}
	const std::vector<std::string> languages = stage.getLanguages();
	if (!languages.empty() || !ui.language.empty())
	{
		ImGui::SameLine();
		ImGui::SetNextItemWidth(160.0f);
		const char* languageLabel = ui.language.empty() ? "(source)"
			: ui.language.c_str();
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
				if (sel) { ImGui::SetItemDefaultFocus(); }
			}
			ImGui::EndCombo();
		}
	}
	if (projectOpen)
	{
		ImGui::SameLine();
		Orkige::compactCheckbox("Widget rects", &ui.overlayRects);
		// EDIT MODE IS ALWAYS ON: showing a .oui screen IS editing it - the canvas
		// selection/grips/guides are simply live whenever a screen is picked (no
		// separate mode toggle). The slim edit row (a quick "+ Add" and a Delete)
		// is canvas-adjacent; the full palette + tree + properties live in the
		// dockable UI Editor panel.
		if (!ui.selectedFile.empty() && ui.editSession.loaded)
		{
			ImGui::SameLine();
			// the compact "+ Add" opens the SAME kind picker as the UI Editor
			// panel's Add Widget button (one add-widget flow, two entry points)
			OrkigeEditor::uiEditAddWidgetControl(ui.editSession, stage,
				"preview", false);
			ImGui::SameLine();
			ImGui::BeginDisabled(ui.editSession.selection.empty());
			// the same trash-can glyph as the UI Editor tree's row control
			if (ImGui::SmallButton(ICON_FA_TRASH_CAN))
			{
				OrkigeEditor::uiEditDeleteSelected(ui.editSession);
				std::string delErr;
				OrkigeEditor::uiEditSave(ui.editSession, stage, delErr);
			}
			ImGui::SetItemTooltip("Delete widget");
			ImGui::EndDisabled();
		}
	}

	// a subtle dimmed hint in the control row (NOT over the image) when the
	// scene has no CameraComponent: the preview mirrors the game, which runs
	// through the default window camera in that case (@see item below). Uses the
	// previous frame's resolution (redrawn every frame, so no visible lag).
	if (ui.usingDefaultCamera)
	{
		ImGui::TextDisabled(
			"default camera - add a Camera object to control framing");
	}

	// the field styling ends here - the preview image is a content area
	ImGui::PopStyleColor();
	ImGui::PopStyleVar();

	ImGui::Separator();
	const ImVec2 avail = ImGui::GetContentRegionAvail();
	if (avail.x < 8.0f || avail.y < 8.0f)
	{
		ImGui::End();
		return;
	}

	// a picked screen IS edit mode (always on). The composite image fills the whole
	// panel on both flavors - no reserved sidebar column; the tools live in the
	// dockable UI Editor panel.
	const bool editActive = projectOpen && !ui.selectedFile.empty();
	const float canvasW = std::max(16.0f, avail.x);

	//--- apply controls to the shared stage --------------------------------
	const OrkigeEditor::GuiPreviewContext ctx = currentContext(ui,
		static_cast<int>(canvasW), static_cast<int>(avail.y));
	const bool contextChanged = !ui.appliedValid || ctx != ui.appliedContext;
	const bool fileChanged = !ui.appliedValid || ui.selectedFile != ui.appliedFile;
	const bool languageChanged =
		!ui.appliedValid || ui.language != ui.appliedLanguage;

	if (contextChanged || fileChanged || languageChanged)
	{
		stage.setContext(ctx);
		stage.setPreviewLanguage(ui.language);
		std::string err;
		stage.setOverlayScreen(root, ui.selectedFile, err);	// "" = scene only
		ui.appliedContext = ctx;
		ui.appliedFile = ui.selectedFile;
		ui.appliedLanguage = ui.language;
		ui.appliedValid = true;
		ui.watchArmed = false;
	}

	// live reload the overlay screen on file change (an agent editing over MCP)
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
					stage.setOverlayScreen(root, ui.selectedFile, err);
				}
			}
		}
	}

	// drive the preview: copy the ACTIVE scene camera + advance the material
	// clock when armed (material-parameter animation ONLY - the world stays
	// dormant), tick the overlay. The Scene panel's inset tracks the SELECTED
	// camera through a separate stage; this panel tracks the active one ("").
	const float dt = ui.animateMaterials ? ImGui::GetIO().DeltaTime : 0.0f;
	stage.update(world, "", ui.animateMaterials, dt);
	dbg.hasCamera = stage.hasCamera();
	dbg.usedDefaultCamera = stage.usedDefaultCamera();
	dbg.trackedCameraId = stage.getTrackedCameraId();
	// a camera-less scene renders through the DEFAULT window camera (mirroring
	// the game) - the control-row hint is shown next frame (drawn above, before
	// the image, so it never overlaps the preview)
	ui.usingDefaultCamera = stage.usedDefaultCamera();

	optr<RenderTexture> target = stage.getTarget();
	if (!target)
	{
		ImGui::TextWrapped("The Game Preview could not build its target: %s",
			stage.getLastError().c_str());
		ImGui::End();
		return;
	}

	const float targetW = static_cast<float>(target->getWidth());
	const float targetH = static_cast<float>(target->getHeight());
	const float targetAspect = targetH > 0.0f ? targetW / targetH : 1.0f;

	const ImVec2 origin = ImGui::GetCursorScreenPos();
	ImDrawList* draw = ImGui::GetWindowDrawList();
	// the device frame draws in edit mode too: the bezel/notch/punch-hole occludes
	// the screen area honestly, and the canvas gizmos (which clip to the canvas
	// image rect) sit over it. The canvas placement below is frame-aware.
	const bool framed = ui.showFrame && canFrame;

	// fit the SCREEN into the canvas column, leaving room for the bezel when framed
	float drawW = canvasW;
	float drawH = canvasW / targetAspect;
	if (drawH > avail.y)
	{
		drawH = avail.y;
		drawW = avail.y * targetAspect;
	}
	Orkige::DevicePreset::FrameGeometry frame;
	if (framed)
	{
		// shrink the screen so the derived bezel still fits the panel
		float screenX = origin.x + (canvasW - drawW) * 0.5f;
		float screenY = origin.y + (avail.y - drawH) * 0.5f;
		frame = DevicePreset::deriveFrame(preset, screenX, screenY, drawW, drawH);
		const float scaleX = frame.bezelW > 0.0f
			? std::min(1.0f, canvasW / frame.bezelW) : 1.0f;
		const float scaleY = frame.bezelH > 0.0f
			? std::min(1.0f, avail.y / frame.bezelH) : 1.0f;
		const float scale = std::min(scaleX, scaleY);
		if (scale < 1.0f)
		{
			drawW *= scale;
			drawH *= scale;
		}
		screenX = origin.x + (canvasW - drawW) * 0.5f;
		screenY = origin.y + (avail.y - drawH) * 0.5f;
		frame = DevicePreset::deriveFrame(preset, screenX, screenY, drawW, drawH);

		// the bezel behind the image (a rounded rect + a rim highlight)
		draw->AddRectFilled(ImVec2(frame.bezelX, frame.bezelY),
			ImVec2(frame.bezelX + frame.bezelW, frame.bezelY + frame.bezelH),
			FRAME_BEZEL, frame.bezelRadius);
		draw->AddRect(ImVec2(frame.bezelX, frame.bezelY),
			ImVec2(frame.bezelX + frame.bezelW, frame.bezelY + frame.bezelH),
			FRAME_EDGE, frame.bezelRadius, 0, 1.5f);
	}

	const float imageX = framed ? frame.screenX
		: origin.x + (canvasW - drawW) * 0.5f;
	const float imageY = framed ? frame.screenY
		: origin.y + (avail.y - drawH) * 0.5f;
	const ImVec2 imageMin(imageX, imageY);
	const ImVec2 imageMax(imageX + drawW, imageY + drawH);

	// the composite (scene + overlay) - the facade HANDLE re-resolves per draw.
	// When framed, the image itself is drawn WITH ROUNDED CORNERS matching the
	// preset's screen radius (AddImageRounded) so the glass corners are real
	// rounding, never corner-mask polygons (which read as angular spikes). The
	// bezel ring is already drawn beneath; the cutout/indicator go on top.
	const ImTextureID composite = gImGuiRenderer->textureIdFor(target);
	if (framed && frame.screenRadius > 0.5f)
	{
		draw->AddImageRounded(composite, imageMin, imageMax,
			ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), IM_COL32_WHITE,
			frame.screenRadius);
	}
	else
	{
		draw->AddImage(composite, imageMin, imageMax);
	}
	dbg.drewImage = true;
	dbg.framed = framed;
	dbg.cutout = framed ? static_cast<int>(frame.cutout)
		: static_cast<int>(Orkige::DevicePreset::CUT_NONE);
	dbg.targetWidth = target->getWidth();
	dbg.targetHeight = target->getHeight();

	// render invariant: when the Game Preview is NOT the frame's renderer (both
	// game views are up and the Scene view is focused), its RTT was FROZEN this
	// frame - dim the frozen image and say so (@see chooseGameViewRenderer). The
	// texture persists, so there is no flicker on focus handoff.
	if (state.gameViewRenderer != OrkigeEditor::GameViewRenderer::Preview)
	{
		draw->AddRectFilled(imageMin, imageMax, IM_COL32(8, 10, 14, 150));
		char const* pausedNote = "Paused while Scene is active";
		const ImVec2 noteSize = ImGui::CalcTextSize(pausedNote);
		draw->AddText(ImVec2(
			(imageMin.x + imageMax.x - noteSize.x) * 0.5f,
			(imageMin.y + imageMax.y - noteSize.y) * 0.5f),
			ImGui::GetColorU32(ImGuiCol_Text, 0.85f), pausedNote);
	}

	// the device frame's occluding intrusions, drawn OVER the image so, with
	// the safe-area guides on, the user sees precisely what the device steals
	if (framed)
	{
		if (frame.cutout == DevicePreset::CUT_NOTCH)
		{
			// the notch is ONE CONTINUOUS shape with the bezel: it extends FROM
			// the bezel edge into the screen, squared where it meets the bezel and
			// rounded only on the far corners. Overlap ~2px into the bezel so
			// there is no seam (same colour).
			const float overlap = 2.0f;
			if (frame.notchOnTop)
			{
				draw->AddRectFilled(
					ImVec2(frame.cutoutX, frame.cutoutY - overlap),
					ImVec2(frame.cutoutX + frame.cutoutW,
						frame.cutoutY + frame.cutoutH),
					FRAME_BEZEL, frame.cutoutRadius,
					ImDrawFlags_RoundCornersBottom);
			}
			else
			{
				// landscape: hangs from the LEFT bezel, rounded on the right
				draw->AddRectFilled(
					ImVec2(frame.cutoutX - overlap, frame.cutoutY),
					ImVec2(frame.cutoutX + frame.cutoutW,
						frame.cutoutY + frame.cutoutH),
					FRAME_BEZEL, frame.cutoutRadius,
					ImDrawFlags_RoundCornersRight);
			}
		}
		else if (frame.cutout == DevicePreset::CUT_PILL)
		{
			// a DETACHED, fully-rounded pill floating below the top edge (this IS
			// the intended "dynamic island" look, distinct from a notch)
			draw->AddRectFilled(ImVec2(frame.cutoutX, frame.cutoutY),
				ImVec2(frame.cutoutX + frame.cutoutW,
					frame.cutoutY + frame.cutoutH),
				FRAME_BEZEL, frame.cutoutRadius);
		}
		else if (frame.cutout == DevicePreset::CUT_PUNCHHOLE)
		{
			draw->AddCircleFilled(
				ImVec2(frame.cutoutX + frame.cutoutRadius,
					frame.cutoutY + frame.cutoutRadius),
				frame.cutoutRadius, FRAME_BEZEL);
		}
		// a physical home button (home-button phones): a thin ring in the chin
		if (frame.hasHomeButton && frame.homeButtonRadius > 1.0f)
		{
			draw->AddCircle(
				ImVec2(frame.homeButtonX, frame.homeButtonY),
				frame.homeButtonRadius, FRAME_EDGE, 0, 2.0f);
		}
		if (frame.hasIndicator)
		{
			draw->AddRectFilled(ImVec2(frame.indicatorX, frame.indicatorY),
				ImVec2(frame.indicatorX + frame.indicatorW,
					frame.indicatorY + frame.indicatorH),
				FRAME_INDICATOR, frame.indicatorRadius);
		}
	}

	// safe-area guides: the inset rectangle from the preset, mapped from device
	// pixels to the displayed image rect (a toggle). Zero insets draw nothing.
	if (ui.showSafeArea && targetW > 0.0f && targetH > 0.0f)
	{
		const Orkige::SafeAreaInsets& in = ctx.insets;
		if (in.mLeft || in.mTop || in.mRight || in.mBottom)
		{
			const float sx = drawW / targetW;
			const float sy = drawH / targetH;
			const ImVec2 a(imageMin.x + in.mLeft * sx,
				imageMin.y + in.mTop * sy);
			const ImVec2 b(imageMax.x - in.mRight * sx,
				imageMax.y - in.mBottom * sy);
			draw->AddRect(a, b, SAFE_AREA_GUIDE, 0.0f, 0, 1.5f);
			dbg.safeAreaDrawn = true;
		}
	}

	// EDIT-MODE SEAM: the resolved overlay widget rects stay addressable here so
	// a future preview edit mode can hit-test/drag widgets over the live scene.
	// For now they are an opt-in debug overlay.
	if (ui.overlayRects && targetW > 0.0f)
	{
		const float sx = drawW / targetW;
		const float sy = drawH / targetH;
		for (OrkigeEditor::GuiPreviewWidgetRect const& widget :
			stage.getOverlayWidgetRects())
		{
			if (!widget.visible) { continue; }
			const ImVec2 a(imageMin.x + widget.left * sx,
				imageMin.y + widget.top * sy);
			const ImVec2 b(imageMin.x + (widget.left + widget.width) * sx,
				imageMin.y + (widget.top + widget.height) * sy);
			draw->AddRect(a, b, widget.enabled ? IM_COL32(90, 200, 120, 200)
				: IM_COL32(150, 150, 150, 160));
		}
	}

	// EDIT MODE: the visual .oui editor. The overlay is rendered through the SAME
	// GamePreviewStage; here we draw the selection/handle adornments over its
	// image (the canvas). The tool surface (tree/properties/palette/undo/save)
	// lives in the dockable UI Editor panel, fed the session through
	// UiEditorPanelLink (@see EditorUiEditorPanel).
	OrkigeEditor::UiEditorDebug& editDbg = OrkigeEditor::uiEditorDebug();
	editDbg = OrkigeEditor::UiEditorDebug();
	OrkigeEditor::UiEditorPanelLink& editLink = OrkigeEditor::uiEditorPanelLink();
	// the UI Editor tool panel EXISTS only while a screen is open here: reveal it
	// when a .oui is shown, retire it when the screen deselects (no permanent empty
	// tab). Toggled only on a real change so the dock layout never thrashes.
	if (viewSettings.showUiEditorPanel != editActive)
	{
		viewSettings.showUiEditorPanel = editActive;
		viewSettings.save();
	}
	if (editActive)
	{
		// (re)load the document when the picked file changes
		if (ui.editAppliedFile != ui.selectedFile || !ui.editSession.loaded)
		{
			std::string err;
			OrkigeEditor::uiEditLoad(ui.editSession, stage, root,
				ui.selectedFile, err);
			ui.editAppliedFile = ui.selectedFile;
		}
		OrkigeEditor::UiEditCanvas canvas;
		canvas.imageX = imageMin.x;
		canvas.imageY = imageMin.y;
		canvas.drawW = drawW;
		canvas.drawH = drawH;
		canvas.surfaceW = targetW;
		canvas.surfaceH = targetH;
		// modifier-snap to a coarse design grid (like the scene gizmos' snap key)
		const float snap = ImGui::GetIO().KeyShift ? 10.0f : 0.0f;
		OrkigeEditor::uiEditDrawCanvas(ui.editSession, stage, canvas, draw, snap);

		// hand the session to the UI Editor panel (drawn later this frame); the
		// canvas holding focus also routes Cmd/Ctrl+Z to the document
		editLink.session = &ui.editSession;
		editLink.stage = &stage;
		editLink.editActive = true;
		editLink.projectRoot = root;
		editLink.contextFocused |=
			ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

		editDbg.active = true;
		editDbg.loaded = ui.editSession.loaded;
		editDbg.sectionCount =
			static_cast<int>(ui.editSession.doc.doc().sections.size());
		editDbg.widgetRectCount =
			static_cast<int>(stage.getOverlayWidgetRects().size());
		editDbg.selected = ui.editSession.selected;
		editDbg.dirty = ui.editSession.doc.dirty();
		editDbg.canUndo = ui.editSession.doc.canUndo();
		editDbg.selectionCount =
			static_cast<int>(ui.editSession.selection.size());
		editDbg.canvasImageX = canvas.imageX;
		editDbg.canvasImageY = canvas.imageY;
		editDbg.canvasDrawW = canvas.drawW;
		editDbg.canvasDrawH = canvas.drawH;
		editDbg.canvasSurfaceW = canvas.surfaceW;
		editDbg.canvasSurfaceH = canvas.surfaceH;
	}
	else if (ui.editSession.loaded)
	{
		// leaving edit mode releases the session (the next entry reloads fresh)
		ui.editSession = OrkigeEditor::UiEditSession();
		ui.editAppliedFile.clear();
	}

	// reserve the drawn region so ImGui scrolling/sizing accounts for it. In edit
	// mode the canvas InvisibleButton already reserves its extent, so a full-avail
	// Dummy on top of it would over-grow the window.
	if (!editActive)
	{
		ImGui::Dummy(avail);
	}
	ImGui::End();
}
