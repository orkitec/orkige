// EditorToolbar.cpp - the play toolbar strip: Play/Pause(Resume)/Step/Stop,
// the desktop/simulator/adb target picker and the session status line.
// Split out of main.cpp (mechanical decomposition, see EditorApp.h).
#include "EditorApp.h"
#include "EditorTheme.h"
#include "IconsFontAwesome6.h"

#include <imgui_internal.h> // SeparatorEx (the vertical toolbar separators)

#include <cstdlib>
#include <filesystem>

// The play toolbar strip: a fixed window at the top of the work area (under
// the main menu bar, above the dockspace) carrying Play/Pause(Resume)/Step/
// Stop with state-appropriate enabling plus a session status line. Returns
// the height the dockspace below must leave free.

namespace
{
	//! transport-icon kinds drawn as vector primitives inside the button (the
	//! loaded UI font need not carry media-glyph codepoints)
	enum class TransportIcon { Play, Stop, Pause, Step };

	//! a toolbar button carrying a small drawn icon PLUS its text label
	bool transportButton(char const* label, TransportIcon icon)
	{
		ImGuiStyle const & style = ImGui::GetStyle();
		const float iconSide = ImGui::GetFontSize() * 0.62f;
		ImVec2 const textSize = ImGui::CalcTextSize(label);
		ImVec2 const size(
			style.FramePadding.x * 2.0f + iconSide +
				style.ItemInnerSpacing.x + textSize.x,
			0.0f);
		ImVec2 const origin = ImGui::GetCursorScreenPos();
		const bool pressed = ImGui::Button(label, size);
		// paint over the button: icon at the left inside the padding, the
		// label shifted right (the Button drew the label centred - cover the
		// frame interior with the frame colour first, then draw both parts)
		ImDrawList* draw = ImGui::GetWindowDrawList();
		ImVec2 const min = origin;
		ImVec2 const max = ImGui::GetItemRectMax();
		ImU32 const frameColour = ImGui::GetColorU32(
			ImGui::IsItemActive() ? ImGuiCol_ButtonActive :
			ImGui::IsItemHovered() ? ImGuiCol_ButtonHovered : ImGuiCol_Button);
		draw->AddRectFilled(
			ImVec2(min.x + 1.0f, min.y + 1.0f),
			ImVec2(max.x - 1.0f, max.y - 1.0f),
			frameColour, style.FrameRounding);
		ImU32 const textColour = ImGui::GetColorU32(ImGuiCol_Text);
		const float iconLeft = min.x + style.FramePadding.x;
		const float iconTop = min.y + (max.y - min.y - iconSide) * 0.5f;
		switch (icon)
		{
		case TransportIcon::Play:
			draw->AddTriangleFilled(
				ImVec2(iconLeft, iconTop),
				ImVec2(iconLeft, iconTop + iconSide),
				ImVec2(iconLeft + iconSide, iconTop + iconSide * 0.5f),
				textColour);
			break;
		case TransportIcon::Stop:
			draw->AddRectFilled(
				ImVec2(iconLeft + 0.5f, iconTop + 0.5f),
				ImVec2(iconLeft + iconSide - 0.5f, iconTop + iconSide - 0.5f),
				textColour, 1.0f);
			break;
		case TransportIcon::Pause:
		{
			const float barWidth = iconSide * 0.32f;
			draw->AddRectFilled(
				ImVec2(iconLeft, iconTop),
				ImVec2(iconLeft + barWidth, iconTop + iconSide),
				textColour, 1.0f);
			draw->AddRectFilled(
				ImVec2(iconLeft + iconSide - barWidth, iconTop),
				ImVec2(iconLeft + iconSide, iconTop + iconSide),
				textColour, 1.0f);
			break;
		}
		case TransportIcon::Step:
		{
			const float barWidth = iconSide * 0.28f;
			draw->AddTriangleFilled(
				ImVec2(iconLeft, iconTop),
				ImVec2(iconLeft, iconTop + iconSide),
				ImVec2(iconLeft + iconSide - barWidth - 1.5f,
					iconTop + iconSide * 0.5f),
				textColour);
			draw->AddRectFilled(
				ImVec2(iconLeft + iconSide - barWidth, iconTop),
				ImVec2(iconLeft + iconSide, iconTop + iconSide),
				textColour, 1.0f);
			break;
		}
		}
		draw->AddText(
			ImVec2(iconLeft + iconSide + style.ItemInnerSpacing.x,
				min.y + (max.y - min.y - textSize.y) * 0.5f),
			textColour, label);
		return pressed;
	}
}

