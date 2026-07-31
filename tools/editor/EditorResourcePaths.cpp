/********************************************************************
	created:	Thursday 2026/07/30 at 09:00
	filename: 	EditorResourcePaths.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#include "EditorResourcePaths.h"

#include "core_util/PlatformUtil.h"
#include "core_debug/DebugMacros.h"

#include <cstdlib>
#include <filesystem>
#include <system_error>

namespace OrkigeEditor
{
	namespace
	{
		//! the bundle sub-path holding the engine media, relative to the
		//! resource root (mirrors what a project export writes)
		const char* const MEDIA_DIR_NAME = "Media";
		//! the bundle sub-path holding the engine's Python tools, relative to
		//! the resource root. It keeps the source tree's own directory name so
		//! a staged script finds its siblings under the same relative layout
		//! (the exporter imports the texture cook and the icon generator).
		const char* const PYTHON_TOOL_DIR_NAME = "Util";
		//! the packaged changelog the About box reads, at the resource root
		//! (the ONE spelling, shared with the packaging tool's CHANGELOG_FILE)
		const char* const CHANGELOG_FILE_NAME = "CHANGELOG.md";
		//! the browser payload a packaged editor carries (the wasm player pair,
		//! the shell page + data loader and the classic engine media): ONE
		//! self-contained directory at the resource root, which is what a web
		//! export packages from (@see ExportWeb.h)
		const char* const WEB_PAYLOAD_DIR_NAME = "web";
		//! the wasm module that MARKS a staged browser payload
		const char* const WEB_PLAYER_FILE_NAME = "orkige_player.wasm";
		//! the sibling executables the bundle carries, under their platform
		//! file names (the ONE place the bundle's tool naming lives)
#ifdef _WIN32
		const char* const PLAYER_FILE_NAME = "orkige_player.exe";
		const char* const TEXCOOK_FILE_NAME = "texcook.exe";
#else
		const char* const PLAYER_FILE_NAME = "orkige_player";
		const char* const TEXCOOK_FILE_NAME = "texcook";
#endif

		//! separator-terminate a non-empty directory string
		Orkige::String terminated(Orkige::String const & directory)
		{
			if(directory.empty() || directory.back() == '/'
				|| directory.back() == '\\')
			{
				return directory;
			}
			return directory + "/";
		}
		//---------------------------------------------------------
		//! the default existence probe: the error_code overload, so an
		//! unreadable path answers "no" instead of throwing out of a resolve
		bool probeExists(Orkige::String const & path)
		{
			if(path.empty())
			{
				return false;
			}
			std::error_code ignored;
			return std::filesystem::exists(path, ignored);
		}
	}
	//---------------------------------------------------------
	EditorResourceLocator::EditorResourceLocator(
		Orkige::String const & bundleBase,
		EditorResourceFallbacks const & fallbacks, ExistsFn exists)
		: mFallbacks(fallbacks),
		mExists(exists ? std::move(exists) : ExistsFn(&probeExists))
	{
		const Orkige::String base = terminated(bundleBase);
		if(base.empty())
		{
			return;
		}
#ifdef __APPLE__
		// SDL_GetBasePath resolves to Orkige.app/Contents/Resources: the
		// content lives right there, the sibling executables one level over in
		// Contents/MacOS (nested executables belong beside the main one).
		// lexically_normal folds the step out of the path so the log line and
		// the spawned player's argv name the real directory.
		this->mResourceRoot = base;
		this->mToolRoot = terminated(std::filesystem::path(base + "../MacOS")
			.lexically_normal().string());
#else
		// a plain executable directory: content in share/orkige, the sibling
		// executables right beside the editor
		this->mResourceRoot = base + "share/orkige/";
		this->mToolRoot = base;
#endif
	}
	//---------------------------------------------------------
	EditorResourcePath EditorResourceLocator::resolveResource(
		Orkige::String const & relative, Orkige::String const & fallback) const
	{
		if(!this->mResourceRoot.empty())
		{
			const Orkige::String candidate = this->mResourceRoot + relative;
			if(this->mExists(candidate))
			{
				return EditorResourcePath{candidate,
					EditorResourceRoot::Bundle};
			}
		}
		if(!fallback.empty() && this->mExists(fallback))
		{
			return EditorResourcePath{fallback, EditorResourceRoot::Tree};
		}
		return EditorResourcePath{};
	}
	//---------------------------------------------------------
	EditorResourcePath EditorResourceLocator::resolveTool(
		Orkige::String const & fileName, Orkige::String const & fallback) const
	{
		if(!this->mToolRoot.empty())
		{
			const Orkige::String candidate = this->mToolRoot + fileName;
			if(this->mExists(candidate))
			{
				return EditorResourcePath{candidate,
					EditorResourceRoot::Bundle};
			}
		}
		if(!fallback.empty() && this->mExists(fallback))
		{
			return EditorResourcePath{fallback, EditorResourceRoot::Tree};
		}
		return EditorResourcePath{};
	}
	//---------------------------------------------------------
	EditorResourcePath EditorResourceLocator::engineMedia() const
	{
		// the marker rule: a Media/ directory only counts when it carries the
		// flavor's shader tree, so an empty (or half-staged) Media never wins
		// over a working developer tree
		const Orkige::String marker = this->mFallbacks.engineMediaMarker.empty()
			? Orkige::String("Hlms") : this->mFallbacks.engineMediaMarker;
		if(!this->mResourceRoot.empty())
		{
			const Orkige::String bundled =
				this->mResourceRoot + MEDIA_DIR_NAME;
			if(this->mExists(bundled + "/" + marker))
			{
				return EditorResourcePath{bundled, EditorResourceRoot::Bundle};
			}
		}
		const Orkige::String fallback = this->mFallbacks.engineMedia;
		if(!fallback.empty()
			&& this->mExists(terminated(fallback) + marker))
		{
			return EditorResourcePath{fallback, EditorResourceRoot::Tree};
		}
		return EditorResourcePath{};
	}
	//---------------------------------------------------------
	EditorResourcePath EditorResourceLocator::engineFonts() const
	{
		return this->resolveResource("Media/fonts", this->mFallbacks.fonts);
	}
	//---------------------------------------------------------
	EditorResourcePath EditorResourceLocator::engineWater() const
	{
		return this->resolveResource("Media/water", this->mFallbacks.water);
	}
	//---------------------------------------------------------
	EditorResourcePath EditorResourceLocator::engineDecals() const
	{
		return this->resolveResource("Media/decals", this->mFallbacks.decals);
	}
	//---------------------------------------------------------
	EditorResourcePath EditorResourceLocator::engineBloom() const
	{
		// per flavor, exactly as an export stages it: Media/bloom/<flavor>
		return this->resolveResource("Media/bloom/" + this->mFallbacks.flavor,
			this->mFallbacks.bloom);
	}
	//---------------------------------------------------------
	EditorResourcePath EditorResourceLocator::engineGrade() const
	{
		return this->resolveResource("Media/grade/" + this->mFallbacks.flavor,
			this->mFallbacks.grade);
	}
	//---------------------------------------------------------
	EditorResourcePath EditorResourceLocator::uiFont(
		Orkige::String const & fileName) const
	{
		const Orkige::String fallback = this->mFallbacks.uiFonts.empty()
			? Orkige::String()
			: terminated(this->mFallbacks.uiFonts) + fileName;
		return this->resolveResource(fileName, fallback);
	}
	//---------------------------------------------------------
	EditorResourcePath EditorResourceLocator::pythonTool(
		Orkige::String const & fileName) const
	{
		const Orkige::String fallback = this->mFallbacks.pythonTools.empty()
			? Orkige::String()
			: terminated(this->mFallbacks.pythonTools) + fileName;
		return this->resolveResource(
			Orkige::String(PYTHON_TOOL_DIR_NAME) + "/" + fileName, fallback);
	}
	//---------------------------------------------------------
	EditorResourcePath EditorResourceLocator::pythonToolFromTree(
		Orkige::String const & fileName) const
	{
		if (this->mFallbacks.pythonTools.empty())
		{
			return EditorResourcePath();
		}
		const Orkige::String path =
			terminated(this->mFallbacks.pythonTools) + fileName;
		if (!this->mExists(path))
		{
			return EditorResourcePath();
		}
		EditorResourcePath resolved;
		resolved.path = path;
		resolved.root = EditorResourceRoot::Tree;
		return resolved;
	}
	//---------------------------------------------------------
	EditorResourcePath EditorResourceLocator::changelog() const
	{
		// no fallback on purpose: only a packaged build shipped WITH a
		// changelog, and a developer build says so rather than showing the
		// working tree's history as if it were a release record
		return this->resolveResource(CHANGELOG_FILE_NAME, Orkige::String());
	}
	//---------------------------------------------------------
	EditorResourcePath EditorResourceLocator::webPlayer() const
	{
		// no fallback on purpose: a build tree answers this question through
		// its own web-release preset tree, which the export plan reaches
		// directly. This asks only "did the packaging stage the browser
		// player inside this app?" - the whole browser payload lives under
		// web/ (@see ExportWeb.h), and the wasm module is what marks it.
		return this->resolveResource(
			Orkige::String(WEB_PAYLOAD_DIR_NAME) + "/" + WEB_PLAYER_FILE_NAME,
			Orkige::String());
	}
	//---------------------------------------------------------
	EditorResourcePath EditorResourceLocator::player() const
	{
		return this->resolveTool(PLAYER_FILE_NAME, this->mFallbacks.player);
	}
	//---------------------------------------------------------
	EditorResourcePath EditorResourceLocator::texcook() const
	{
		return this->resolveTool(TEXCOOK_FILE_NAME, this->mFallbacks.texcook);
	}
	//---------------------------------------------------------
	Orkige::String EditorResourceLocator::describe() const
	{
		const EditorResourcePath media = this->engineMedia();
		Orkige::String text = "resources: ";
		if(media.fromBundle())
		{
			text += "bundled app (" + this->mResourceRoot + ")";
		}
		else if(media.found())
		{
			text += "developer tree";
		}
		else
		{
			text += "NO engine media found - rendering will not work";
		}
		text += ", engine media '" + media.path + "'";
		const EditorResourcePath playerPath = this->player();
		text += ", player '" + playerPath.path + "'" +
			(playerPath.fromBundle() ? " (bundled)" : "");
		return text;
	}
	//---------------------------------------------------------
	Orkige::String editorWritableStateDirectory()
	{
		// ORKIGE_EDITOR_STATE_DIR redirects the whole writable set (the same
		// test-isolation seam ORKIGE_BREADCRUMB_DIR is for the crash trail):
		// the platform's application-support directory is derived from the
		// user account, not from HOME, so a scripted run cannot isolate itself
		// by moving HOME and would otherwise scribble into the real user's
		// editor state.
		if(const char* const stateDirEnv = std::getenv("ORKIGE_EDITOR_STATE_DIR"))
		{
			if(stateDirEnv[0] != '\0')
			{
				const Orkige::String directory = terminated(stateDirEnv);
				std::error_code createError;
				std::filesystem::create_directories(directory, createError);
				return directory;
			}
		}
		const Orkige::String support =
			Orkige::PlatformUtil::getSupportDirectory("Orkige");
		if(support.empty())
		{
			return Orkige::String();
		}
		// getSupportDirectory creates the directory where the platform has a
		// real per-user application-support location; create_directories keeps
		// the contract on the platforms whose implementation does not
		std::error_code createError;
		std::filesystem::create_directories(support, createError);
		return terminated(support);
	}
	//---------------------------------------------------------
	Orkige::String editorStateFilePath(Orkige::String const & fileName,
		Orkige::String const & legacyDirectory)
	{
		const Orkige::String stateDir = editorWritableStateDirectory();
		const Orkige::String legacy = terminated(legacyDirectory) + fileName;
		if(stateDir.empty())
		{
			// no per-user application directory on this platform: the
			// historical location beside the executable stays the answer
			return legacy;
		}
		const Orkige::String target = stateDir + fileName;
		if(legacyDirectory.empty())
		{
			return target;
		}
		std::error_code probeError;
		if(legacy != target
			&& std::filesystem::exists(legacy, probeError)
			&& !std::filesystem::exists(target, probeError))
		{
			// one-time move of a pre-existing settings file out of the (now
			// read-only) app directory, so an in-place upgrade keeps the
			// user's layout and recents
			std::error_code copyError;
			std::filesystem::copy_file(legacy, target,
				std::filesystem::copy_options::overwrite_existing, copyError);
			if(!copyError)
			{
				std::error_code removeError;
				std::filesystem::remove(legacy, removeError);
				oDebugMsg("editor.paths", 0, "moved settings file '" <<
					fileName << "' from '" << legacyDirectory <<
					"' to the writable state directory '" << stateDir << "'");
			}
			else
			{
				oDebugWarn("editor.paths", 0, "could not migrate settings "
					"file '" << legacy << "' to '" << target << "' - starting "
					"from defaults there");
			}
		}
		return target;
	}
}
