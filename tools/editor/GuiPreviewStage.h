/********************************************************************
	created:	Saturday 2026/07/12 at 12:00
	filename: 	GuiPreviewStage.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __GuiPreviewStage_h__12_7_2026__12_00_00__
#define __GuiPreviewStage_h__12_7_2026__12_00_00__

//! @file GuiPreviewStage.h
//! @brief the editor's GUI Preview stage: a REAL, isolated gui stack
//! (GuiFactory + GuiManager + UiRenderer) rendered into an offscreen
//! RenderTexture at a SIMULATED device context (resolution, content
//! scale, safe-area insets). ONE stage instance is shared by the GUI
//! Preview tab (the human's live view) and the preview_ui MCP verb (an
//! agent's screenshots) - the collaborative UI design loop. It is never
//! the running game's gui: the game runs in a separate player process;
//! this gui lives in the editor process and only exists while a screen is
//! previewed.

#include <core_util/optr.h>
#include <core_util/SafeArea.h>
#include <engine_gui/GuiManager.h>

#include <string>
#include <vector>

namespace Orkige
{
	class RenderTexture;
	class GuiFactory;
	class StringTable;
	class Project;
}

namespace OrkigeEditor
{
	//! @brief the simulated device the preview renders at
	struct GuiPreviewContext
	{
		unsigned int		width = 1179;	//!< device width in pixels (phone portrait default)
		unsigned int		height = 2556;	//!< device height in pixels
		float				contentScale = 3.0f;	//!< display density (1/2/3x)
		Orkige::SafeAreaInsets	insets;		//!< notch/home-bar insets (pixels)

		bool operator==(GuiPreviewContext const& other) const
		{
			return width == other.width && height == other.height &&
				contentScale == other.contentScale &&
				insets.mLeft == other.insets.mLeft &&
				insets.mTop == other.insets.mTop &&
				insets.mRight == other.insets.mRight &&
				insets.mBottom == other.insets.mBottom;
		}
		bool operator!=(GuiPreviewContext const& other) const
		{
			return !(*this == other);
		}
	};

	//! @brief a resolved widget rect the preview readback returns (mirrors
	//! GuiManager::WidgetLayout but decoupled from the include for callers)
	struct GuiPreviewWidgetRect
	{
		std::string	id;
		float		left = 0, top = 0, width = 0, height = 0;
		bool		visible = true, enabled = true, modal = false;
	};

	//! @brief the shared preview stage (@see the file comment)
	class GuiPreviewStage
	{
	public:
		GuiPreviewStage();
		~GuiPreviewStage();

		//! @brief set the simulated device context. A change tears down and
		//! rebuilds the stage on the next show() (content scale is baked at
		//! gui construction). @return true when the context actually changed.
		bool setContext(GuiPreviewContext const& context);
		GuiPreviewContext const& getContext() const { return this->mContext; }

		//! @brief load the open project's localisation directory (manifest
		//! Settings "localisation", config-asset convention) into the stage's
		//! OWN StringTable so a previewed screen's `@key` captions resolve. The
		//! editor process otherwise has no game StringTable; the stage owns the
		//! process singleton for its lifetime (an empty table just echoes keys,
		//! the pre-localisation behaviour). Idempotent: reloads only when the
		//! project or its directory changes - a language switch is table-resident
		//! (no I/O). A project with no `localisation` setting clears the table
		//! (source/none only). @see setPreviewLanguage.
		void loadLocalisation(Orkige::Project const& project);
		//! @brief every loaded language code, sorted (the source language
		//! included); empty when the project has no loc/ directory. Feeds the
		//! GUI Preview tab's language combo and the preview_ui `languages` field.
		std::vector<std::string> getLanguages() const;
		//! the loaded source-language code ("" when no loc/ directory is loaded)
		std::string getSourceLanguage() const;
		//! @brief set the language the preview resolves `@keys` in; a change is
		//! applied to the stage's StringTable immediately, and the next show()
		//! re-resolves the screen against it (so the panel/verb re-show to make a
		//! switch live). An empty tag (or the source code) previews the SOURCE
		//! text. Unknown tags are the caller's to reject (getLanguages lists the
		//! valid set).
		void setPreviewLanguage(std::string const& language);
		//! the active preview language ("" = source language)
		std::string const& getPreviewLanguage() const
		{
			return this->mPreviewLanguage;
		}

		//! @brief build the stage (if needed) and load a project-relative
		//! `.oui` into it, replacing whatever was shown. An empty ouiRelPath
		//! clears the stage (the empty state). Registers the file's directory
		//! in the project resource group so the layout + its atlas resolve.
		//! @return false + a message in getLastError() on failure (no such
		//! file, the atlas is missing, a parse error).
		bool show(std::string const& projectRoot, std::string const& ouiRelPath,
			std::string& outError);
		//! @brief drop the loaded screen and the gui stack (the empty state);
		//! keeps the current context for the next show()
		void clear();

		//! @brief tick the gui one frame so it resolves layout and resubmits
		//! its batch into the offscreen target (call once per editor frame
		//! while the tab is visible). A no-op when nothing is loaded.
		void tick(float deltaSeconds);

		//! @brief force one render of the preview target and write it to a PNG
		//! (the preview_ui verb / a headless screenshot). Renders synchronously
		//! so the file reflects the CURRENT layout. @return false + a message
		//! when nothing is loaded or the write fails.
		bool renderAndCapture(std::string const& pngPath, std::string& outError);

		//! is a screen currently loaded and rendering?
		bool isLoaded() const;
		//! the offscreen target the gui composites into (null until built) -
		//! the tab shows this inside an ImGui image
		Orkige::optr<Orkige::RenderTexture> getTarget() const { return this->mTarget; }
		//! the project-relative path of the loaded screen ("" when none)
		std::string const& getLoadedFile() const { return this->mLoadedFile; }
		//! the resolved widget rects (pixels, in the simulated surface space)
		std::vector<GuiPreviewWidgetRect> getWidgetRects() const;
		//! the gui's draw-batch count after the last tick (> 0 proves the
		//! screen actually submitted geometry - the "did it render?" probe)
		size_t getLastBatchCount() const;
		//! the last failure message (set by show/renderAndCapture)
		std::string const& getLastError() const { return this->mLastError; }

	private:
		//! (re)create the offscreen target + a fresh gui stack for the current
		//! context, using @p atlas as the default atlas. Returns false + sets
		//! mLastError when the target/atlas cannot be created.
		bool rebuild(std::string const& atlas);
		//! tear down the gui stack and the target
		void teardown();
		//! peek the `[Layout] atlas = ...` line of a `.oui` (default gui_default)
		static std::string peekAtlas(std::string const& absOuiPath);
		//! apply mPreviewLanguage to the stage's StringTable (empty => the source
		//! language, so keys resolve to source text without miss-logging)
		void applyPreviewLanguage();

		GuiPreviewContext					mContext;
		Orkige::optr<Orkige::RenderTexture>	mTarget;
		Orkige::optr<Orkige::GuiFactory>	mFactory;
		Orkige::optr<Orkige::GuiManager>	mGui;
		std::string							mAtlas;			//!< the atlas the gui was built with
		std::string							mLoadedFile;	//!< project-relative .oui path ("" = none)
		std::string							mLastError;
		//! the stage's own localisation table (the editor has no game one); its
		//! ctor makes it the process StringTable singleton so GuiFactory's `@key`
		//! routing resolves against it. Empty until loadLocalisation loads a
		//! project's loc/ directory.
		Orkige::optr<Orkige::StringTable>	mStringTable;
		//! signature (root|absDir) of the localisation directory currently loaded,
		//! so loadLocalisation reloads only on a real project/directory change
		std::string							mLocSignature;
		//! the previewed language ("" = source language); applied before @keys
		//! resolve on show()
		std::string							mPreviewLanguage;
		//! directories registered in the project group for this stage's files;
		//! kept so a project switch can be detected (rebuild frees the gui, the
		//! locations stay - they are the project's own, idempotently re-added)
		bool								mContextDirty;	//!< context changed, rebuild on next show
	};
}

#endif //__GuiPreviewStage_h__12_7_2026__12_00_00__