float drawToolbar(EditorState& state, PlaySession& session,
	Orkige::EditorCore& core)
{
	Orkige::GameObjectManager& gameObjectManager = core.getGameObjectManager();
	const ImGuiViewport* mainViewport = ImGui::GetMainViewport();
	const float toolbarHeight = ImGui::GetFrameHeight() +
		ImGui::GetStyle().WindowPadding.y * 2.0f;
	ImGui::SetNextWindowPos(mainViewport->WorkPos);
	ImGui::SetNextWindowSize(ImVec2(mainViewport->WorkSize.x, toolbarHeight));
	// the toolbar is CHROME, not a tabbed panel - it keeps the darker strip
	// colour while panel bodies carry the brighter tab-matched surface
	ImGui::PushStyleColor(ImGuiCol_WindowBg, Orkige::editorChromeBackground());
	if (ImGui::Begin("##PlayToolbar", nullptr,
		ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
		ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings |
		ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus))
	{
		const PlaySession::Mode mode = session.mode;
		// Play runs the SCENE world; while a prefab stage is open the Play
		// button and the target picker grey out (the shortcut and the play
		// functions refuse too - this is the honest UI on top)
		const bool prefabMode = isPrefabEditActive(state);
		// play target picker: Desktop, a booted iOS simulator or a connected
		// Android device/emulator (plus enumerated-but-gated iOS hardware).
		// The device lists are scanned when the popup opens (short
		// synchronous simctl/adb calls - acceptable for an explicit user
		// action).
#ifdef __APPLE__
		static std::vector<SimulatorDevice> availableSimulators;
		static std::vector<IosHardwareDevice> iosHardware;
		static bool codesignIdentityPresent = false;
#endif
		static std::vector<AndroidDevice> androidDevices;
		static bool otherFlavorPlayerPresent = false;
		static bool webPlayerPresent = false;
		ImGui::BeginDisabled(mode != PlaySession::Mode::Edit || prefabMode);
		ImGui::SetNextItemWidth(150.0f);
		const char* targetPreview = "Desktop";
		if (!session.desktopLabel.empty())
		{
			targetPreview = session.desktopLabel.c_str();
		}
		else if (!session.simulatorUdid.empty())
		{
			targetPreview = session.simulatorLabel.c_str();
		}
		else if (!session.androidSerial.empty())
		{
			targetPreview = session.androidLabel.c_str();
		}
		else if (!session.iosDeviceUdid.empty())
		{
			targetPreview = session.iosDeviceLabel.c_str();
		}
		else if (session.browserTarget)
		{
			targetPreview = "Browser (WebGL)";
		}
		// the two desktop flavors: this build's own player and the OTHER
		// render flavor's (conventional preset build tree - baked in by
		// CMake). The debug protocol is flavor-agnostic; the visual result
		// must be identical (the WYSIWYG backend-parity rule).
#ifdef ORKIGE_RENDER_NEXT
		const char* const ownFlavorLabel = "Desktop (Ogre-Next)";
		const char* const otherFlavorLabel = "Desktop (classic OGRE)";
		const char* const otherFlavorPlayerPath =
			ORKIGE_EDITOR_PLAYER_PATH_CLASSIC;
#else
		const char* const ownFlavorLabel = "Desktop (classic OGRE)";
		const char* const otherFlavorLabel = "Desktop (Ogre-Next)";
		const char* const otherFlavorPlayerPath =
			ORKIGE_EDITOR_PLAYER_PATH_NEXT;
#endif
		if (ImGui::BeginCombo("##PlayTarget", targetPreview))
		{
			if (ImGui::IsWindowAppearing())
			{
#ifdef __APPLE__
				availableSimulators = listSimulators();
				// iOS hardware needs signed builds: only enumerate once
				// signing is fully configured - a codesigning identity AND a
				// provisioning profile (the devicectl call is the slower
				// one, so gate it too)
				codesignIdentityPresent = isIosSigningConfigured();
				iosHardware = codesignIdentityPresent
					? listIosHardwareDevices()
					: std::vector<IosHardwareDevice>();
#endif
				androidDevices = listAdbDevices();
				std::error_code ignored;
				otherFlavorPlayerPresent = std::filesystem::exists(
					otherFlavorPlayerPath, ignored);
				webPlayerPresent = std::filesystem::exists(
					ORKIGE_EDITOR_WEB_PLAYER_JS, ignored);
			}
			const bool desktopSelected = session.simulatorUdid.empty() &&
				session.androidSerial.empty() &&
				session.iosDeviceUdid.empty() && !session.browserTarget;
			if (ImGui::Selectable(ownFlavorLabel,
				desktopSelected && session.desktopPlayerPath.empty()))
			{
				session.desktopPlayerPath.clear();
				session.desktopLabel.clear();
				session.simulatorUdid.clear();
				session.simulatorLabel.clear();
				session.androidSerial.clear();
				session.androidLabel.clear();
				session.iosDeviceUdid.clear();
				session.iosDeviceLabel.clear();
				session.browserTarget = false;
			}
			// the other flavor's player: selectable only when its build
			// tree carries the binary (grey + tooltip otherwise - honest
			// gating over a Play that cannot work)
			ImGui::BeginDisabled(!otherFlavorPlayerPresent);
			if (ImGui::Selectable(otherFlavorLabel,
				desktopSelected && !session.desktopPlayerPath.empty()))
			{
				session.desktopPlayerPath = otherFlavorPlayerPath;
				session.desktopLabel = otherFlavorLabel;
				session.simulatorUdid.clear();
				session.simulatorLabel.clear();
				session.androidSerial.clear();
				session.androidLabel.clear();
				session.iosDeviceUdid.clear();
				session.iosDeviceLabel.clear();
				session.browserTarget = false;
			}
			ImGui::EndDisabled();
			if (!otherFlavorPlayerPresent &&
				ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			{
				ImGui::SetTooltip("that flavor's player is not built - "
					"configure + build its preset first (%s)",
					otherFlavorPlayerPath);
			}
			// the browser (WebGL): selectable once the web-release preset
			// built the wasm player. Play becomes an export-serve-open whose
			// page dials the debug link back in - a live session like
			// desktop play (a page that never connects runs standalone).
			ImGui::BeginDisabled(!webPlayerPresent);
			if (ImGui::Selectable("Browser (WebGL)", session.browserTarget))
			{
				session.browserTarget = true;
				session.desktopPlayerPath.clear();
				session.desktopLabel.clear();
				session.simulatorUdid.clear();
				session.simulatorLabel.clear();
				session.androidSerial.clear();
				session.androidLabel.clear();
				session.iosDeviceUdid.clear();
				session.iosDeviceLabel.clear();
			}
			ImGui::EndDisabled();
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			{
				ImGui::SetTooltip(webPlayerPresent
					? "Play exports the project for the web, serves it on a "
						"local port and opens your browser - the page "
						"connects the live debug session (remote logs, "
						"hierarchy, pause)"
					: "the wasm player is not built - configure + build the "
						"web-release preset first (%s)",
					ORKIGE_EDITOR_WEB_PLAYER_JS);
			}
#ifdef __APPLE__
			// every AVAILABLE simulator is a valid target: Play boots a
			// shutdown one automatically (state marked in the entry)
			for (SimulatorDevice const& device : availableSimulators)
			{
				const std::string entryLabel = device.booted
					? device.name
					: device.name + "  (not running - Play boots it)";
				if (ImGui::Selectable(
					(entryLabel + "##" + device.udid).c_str(),
					session.simulatorUdid == device.udid))
				{
					session.simulatorUdid = device.udid;
					session.simulatorLabel = device.name;
					session.androidSerial.clear();
					session.androidLabel.clear();
					session.desktopPlayerPath.clear();
					session.desktopLabel.clear();
					session.iosDeviceUdid.clear();
					session.iosDeviceLabel.clear();
					session.browserTarget = false;
				}
				if (!device.booted && ImGui::IsItemHovered())
				{
					ImGui::SetTooltip(
						"shut down - Play boots it (takes a moment)");
				}
			}
#endif
			for (AndroidDevice const& device : androidDevices)
			{
				if (ImGui::Selectable(
					(device.label + "##" + device.serial).c_str(),
					session.androidSerial == device.serial))
				{
					session.androidSerial = device.serial;
					session.androidLabel = device.label;
					session.simulatorUdid.clear();
					session.simulatorLabel.clear();
					session.desktopPlayerPath.clear();
					session.desktopLabel.clear();
					session.iosDeviceUdid.clear();
					session.iosDeviceLabel.clear();
					session.browserTarget = false;
				}
			}
#ifdef __APPLE__
			// iOS hardware (physical iPhone/iPad over USB): gated on iOS
			// signing (an Apple Developer identity + a provisioning profile -
			// codesignIdentityPresent above is isIosSigningConfigured()).
			// Selecting a device makes Play an export-and-deploy: build + sign
			// (Util/orkige_export.py --platform ios) + install + launch via
			// devicectl. It is NOT a live play session - a USB device shares
			// neither the host filesystem nor its loopback, and no dependency-
			// free CLI forwards a debug-port tunnel to it, so the game runs
			// standalone and the remote hierarchy/inspector stay unavailable
			// (the documented gap, Docs/ios-signing.md).
			if (!codesignIdentityPresent)
			{
				ImGui::BeginDisabled(true);
				ImGui::Selectable("iPhone/iPad (USB)");
				ImGui::EndDisabled();
				if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
				{
					ImGui::SetTooltip(
						"requires iOS signing configuration (an Apple "
						"Developer identity + a provisioning profile - see "
						"Docs/ios-signing.md)");
				}
			}
			for (IosHardwareDevice const& device : iosHardware)
			{
				if (ImGui::Selectable(
					(device.name + "##" + device.udid).c_str(),
					session.iosDeviceUdid == device.udid))
				{
					session.iosDeviceUdid = device.udid;
					session.iosDeviceLabel = device.name;
					session.simulatorUdid.clear();
					session.simulatorLabel.clear();
					session.androidSerial.clear();
					session.androidLabel.clear();
					session.desktopPlayerPath.clear();
					session.desktopLabel.clear();
					session.browserTarget = false;
				}
				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("Play builds + signs + installs + "
						"launches on this device (devicectl). It runs "
						"standalone - live debug over USB is unavailable (no "
						"debug-port tunnel; see Docs/ios-signing.md)");
				}
			}
#endif
			ImGui::EndCombo();
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		// ONE slot for Play/Stop: in edit mode it starts a session; while a
		// session runs the same button IS the stop control
		if (mode != PlaySession::Mode::Edit)
		{
			ImGui::BeginDisabled(mode == PlaySession::Mode::Stopping);
			if (transportButton("Stop", TransportIcon::Stop))
			{
				requestStopPlay(session);
			}
			ImGui::EndDisabled();
		}
		else if (ImGui::BeginDisabled(prefabMode), transportButton("Play", TransportIcon::Play))
		{
			if (!session.iosDeviceUdid.empty())
			{
				// Play on a connected iPhone/iPad is an export-and-deploy, not a
				// live play session: the frame loop runs an "ios" export whose
				// success installs + launches via devicectl (the deploy fields on
				// ExportJob carry it). Live debug over USB is the documented gap
				// (Docs/ios-signing.md).
				state.requestedIosDeviceDeployUdid = session.iosDeviceUdid;
				state.requestedIosDeviceDeployLabel = session.iosDeviceLabel;
			}
			else if (session.browserTarget)
			{
				// Play in Browser: the frame loop runs a "web" export whose
				// success serves the artifact + opens the default browser
				// (the deployBrowser continuation on ExportJob), then the
				// session WAITS for the page to dial the debug link in -
				// from there it is a live session like desktop play
				state.requestedBrowserPlay = true;
			}
			else
			{
				startPlay(session, gameObjectManager, state.project);
			}
		}
		if (mode == PlaySession::Mode::Edit)
		{
			ImGui::EndDisabled();
		}
		ImGui::SameLine();
		if (mode == PlaySession::Mode::Paused)
		{
			if (transportButton("Resume", TransportIcon::Play))
			{
				session.client.send(
					Orkige::DebugMessage(Protocol::MSG_RESUME));
				session.mode = PlaySession::Mode::Playing;
			}
		}
		else
		{
			ImGui::BeginDisabled(mode != PlaySession::Mode::Playing);
			if (transportButton("Pause", TransportIcon::Pause))
			{
				session.client.send(Orkige::DebugMessage(Protocol::MSG_PAUSE));
				session.mode = PlaySession::Mode::Paused;
			}
			ImGui::EndDisabled();
		}
		ImGui::SameLine();
		ImGui::BeginDisabled(mode != PlaySession::Mode::Paused);
		if (transportButton("Step", TransportIcon::Step))
		{
			session.client.send(Orkige::DebugMessage(Protocol::MSG_STEP));
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
		ImGui::SameLine();
		// the tool strip: Q/W/E/R, world/local space, snap toggle - the
		// buttons call the exact functions the keyboard shortcuts invoke
		ImGui::BeginDisabled(session.isActive());
		// each tool button shows an FA glyph (falling back to its shortcut
		// letter when the icon font is unavailable) and a "<Name> (<Key>)"
		// tooltip - the letter is the keyboard shortcut, always named
		auto toolButton = [&core](char const* glyph, char const* letter,
			char const* name, Orkige::EditorTool tool)
		{
			const bool active = (core.getActiveTool() == tool);
			if (active)
			{
				ImGui::PushStyleColor(ImGuiCol_Button,
					ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
			}
			ImGui::PushID(letter);
			if (ImGui::Button(Orkige::editorIconFont() ? glyph : letter))
			{
				core.setActiveTool(tool);
			}
			ImGui::PopID();
			if (active)
			{
				ImGui::PopStyleColor();
			}
			ImGui::SetItemTooltip("%s (%s)", name, letter);
			ImGui::SameLine();
		};
		toolButton(ICON_FA_ARROW_POINTER, "Q", "Select",
			Orkige::EditorTool::Select);
		toolButton(ICON_FA_ARROWS_UP_DOWN_LEFT_RIGHT, "W", "Translate",
			Orkige::EditorTool::Translate);
		toolButton(ICON_FA_ROTATE, "E", "Rotate", Orkige::EditorTool::Rotate);
		toolButton(ICON_FA_UP_RIGHT_AND_DOWN_LEFT_FROM_CENTER, "R", "Scale",
			Orkige::EditorTool::Scale);
		toolButton(ICON_FA_HAND, "H", "Hand", Orkige::EditorTool::Hand);
		// Paint (B): 2D grid painting, usable once a prefab is armed in the
		// Tile Palette - the button greys out until then (and while a prefab
		// stage is open: paint places ROOT-level grid objects, incompatible
		// with the stage's single-root contract)
		{
			const bool armed = !state.tilePalette.armedAssetPath.empty() &&
				!prefabMode;
			const bool active =
				(core.getActiveTool() == Orkige::EditorTool::Paint);
			ImGui::BeginDisabled(!armed);
			if (active)
			{
				ImGui::PushStyleColor(ImGuiCol_Button,
					ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
			}
			ImGui::PushID("B");
			if (ImGui::Button(Orkige::editorIconFont()
				? ICON_FA_PAINTBRUSH : "B"))
			{
				core.setActiveTool(Orkige::EditorTool::Paint);
			}
			ImGui::PopID();
			if (active)
			{
				ImGui::PopStyleColor();
			}
			ImGui::EndDisabled();
			ImGui::SetItemTooltip(armed ? "Paint (B) - paint on the grid (2D)"
				: "Paint (B) - arm a tile in the Tile Palette first");
			ImGui::SameLine();
		}
		if (ImGui::Button(core.getTransformSpace() ==
			Orkige::EditorTransformSpace::World ? "World" : "Local"))
		{
			core.toggleTransformSpace();
		}
		ImGui::SameLine();
		// 2D/3D view toggle: flips the Scene viewport between the
		// orthographic XY-plane 2D mode and the orbit 3D view. Persisted like
		// the other view flags; a pure view feature (no command-stack change)
		if (gViewSettings)
		{
			const bool is2D = gViewSettings->editor2D;
			if (is2D)
			{
				ImGui::PushStyleColor(ImGuiCol_Button,
					ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
			}
			if (ImGui::Button(is2D ? "2D" : "3D"))
			{
				gViewSettings->editor2D = !gViewSettings->editor2D;
				gViewSettings->save();
			}
			if (is2D)
			{
				ImGui::PopStyleColor();
			}
			ImGui::SetItemTooltip(
				"toggle 2D (orthographic, XY plane) / 3D orbit view");
			ImGui::SameLine();
		}
		bool snapEnabled = core.isSnapEnabled();
		if (Orkige::compactCheckbox("Snap", &snapEnabled))
		{
			core.setSnapEnabled(snapEnabled);
			if (gViewSettings)
			{
				gViewSettings->snapEnabled = snapEnabled;
				gViewSettings->save();
			}
		}
		ImGui::SameLine();
		// the current steps double as the button into the snap-settings
		// popover (editable values, the snap settings)
		char snapLabel[64];
		SDL_snprintf(snapLabel, sizeof(snapLabel),
			"%.2g / %.2g\xC2\xB0 / %.2g###SnapSettings",
			core.getSnapTranslate(), core.getSnapRotateDegrees(),
			core.getSnapScale());
		if (ImGui::SmallButton(snapLabel))
		{
			ImGui::OpenPopup("##SnapSettingsPopover");
		}
		ImGui::SetItemTooltip("snap step settings (move / rotate / scale)");
		if (ImGui::BeginPopup("##SnapSettingsPopover"))
		{
			float snapTranslate = core.getSnapTranslate();
			float snapRotate = core.getSnapRotateDegrees();
			float snapScale = core.getSnapScale();
			bool snapEdited = false;
			ImGui::TextDisabled("Snap Steps");
			ImGui::SetNextItemWidth(120.0f);
			snapEdited |= ImGui::DragFloat("Move", &snapTranslate,
				0.05f, 0.001f, 100.0f, "%.3f");
			ImGui::SetNextItemWidth(120.0f);
			snapEdited |= ImGui::DragFloat("Rotate", &snapRotate,
				0.5f, 0.1f, 180.0f, "%.1f\xC2\xB0");
			ImGui::SetNextItemWidth(120.0f);
			snapEdited |= ImGui::DragFloat("Scale", &snapScale,
				0.01f, 0.001f, 10.0f, "%.3f");
			if (ImGui::MenuItem("Reset to Defaults"))
			{
				snapTranslate = Orkige::EditorCore::SNAP_TRANSLATE;
				snapRotate = Orkige::EditorCore::SNAP_ROTATE_DEGREES;
				snapScale = Orkige::EditorCore::SNAP_SCALE;
				snapEdited = true;
			}
			if (snapEdited)
			{
				core.setSnapValues(snapTranslate, snapRotate, snapScale);
				if (gViewSettings)
				{
					// persist the CLAMPED values EditorCore actually uses
					gViewSettings->snapTranslate = core.getSnapTranslate();
					gViewSettings->snapRotateDegrees =
						core.getSnapRotateDegrees();
					gViewSettings->snapScale = core.getSnapScale();
					gViewSettings->save();
				}
			}
			ImGui::EndPopup();
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
		ImGui::SameLine();
		switch (mode)
		{
		case PlaySession::Mode::Edit:
			// Play in Browser is a deploy-and-run (no live session, the page
			// runs standalone), so the editor STAYS in edit mode - this
			// status is the visible trace of that flow: the export progress,
			// the served URL (a click re-opens the browser - the recovery
			// when the first open failed silently), or the failure pointer
			if (state.browserPlayStatus == "exporting")
			{
				ImGui::TextUnformatted("web: exporting...");
			}
			else if (state.browserPlayStatus == "failed")
			{
				ImGui::PushStyleColor(ImGuiCol_Text,
					Orkige::editorErrorTextColor());
				ImGui::TextUnformatted("web: export failed - see Console");
				ImGui::PopStyleColor();
			}
			else if (state.browserPlayStatus == "serving" &&
				!state.browserPlayUrl.empty())
			{
				if (ImGui::SmallButton(
					("web: " + state.browserPlayUrl).c_str()))
				{
					SDL_OpenURL(state.browserPlayUrl.c_str());
				}
				ImGui::SetItemTooltip("the exported game is served at this "
					"address (it runs standalone in the page - the editor "
					"stays in edit mode); click to open the browser again");
			}
			else
			{
				ImGui::TextDisabled(prefabMode ? "editing prefab" : "editing");
			}
			break;
		case PlaySession::Mode::Building:
			// compile-on-Play: the Console carries the [build] output
			ImGui::TextUnformatted(session.launchStatus.empty()
				? "building..." : session.launchStatus.c_str());
			break;
		case PlaySession::Mode::Launching:
			// the simulator prep pipeline reports its phase here
			// (booting / installing / launching)
			ImGui::TextUnformatted(session.launchStatus.empty()
				? "launching player..." : session.launchStatus.c_str());
			break;
		case PlaySession::Mode::Playing:
			ImGui::Text("PLAYING (remote: %zu objects)",
				session.remoteHierarchy.size());
			break;
		case PlaySession::Mode::Paused:
			ImGui::Text("PAUSED (remote: %zu objects)",
				session.remoteHierarchy.size());
			break;
		case PlaySession::Mode::Stopping:
			ImGui::TextUnformatted("stopping...");
			break;
		}
		// script failures must be loud: a red marker next to the play status
		// while any script error is known in the current session (fed by the
		// player's script_error messages; cleared on Stop / a new session)
		if (session.isActive() && !session.scriptErrorIds.empty())
		{
			ImGui::SameLine();
			ImGui::TextColored(ImVec4(0.94f, 0.35f, 0.35f, 1.0f),
				"%zu script error%s - see Console",
				session.scriptErrorIds.size(),
				session.scriptErrorIds.size() == 1 ? "" : "s");
		}
	}
	ImGui::End();
	ImGui::PopStyleColor();	// chrome WindowBg
	return toolbarHeight;
}
