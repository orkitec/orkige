// EditorSettings.cpp - ViewSettings persistence (orkige_editor_view.ini:
// grid/camera-feel/panel-visibility/snap/recents) plus the editor-wide
// globals. Split out of main.cpp (mechanical decomposition, see EditorApp.h).
#include "EditorApp.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>

void ViewSettings::load()
{
	std::ifstream file(this->path);
	std::string line;
	while (std::getline(file, line))
	{
		const std::size_t equals = line.find('=');
		if (equals == std::string::npos)
		{
			continue;
		}
		const std::string key = line.substr(0, equals);
		const std::string value = line.substr(equals + 1);
		if (key == "show_grid")
		{
			this->showGrid = (value == "1");
		}
		else if (key == "show_view_gizmo")
		{
			this->showViewGizmo = (value == "1");
		}
		else if (key == "mode_2d")
		{
			this->editor2D = (value == "1");
		}
		else if (key == "orbit_speed")
		{
			this->orbitSpeed = std::strtof(value.c_str(), nullptr);
		}
		else if (key == "look_speed")
		{
			this->lookSpeed = std::strtof(value.c_str(), nullptr);
		}
		else if (key == "zoom_speed")
		{
			this->zoomSpeed = std::strtof(value.c_str(), nullptr);
		}
		else if (key == "fly_speed")
		{
			this->flySpeed = std::strtof(value.c_str(), nullptr);
		}
		else if (key == "fov_deg")
		{
			this->fovDeg = std::strtof(value.c_str(), nullptr);
		}
		else if (key == "panel_hierarchy")
		{
			this->showHierarchyPanel = (value == "1");
		}
		else if (key == "panel_inspector")
		{
			this->showInspectorPanel = (value == "1");
		}
		else if (key == "panel_console")
		{
			this->showConsolePanel = (value == "1");
		}
		else if (key == "panel_stats")
		{
			this->showStatsPanel = (value == "1");
		}
		else if (key == "panel_scene")
		{
			this->showScenePanel = (value == "1");
		}
		else if (key == "panel_assets")
		{
			this->showAssetBrowserPanel = (value == "1");
		}
		else if (key == "panel_tilepalette")
		{
			this->showTilePalettePanel = (value == "1");
		}
		else if (key == "panel_gui_preview")
		{
			this->showGuiPreviewPanel = (value == "1");
		}
		else if (key == "snap_enabled")
		{
			this->snapEnabled = (value == "1");
		}
		else if (key == "snap_translate")
		{
			this->snapTranslate = std::strtof(value.c_str(), nullptr);
		}
		else if (key == "snap_rotate_deg")
		{
			this->snapRotateDegrees = std::strtof(value.c_str(), nullptr);
		}
		else if (key == "snap_scale")
		{
			this->snapScale = std::strtof(value.c_str(), nullptr);
		}
		else if (key == "asset_thumb_size")
		{
			this->assetThumbnailSize = std::strtof(value.c_str(), nullptr);
		}
		else if (key == "reopen_last_project")
		{
			this->reopenLastProject = (value == "1");
		}
		else if (key == "external_editor")
		{
			this->externalEditor = value;
		}
		else if (key == "theme_mode")
		{
			// "system" (default) / "dark" / "light"
			this->themeMode = (value == "dark") ? Orkige::EditorThemeMode::Dark
				: (value == "light") ? Orkige::EditorThemeMode::Light
				: Orkige::EditorThemeMode::System;
		}
		else if (key == "layout_content_scale")
		{
			this->layoutContentScale = std::strtof(value.c_str(), nullptr);
		}
		else if (key == "gui_preview_language")
		{
			this->guiPreviewLanguage = value;
		}
		else if (key == "recent_scene")
		{
			// one line per entry, newest first (the save order)
			if (!value.empty() &&
				this->recentScenes.size() < MAX_RECENT_SCENES)
			{
				this->recentScenes.push_back(value);
			}
		}
		else if (key == "recent_project")
		{
			if (!value.empty() &&
				this->recentProjects.size() < MAX_RECENT_PROJECTS)
			{
				this->recentProjects.push_back(value);
			}
		}
	}
	// keep loaded values inside the UI's ranges (a hand-edited file must
	// not wedge the camera)
	this->orbitSpeed = std::clamp(this->orbitSpeed, 0.05f, 2.0f);
	this->lookSpeed = std::clamp(this->lookSpeed, 0.02f, 1.0f);
	this->zoomSpeed = std::clamp(this->zoomSpeed, 0.1f, 3.0f);
	this->flySpeed = std::clamp(this->flySpeed,
		Orkige::FLY_SPEED_MIN, Orkige::FLY_SPEED_MAX);
	this->fovDeg = std::clamp(this->fovDeg, 20.0f, 120.0f);
	// same clamping rule EditorCore::setSnapValues applies (a zero step
	// from a hand-edited file must not freeze the gizmo)
	this->snapTranslate = std::max(this->snapTranslate, 0.001f);
	this->snapRotateDegrees = std::max(this->snapRotateDegrees, 0.1f);
	this->snapScale = std::max(this->snapScale, 0.001f);
	this->assetThumbnailSize = std::clamp(this->assetThumbnailSize,
		AssetBrowserState::THUMBNAIL_MIN, AssetBrowserState::THUMBNAIL_MAX);
}

