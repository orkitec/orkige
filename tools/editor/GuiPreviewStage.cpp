/********************************************************************
	created:	Saturday 2026/07/12 at 12:00
	filename: 	GuiPreviewStage.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

//! @file GuiPreviewStage.cpp
//! @brief the shared editor GUI Preview stage (@see GuiPreviewStage.h)

#include "GuiPreviewStage.h"

#include <engine_render/RenderSystem.h>
#include <engine_render/RenderTexture.h>
#include <engine_gui/GuiFactory.h>
#include <core_project/Project.h>
#include <core_util/StringTable.h>

#include <fstream>
#include <filesystem>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace OrkigeEditor
{
	using namespace Orkige;

	namespace
	{
		//! the offscreen target's backend name (one per editor - the stage is
		//! a singleton in practice; the gui is a process Singleton anyway)
		const char* PREVIEW_TARGET_NAME = "EditorGuiPreviewRT";
		//! a neutral device-frame background so light and dark UI both read
		const Color PREVIEW_BACKGROUND(0.14f, 0.15f, 0.17f, 1.0f);
	}

	//---------------------------------------------------------
	GuiPreviewStage::GuiPreviewStage()
		: mContextDirty(true)
	{
		// own the process StringTable singleton for the editor's lifetime: the
		// editor has no game string table, so the previewed gui's `@key` captions
		// resolve through this one. Empty until a project's loc/ directory loads,
		// which just echoes keys back - the pre-localisation preview behaviour.
		this->mStringTable = onew(new StringTable());
	}
	//---------------------------------------------------------
	GuiPreviewStage::~GuiPreviewStage()
	{
		this->teardown();
	}
	//---------------------------------------------------------
	bool GuiPreviewStage::setContext(GuiPreviewContext const& context)
	{
		if(this->mContext == context)
		{
			return false;
		}
		this->mContext = context;
		// content scale is baked at gui construction and the target size is
		// fixed at creation, so a context change forces a full rebuild
		this->mContextDirty = true;
		return true;
	}
	//---------------------------------------------------------
	void GuiPreviewStage::loadLocalisation(Project const& project)
	{
		// resolve the project's localisation directory (manifest Settings
		// "localisation", config-asset convention). A signature of root + the
		// resolved directory detects a real change so we reload only then.
		std::string absDir;
		std::string signature;
		if(project.isLoaded())
		{
			const std::string ref = project.getSetting(
				StringTable::LOCALISATION_SETTING_KEY);
			if(!ref.empty())
			{
				absDir = project.resolvePath(ref);
				signature = project.getRootDirectory() + "|" + absDir;
			}
		}
		if(signature == this->mLocSignature)
		{
			return;	// same project + directory already loaded (or both empty)
		}
		this->mLocSignature = signature;
		this->mStringTable->clear();
		if(!absDir.empty())
		{
			// a missing/broken directory just leaves the table empty (keys echo)
			this->mStringTable->loadXliffDirectory(absDir);
		}
		this->applyPreviewLanguage();
	}
	//---------------------------------------------------------
	std::vector<std::string> GuiPreviewStage::getLanguages() const
	{
		return this->mStringTable ? this->mStringTable->getLanguages()
			: std::vector<std::string>();
	}
	//---------------------------------------------------------
	std::string GuiPreviewStage::getSourceLanguage() const
	{
		return this->mStringTable ? this->mStringTable->getSourceLanguage()
			: std::string();
	}
	//---------------------------------------------------------
	void GuiPreviewStage::setPreviewLanguage(std::string const& language)
	{
		this->mPreviewLanguage = language;
		this->applyPreviewLanguage();
	}
	//---------------------------------------------------------
	void GuiPreviewStage::applyPreviewLanguage()
	{
		if(!this->mStringTable)
		{
			return;
		}
		// an empty preview language previews the source language directly, so a
		// source-text lookup hits the source table (no once-per-key miss log)
		const std::string language = this->mPreviewLanguage.empty()
			? this->mStringTable->getSourceLanguage()
			: this->mPreviewLanguage;
		this->mStringTable->setLanguage(language);
	}
	//---------------------------------------------------------
	std::string GuiPreviewStage::peekAtlas(std::string const& absOuiPath)
	{
		std::ifstream file(absOuiPath);
		if(!file)
		{
			return "gui_default";
		}
		std::string line;
		bool inLayout = false;
		bool sawSection = false;
		while(std::getline(file, line))
		{
			// strip a trailing CR and surrounding whitespace
			while(!line.empty() && (line.back() == '\r' || line.back() == ' ' ||
				line.back() == '\t'))
			{
				line.pop_back();
			}
			size_t start = line.find_first_not_of(" \t");
			if(start == std::string::npos)
			{
				continue;
			}
			std::string trimmed = line.substr(start);
			if(trimmed[0] == '#')
			{
				continue;
			}
			if(trimmed[0] == '[')
			{
				// the [Layout] section is the first one; once a widget section
				// starts, an unset atlas defaults to gui_default
				inLayout = (trimmed.rfind("[Layout", 0) == 0);
				sawSection = true;
				continue;
			}
			if(inLayout || !sawSection)
			{
				const size_t eq = trimmed.find('=');
				if(eq != std::string::npos)
				{
					std::string key = trimmed.substr(0, eq);
					while(!key.empty() && (key.back() == ' ' || key.back() == '\t'))
					{
						key.pop_back();
					}
					if(key == "atlas")
					{
						std::string value = trimmed.substr(eq + 1);
						size_t vs = value.find_first_not_of(" \t");
						if(vs != std::string::npos)
						{
							value = value.substr(vs);
							return value;
						}
					}
				}
			}
			if(sawSection && !inLayout)
			{
				break;	// past the [Layout] block, no atlas declared
			}
		}
		return "gui_default";
	}
	//---------------------------------------------------------
	bool GuiPreviewStage::rebuild(std::string const& atlas)
	{
		this->teardown();
		RenderSystem* render = RenderSystem::get();
		if(!render)
		{
			this->mLastError = "no render system";
			return false;
		}
		if(!render->supports(RenderCaps::OffscreenOwnedLayers))
		{
			this->mLastError = "offscreen 2D composition is not supported on "
				"this render backend (classic OGRE) - the GUI Preview is "
				"Ogre-Next only";
			return false;
		}
		// a UI-only offscreen target at the simulated device resolution: no 3D
		// camera, just a clear + the gui's own 2D layers (createLayer)
		this->mTarget = render->createRenderTexture(PREVIEW_TARGET_NAME,
			this->mContext.width, this->mContext.height);
		if(!this->mTarget)
		{
			this->mLastError = "could not create the preview render target";
			return false;
		}
		this->mTarget->setBackgroundColour(PREVIEW_BACKGROUND);

		GuiManager::PreviewSurface surface;
		surface.target = this->mTarget;
		surface.width = this->mContext.width;
		surface.height = this->mContext.height;
		surface.safeArea = this->mContext.insets;
		surface.contentScale = this->mContext.contentScale;

		this->mFactory = onew(new GuiFactory());
		// the gui's views load their atlases from the open project's group; the
		// ctor pre-creates the default-atlas view, so the caller verified the
		// atlas resolves before we reach here
		this->mGui = onew(new GuiManager(this->mFactory, atlas,
			Project::RESOURCE_GROUP_NAME, &surface));
		this->mAtlas = atlas;
		this->mContextDirty = false;
		return true;
	}
	//---------------------------------------------------------
	void GuiPreviewStage::teardown()
	{
		// order: the gui's screens hold DrawLayer2D handles that reference the
		// target's layers, so the gui dies before the target
		this->mGui.reset();
		this->mFactory.reset();
		this->mTarget.reset();
		this->mAtlas.clear();
	}
	//---------------------------------------------------------
	bool GuiPreviewStage::show(std::string const& projectRoot,
		std::string const& ouiRelPath, std::string& outError)
	{
		this->mLastError.clear();
		if(ouiRelPath.empty())
		{
			this->clear();
			return true;
		}
		if(projectRoot.empty())
		{
			this->mLastError = "no project open";
			outError = this->mLastError;
			return false;
		}
		namespace fs = std::filesystem;
		const fs::path absOui = fs::path(projectRoot) / ouiRelPath;
		std::error_code ec;
		if(!fs::is_regular_file(absOui, ec))
		{
			this->mLastError = "no such layout file: " + ouiRelPath;
			outError = this->mLastError;
			return false;
		}
		RenderSystem* render = RenderSystem::get();
		if(!render)
		{
			this->mLastError = "no render system";
			outError = this->mLastError;
			return false;
		}
		// register the file's directory in the project group so the layout (and
		// a freshly written one) resolves by bare name. Idempotent - the same
		// remove-then-add the editor uses for project asset locations.
		const std::string dir = absOui.parent_path().string();
		render->removeResourceLocation(dir, Project::RESOURCE_GROUP_NAME);
		render->addResourceLocation(dir, RenderSystem::LT_FILESYSTEM,
			Project::RESOURCE_GROUP_NAME, false);

		const std::string atlas = peekAtlas(absOui.string());
		if(!render->resourceExists(atlas + ".ogui", Project::RESOURCE_GROUP_NAME))
		{
			this->mLastError = "the layout's atlas '" + atlas +
				"' was not found in the project (expected " + atlas +
				".ogui under assets/)";
			outError = this->mLastError;
			return false;
		}

		// (re)build the stage when nothing is up, the context changed, or the
		// atlas differs from what the gui was built with
		if(!this->mGui || this->mContextDirty || this->mAtlas != atlas)
		{
			if(!this->rebuild(atlas))
			{
				outError = this->mLastError;
				return false;
			}
		}
		else
		{
			// reuse the gui stack: drop the previous screen's widgets/views
			// (keep the default atlas view) so the new layout starts clean
			this->mGui->destroyAllWidgets();
			this->mGui->destroyAllViews(true);
		}

		// make the previewed language current so loadLayout's `@key` captions
		// resolve against the right table (a language switch re-shows the screen)
		this->applyPreviewLanguage();
		try
		{
			const std::string bare = absOui.filename().string();
			this->mFactory->loadLayout(bare);
		}
		catch(std::exception const& e)
		{
			this->mLastError = std::string("failed to load the layout: ") +
				e.what();
			outError = this->mLastError;
			this->mLoadedFile.clear();
			return false;
		}
		this->mLoadedFile = ouiRelPath;
		// resolve + submit once so getWidgetRects / a screenshot are valid
		// immediately (before the next editor frame ticks it)
		this->mGui->profileTick(0.0f);
		return true;
	}
	//---------------------------------------------------------
	void GuiPreviewStage::clear()
	{
		this->teardown();
		this->mLoadedFile.clear();
	}
	//---------------------------------------------------------
	void GuiPreviewStage::tick(float deltaSeconds)
	{
		if(this->mGui)
		{
			this->mGui->profileTick(deltaSeconds);
		}
	}
	//---------------------------------------------------------
	bool GuiPreviewStage::renderAndCapture(std::string const& pngPath,
		std::string& outError)
	{
		if(!this->isLoaded() || !this->mTarget)
		{
			this->mLastError = "nothing is loaded in the preview";
			outError = this->mLastError;
			return false;
		}
		// submit the current layout, render the backend's FRAME DEPTH so the
		// offscreen target holds the CURRENT geometry, then read it back:
		// freshly rewritten 2D batches settle within up to 3 frames where the
		// vertex buffers are multi-buffered (the DrawLayer2D visibility
		// contract) - a single frame can capture the previous layout. The MCP
		// pump runs before the editor's own NewFrame/renderOneFrame, so the
		// extra renders here are safe.
		this->mGui->profileTick(0.0f);
		for(int settleFrame = 0; settleFrame < 3; ++settleFrame)
		{
			RenderSystem::get()->renderOneFrame();
		}
		try
		{
			this->mTarget->writeContentsToFile(pngPath);
		}
		catch(std::exception const& e)
		{
			this->mLastError = std::string("could not write the screenshot: ") +
				e.what();
			outError = this->mLastError;
			return false;
		}
		return true;
	}
	//---------------------------------------------------------
	bool GuiPreviewStage::isLoaded() const
	{
		return this->mGui != NULL && !this->mLoadedFile.empty();
	}
	//---------------------------------------------------------
	size_t GuiPreviewStage::getLastBatchCount() const
	{
		return this->mGui ? this->mGui->getLastBatchCount() : 0;
	}
	//---------------------------------------------------------
	std::vector<GuiPreviewWidgetRect> GuiPreviewStage::getWidgetRects() const
	{
		std::vector<GuiPreviewWidgetRect> out;
		if(!this->mGui)
		{
			return out;
		}
		std::vector<GuiManager::WidgetLayout> layouts =
			this->mGui->getWidgetLayouts();
		out.reserve(layouts.size());
		for(GuiManager::WidgetLayout const& layout : layouts)
		{
			GuiPreviewWidgetRect rect;
			rect.id = layout.id;
			rect.left = layout.left;
			rect.top = layout.top;
			rect.width = layout.width;
			rect.height = layout.height;
			rect.visible = layout.visible;
			rect.enabled = layout.enabled;
			rect.modal = layout.modal;
			out.push_back(rect);
		}
		return out;
	}
}
