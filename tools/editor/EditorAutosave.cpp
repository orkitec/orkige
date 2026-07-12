// EditorAutosave implementation - see EditorAutosave.h for the conventions.
//
// Part of orkige (orkitec Game Engine), (c) 2009-2026 orkitec
#include "EditorAutosave.h"

#include <filesystem>

namespace Orkige
{
	namespace EditorAutosave
	{
		double defaultIntervalSeconds()
		{
			return 120.0;	// ~2 minutes
		}

		String autosavePath(String const& scenePath)
		{
			if (scenePath.empty())
			{
				return String();
			}
			return scenePath + ".autosave";
		}

		String backupPath(String const& scenePath)
		{
			if (scenePath.empty())
			{
				return String();
			}
			return scenePath + ".bak";
		}

		bool shouldAutosave(bool sceneDirty, bool automatedRun, bool playActive,
			bool prefabEditActive, double secondsSinceLastAutosave,
			double intervalSeconds)
		{
			if (!sceneDirty || automatedRun || playActive || prefabEditActive)
			{
				return false;
			}
			return secondsSinceLastAutosave >= intervalSeconds;
		}

		bool recoveryAvailable(String const& scenePath)
		{
			if (scenePath.empty())
			{
				return false;
			}
			std::error_code ec;
			const std::filesystem::path autosave = autosavePath(scenePath);
			if (!std::filesystem::exists(autosave, ec))
			{
				return false;
			}
			// a surviving autosave with no scene file (an untitled scene that
			// was autosaved then lost its target) is recoverable outright
			const std::filesystem::path scene = scenePath;
			if (!std::filesystem::exists(scene, ec))
			{
				return true;
			}
			const std::filesystem::file_time_type autosaveTime =
				std::filesystem::last_write_time(autosave, ec);
			if (ec)
			{
				return false;
			}
			const std::filesystem::file_time_type sceneTime =
				std::filesystem::last_write_time(scene, ec);
			if (ec)
			{
				return false;
			}
			return autosaveTime >= sceneTime;
		}

		bool writeBackup(String const& scenePath)
		{
			if (scenePath.empty())
			{
				return true;	// nothing to back up (untitled)
			}
			std::error_code ec;
			const std::filesystem::path scene = scenePath;
			if (!std::filesystem::exists(scene, ec))
			{
				return true;	// first Save / Save As to a new path
			}
			std::filesystem::copy_file(scene, backupPath(scenePath),
				std::filesystem::copy_options::overwrite_existing, ec);
			return !ec;
		}

		bool removeAutosave(String const& scenePath)
		{
			if (scenePath.empty())
			{
				return true;
			}
			std::error_code ec;
			std::filesystem::remove(autosavePath(scenePath), ec);
			return !ec;
		}
	}
}