void ViewSettings::save() const
{
	if (this->path.empty())
	{
		return;
	}
	std::ofstream file(this->path, std::ios::trunc);
	file << "show_grid=" << (this->showGrid ? 1 : 0) << "\n"
		<< "show_view_gizmo=" << (this->showViewGizmo ? 1 : 0) << "\n"
		<< "mode_2d=" << (this->editor2D ? 1 : 0) << "\n"
		<< "orbit_speed=" << this->orbitSpeed << "\n"
		<< "look_speed=" << this->lookSpeed << "\n"
		<< "zoom_speed=" << this->zoomSpeed << "\n"
		<< "fly_speed=" << this->flySpeed << "\n"
		<< "fov_deg=" << this->fovDeg << "\n"
		<< "panel_hierarchy=" << (this->showHierarchyPanel ? 1 : 0) << "\n"
		<< "panel_inspector=" << (this->showInspectorPanel ? 1 : 0) << "\n"
		<< "panel_console=" << (this->showConsolePanel ? 1 : 0) << "\n"
		<< "panel_stats=" << (this->showStatsPanel ? 1 : 0) << "\n"
		<< "panel_scene=" << (this->showScenePanel ? 1 : 0) << "\n"
		<< "panel_assets=" << (this->showAssetBrowserPanel ? 1 : 0) << "\n"
		<< "panel_tilepalette=" << (this->showTilePalettePanel ? 1 : 0) << "\n"
		<< "panel_gui_preview=" << (this->showGuiPreviewPanel ? 1 : 0) << "\n"
		<< "snap_enabled=" << (this->snapEnabled ? 1 : 0) << "\n"
		<< "snap_translate=" << this->snapTranslate << "\n"
		<< "snap_rotate_deg=" << this->snapRotateDegrees << "\n"
		<< "snap_scale=" << this->snapScale << "\n"
		<< "asset_thumb_size=" << this->assetThumbnailSize << "\n"
		<< "reopen_last_project="
		<< (this->reopenLastProject ? 1 : 0) << "\n"
		<< "external_editor=" << this->externalEditor << "\n"
		<< "theme_mode="
		<< (this->themeMode == Orkige::EditorThemeMode::Dark ? "dark"
			: this->themeMode == Orkige::EditorThemeMode::Light ? "light"
			: "system")
		<< "\n"
		<< "layout_content_scale=" << this->layoutContentScale << "\n"
		<< "gui_preview_language=" << this->guiPreviewLanguage << "\n";
	for (std::string const& recent : this->recentScenes)
	{
		file << "recent_scene=" << recent << "\n";
	}
	for (std::string const& recent : this->recentProjects)
	{
		file << "recent_project=" << recent << "\n";
	}
}

void ViewSettings::addRecentScene(std::string const& scenePath)
{
	if (scenePath.empty())
	{
		return;
	}
	this->recentScenes.erase(std::remove(this->recentScenes.begin(),
		this->recentScenes.end(), scenePath), this->recentScenes.end());
	this->recentScenes.insert(this->recentScenes.begin(), scenePath);
	if (this->recentScenes.size() > MAX_RECENT_SCENES)
	{
		this->recentScenes.resize(MAX_RECENT_SCENES);
	}
}

void ViewSettings::addRecentProject(std::string const& projectRoot)
{
	if (projectRoot.empty())
	{
		return;
	}
	this->recentProjects.erase(std::remove(this->recentProjects.begin(),
		this->recentProjects.end(), projectRoot),
		this->recentProjects.end());
	this->recentProjects.insert(this->recentProjects.begin(), projectRoot);
	if (this->recentProjects.size() > MAX_RECENT_PROJECTS)
	{
		this->recentProjects.resize(MAX_RECENT_PROJECTS);
	}
}

void ViewSettings::resetCameraAndDisplayDefaults()
{
	const ViewSettings defaults;
	this->showGrid = defaults.showGrid;
	this->showViewGizmo = defaults.showViewGizmo;
	this->orbitSpeed = defaults.orbitSpeed;
	this->lookSpeed = defaults.lookSpeed;
	this->zoomSpeed = defaults.zoomSpeed;
	this->flySpeed = defaults.flySpeed;
	this->fovDeg = defaults.fovDeg;
	this->assetThumbnailSize = defaults.assetThumbnailSize;
}

void ViewSettings::showAllPanels()
{
#define ORKIGE_SHOW_PANEL(id, label, visible, member) this->member = true;
	ORKIGE_EDITOR_PANEL_LIST(ORKIGE_SHOW_PANEL)
#undef ORKIGE_SHOW_PANEL
}

// the editor-wide globals (declared extern in EditorApp.h)
ViewSettings* gViewSettings = nullptr;
EditorState* gEditorState = nullptr;
Orkige::ImGuiFacadeRenderer* gImGuiRenderer = nullptr;
bool gRecordRecents = true;
bool gAutomatedRun = false;

//! record a scene path in the Open Recent list and persist it
void recordRecentScene(std::string const& scenePath)
{
	if (gViewSettings && gRecordRecents)
	{
		gViewSettings->addRecentScene(scenePath);
		gViewSettings->save();
	}
}

//! record a project root in the Open Recent Project list and persist it
void recordRecentProject(std::string const& projectRoot)
{
	if (gViewSettings && gRecordRecents)
	{
		gViewSettings->addRecentProject(projectRoot);
		gViewSettings->save();
	}
}
