/**************************************************************
	created:	2026/07/27 at 12:00
	filename: 	EditorSourceControlPanel.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
// EditorSourceControlPanel.cpp - the "Source Control" dockable panel and the
// shared async git-status service behind it. The service resolves the open
// project's repo (EditorGit), keeps ONE cached status snapshot the panel AND the
// Asset browser read, and runs stage/unstage/discard/commit/push on a worker
// thread so the frame never blocks on git. Results marshal back on
// sourceControlTick(), called once per editor frame.
//
// NO MCP verbs are exposed for stage/commit/push: agents are forbidden from
// committing on the user's behalf, and an MCP tool would launder that
// prohibition - status READ could be exposed later, but no mutation. This is the
// documented exception to the MCP-parity rule (@see Docs/editor.md).
#include "EditorApp.h"
#include "EditorSourceControlPanel.h"
#include "EditorTabMenu.h"
#include "IconsFontAwesome6.h"

#include <SDL3/SDL.h>

#include <atomic>
#include <functional>
#include <mutex>
#include <thread>

namespace OrkigeEditor
{
	namespace
	{
		//! run git with stdout+stderr MERGED into `output` (a commit-msg hook
		//! rejection / push auth error writes to stderr - we must surface it). The
		//! same SDL idiom the control server uses; safe to call off the UI thread.
		bool runGitProcess(std::vector<std::string> const& args,
			std::string& output, int& exitCode)
		{
			output.clear();
			exitCode = -1;
			std::vector<const char*> argv;
			argv.reserve(args.size() + 1);
			for (std::string const& arg : args)
			{
				argv.push_back(arg.c_str());
			}
			argv.push_back(nullptr);
			SDL_PropertiesID props = SDL_CreateProperties();
			SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER,
				const_cast<char**>(argv.data()));
			SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER,
				SDL_PROCESS_STDIO_APP);
			SDL_SetBooleanProperty(props,
				SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true);
			SDL_Process* process = SDL_CreateProcessWithProperties(props);
			SDL_DestroyProperties(props);
			if (!process)
			{
				const char* error = SDL_GetError();
				output = error ? error : "git could not be spawned";
				return false;
			}
			size_t dataSize = 0;
			void* data = SDL_ReadProcess(process, &dataSize, &exitCode);
			if (data)
			{
				output.assign(static_cast<char*>(data), dataSize);
				SDL_free(data);
			}
			SDL_DestroyProcess(process);
			return true;
		}

		//! keep the last few lines of a message for the one-line status strip
		std::string lastLine(std::string text)
		{
			while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
			{
				text.pop_back();
			}
			const std::size_t nl = text.find_last_of('\n');
			return nl == std::string::npos ? text : text.substr(nl + 1);
		}

		//! the outcome of one worker job (an optional op + a status refresh),
		//! tagged with the project it ran for so a project switch mid-flight
		//! discards a stale result.
		struct GitJobResult
		{
			std::string	projectRoot;	//!< the project the job ran for
			std::string	root;			//!< the resolved repo root
			bool		repoValid = false;
			bool		ranOp = false;
			GitResult	opResult;
			GitStatus	status;
		};

		//! the process-wide source-control service (one open project at a time).
		struct Service
		{
			std::string	projectRoot;		//!< the tracked project ("" = none)
			std::string	repoRoot;			//!< cached resolved repo root
			bool		resolved = false;	//!< a refresh has completed once
			bool		hasRepo = false;	//!< the project sits in a git repo
			GitStatus	status;				//!< the grouped status for the panel
			GitBadgeSnapshot	snapshot;	//!< the shared browser snapshot
			std::string	statusLine;			//!< last op message / errors
			bool		statusLineError = false;

			// async worker (one job at a time)
			std::thread	worker;
			std::atomic<bool>	running{ false };
			std::atomic<bool>	done{ false };
			std::mutex			mutex;
			GitJobResult		result;			//!< guarded by mutex

			// the pending job (set on the UI thread, consumed by tick)
			bool		jobPending = false;
			std::string	jobLabel;
			std::function<GitResult(GitRepo const&)>	jobOp;	//!< empty = refresh only
			bool		jobIsDiscard = false;
			std::string	jobDiscardAbs;			//!< absolute path to reload after

			//! join any in-flight worker at process exit so a commit/push still
			//! running when the editor quits never trips std::terminate (the
			//! function-static's destructor runs while SDL is still winding down;
			//! the worker was spawned while SDL was up, so joining just drains it)
			~Service()
			{
				if (this->worker.joinable())
				{
					this->worker.join();
				}
			}
		};

		Service& service()
		{
			static Service instance;
			return instance;
		}

		//! the worker body: resolve the repo (if not cached), run the op, refresh
		void runJob(Service* svc, GitRunner runner, std::string projectRoot,
			std::string cachedRoot, std::function<GitResult(GitRepo const&)> op)
		{
			GitJobResult out;
			out.projectRoot = projectRoot;
			std::string root = cachedRoot;
			if (root.empty())
			{
				root = gitResolveRepoRoot(runner, projectRoot);
			}
			out.root = root;
			GitRepo repo{ runner, root };
			if (repo.valid())
			{
				out.repoValid = true;
				if (op)
				{
					out.ranOp = true;
					out.opResult = op(repo);
				}
				out.status = repo.status();
			}
			{
				std::lock_guard<std::mutex> lock(svc->mutex);
				svc->result = std::move(out);
			}
			svc->done.store(true);
		}

		//! queue a job (op empty = a pure refresh). One at a time: a request while
		//! busy is dropped (the in-flight job already refreshes on completion). No
		//! live git during automated runs.
		void requestJob(std::string const& label,
			std::function<GitResult(GitRepo const&)> op,
			bool isDiscard = false, std::string const& discardAbs = std::string())
		{
			if (gAutomatedRun)
			{
				return;
			}
			Service& svc = service();
			if (svc.running.load() || svc.jobPending || svc.projectRoot.empty())
			{
				return;
			}
			svc.jobPending = true;
			svc.jobLabel = label;
			svc.jobOp = std::move(op);
			svc.jobIsDiscard = isDiscard;
			svc.jobDiscardAbs = discardAbs;
		}

		void requestRefresh()
		{
			requestJob("refresh", nullptr);
		}

		//! project-relative -> absolute (for opening a row / reloading a doc)
		std::string repoRelToAbsolute(Service const& svc, std::string const& repoRel)
		{
			if (svc.repoRoot.empty() || repoRel.empty())
			{
				return std::string();
			}
			return svc.repoRoot + "/" + repoRel;
		}
	}

	GitRunner sourceControlGitRunner()
	{
		return [](std::vector<std::string> const& argv, std::string& output,
			int& exitCode) { return runGitProcess(argv, output, exitCode); };
	}

	GitBadgeSnapshot const& sourceControlBadgeSnapshot()
	{
		return service().snapshot;
	}

	void sourceControlOnProjectChanged(std::string const& projectRoot)
	{
		Service& svc = service();
		svc.projectRoot = projectRoot;
		svc.repoRoot.clear();
		svc.resolved = false;
		svc.hasRepo = false;
		svc.status = GitStatus();
		svc.snapshot = GitBadgeSnapshot();
		svc.statusLine.clear();
		svc.statusLineError = false;
		if (!projectRoot.empty())
		{
			requestRefresh();
		}
	}

	void sourceControlEnsureProject(std::string const& projectRoot)
	{
		if (service().projectRoot != projectRoot)
		{
			sourceControlOnProjectChanged(projectRoot);
		}
	}

	void sourceControlRefresh()
	{
		requestRefresh();
	}

	void sourceControlTick()
	{
		Service& svc = service();
		// marshal a finished worker back onto the UI thread
		if (svc.running.load() && svc.done.load())
		{
			if (svc.worker.joinable())
			{
				svc.worker.join();
			}
			GitJobResult result;
			{
				std::lock_guard<std::mutex> lock(svc.mutex);
				result = svc.result;
			}
			svc.running.store(false);
			// a project switch mid-flight: discard the stale result, re-refresh
			if (result.projectRoot != svc.projectRoot)
			{
				if (!svc.projectRoot.empty())
				{
					requestRefresh();
				}
			}
			else
			{
				svc.repoRoot = result.root;
				svc.resolved = true;
				svc.hasRepo = result.repoValid;
				if (result.repoValid)
				{
					svc.status = result.status;
					svc.snapshot = buildBadgeSnapshot(result.status,
						result.root, svc.projectRoot);
				}
				else
				{
					svc.status = GitStatus();
					svc.snapshot = GitBadgeSnapshot();
				}
				if (result.ranOp)
				{
					svc.statusLineError = !result.opResult.ok();
					if (!result.opResult.spawned)
					{
						svc.statusLine = "git could not be run: " +
							result.opResult.output;
					}
					else if (result.opResult.ok())
					{
						svc.statusLine = svc.jobLabel + ": done" +
							(result.opResult.output.empty() ? "" :
								" - " + lastLine(result.opResult.output));
					}
					else
					{
						svc.statusLine = svc.jobLabel + " failed: " +
							(result.opResult.output.empty() ?
								"exit " + std::to_string(result.opResult.exitCode)
								: result.opResult.output);
					}
					// a discard reloads the file's open editor buffer (+ gutter
					// baseline) so the document never stays stale on disk-changed
					if (svc.jobIsDiscard && result.opResult.ok() &&
						!svc.jobDiscardAbs.empty())
					{
						scriptPanelReloadFromDisk(svc.jobDiscardAbs);
					}
				}
			}
		}
		// start a pending job
		if (svc.jobPending && !svc.running.load())
		{
			svc.jobPending = false;
			svc.running.store(true);
			svc.done.store(false);
			svc.worker = std::thread(runJob, &svc, sourceControlGitRunner(),
				svc.projectRoot, svc.repoRoot, svc.jobOp);
		}
	}
}

namespace
{
	using namespace OrkigeEditor;

	ImVec4 const kStagedColor{ 0.42f, 0.78f, 0.47f, 1.0f };		//!< green
	ImVec4 const kModifiedColor{ 0.90f, 0.68f, 0.28f, 1.0f };	//!< amber
	ImVec4 const kUntrackedColor{ 0.55f, 0.62f, 0.70f, 1.0f };	//!< muted grey
	ImVec4 const kConflictColor{ 0.90f, 0.38f, 0.38f, 1.0f };	//!< red

	//! the one-letter status code + tint for a row in a given group
	void statusGlyph(GitFileEntry const& entry, bool stagedGroup,
		char& outLetter, ImVec4& outColor)
	{
		if (entry.conflicted)
		{
			outLetter = 'U';
			outColor = kConflictColor;
			return;
		}
		if (entry.untracked)
		{
			outLetter = '?';
			outColor = kUntrackedColor;
			return;
		}
		const char code = stagedGroup ? entry.index : entry.worktree;
		outLetter = (code == '.' || code == '\0') ? 'M' : code;
		outColor = stagedGroup ? kStagedColor : kModifiedColor;
	}

	//! the display path for a row (a rename shows old -> new)
	std::string rowLabel(GitFileEntry const& entry)
	{
		if (entry.isRename())
		{
			return entry.origPath + "  " ICON_FA_ARROW_UP "  " + entry.path;
		}
		return entry.path;
	}
}

//! draw the Source Control panel. Signature mirrors the other project panels
//! (@see EditorApp.h). All git runs through the async service; automated runs
//! do no git at all here.
void drawSourceControlPanel(EditorState& state, ViewSettings& viewSettings,
	Orkige::EditorCore& core, bool* visible)
{
	(void)core;
	Service& svc = service();
	static bool sFocusedLastFrame = false;
	// the discard confirm target (repo-relative + a display name); "" = no popup
	static std::string sDiscardRepoRel;
	static std::string sDiscardDisplay;
	// a trash click sets this; the popup is OPENED at the window-root ID level
	// below (OpenPopup and BeginPopupModal must share an ID-stack level, and the
	// click happens inside the per-row PushID scope)
	static bool sOpenDiscardPopup = false;
	static char sCommitMessage[4096] = "";

	if (!ImGui::Begin(ICON_FA_CODE_BRANCH " Source Control###SourceControl",
		visible))
	{
		ImGui::End();
		return;
	}
	OrkigeEditor::editorPanelTabMenu(visible);

	// automated runs perform NO git here (the pollution-hygiene rule); the
	// selfcheck drives the EditorGit seam directly against its temp repo
	if (gAutomatedRun)
	{
		ImGui::TextDisabled(
			"Source control is inactive during automated runs.");
		ImGui::End();
		return;
	}
	if (!state.project.isLoaded())
	{
		ImGui::TextDisabled("Open a project to use source control.");
		sFocusedLastFrame = false;
		ImGui::End();
		return;
	}
	// keep the service pointed at the current project (handles a switch even if
	// the open path did not route through sourceControlOnProjectChanged)
	const std::string projectRoot = state.project.getRootDirectory();
	if (svc.projectRoot != projectRoot)
	{
		sourceControlOnProjectChanged(projectRoot);
	}

	// refresh when the tab gains focus (no polling loop in v1)
	const bool focused = ImGui::IsWindowFocused(
		ImGuiFocusedFlags_RootAndChildWindows);
	if (focused && !sFocusedLastFrame)
	{
		requestRefresh();
	}
	sFocusedLastFrame = focused;

	const bool busy = svc.running.load();

	// --- header: branch + ahead/behind + refresh ---------------------------
	if (svc.resolved && svc.hasRepo)
	{
		ImGui::TextColored(kStagedColor, ICON_FA_CODE_BRANCH);
		ImGui::SameLine();
		if (svc.status.detached)
		{
			ImGui::TextUnformatted("(detached HEAD)");
		}
		else if (svc.status.initialCommit)
		{
			ImGui::Text("%s (no commits yet)",
				svc.status.branch.empty() ? "?" : svc.status.branch.c_str());
		}
		else
		{
			ImGui::TextUnformatted(svc.status.branch.empty() ?
				"(unknown branch)" : svc.status.branch.c_str());
		}
		if (svc.status.hasUpstream &&
			(svc.status.ahead > 0 || svc.status.behind > 0))
		{
			ImGui::SameLine();
			ImGui::TextDisabled(ICON_FA_ARROW_UP "%d  " ICON_FA_ARROW_DOWN "%d",
				svc.status.ahead, svc.status.behind);
		}
	}
	ImGui::SameLine(ImGui::GetContentRegionAvail().x -
		ImGui::GetFrameHeight() - ImGui::GetStyle().ItemSpacing.x);
	ImGui::BeginDisabled(busy);
	if (ImGui::Button(ICON_FA_ARROWS_ROTATE "###scRefresh"))
	{
		requestRefresh();
	}
	ImGui::EndDisabled();
	ImGui::SetItemTooltip("Refresh git status");
	if (busy)
	{
		ImGui::SameLine();
		ImGui::TextDisabled("working...");
	}
	ImGui::Separator();

	// honest empty state: not a repo / git absent (only after a completed probe)
	if (svc.resolved && !svc.hasRepo)
	{
		ImGui::TextWrapped(
			"This project is not inside a git repository, or git is not "
			"available on PATH. Source control is unavailable.");
		ImGui::End();
		return;
	}
	if (!svc.resolved)
	{
		ImGui::TextDisabled("Reading git status...");
		ImGui::End();
		return;
	}

	// --- a group of file rows ----------------------------------------------
	// returns the repo-rel path the user clicked to open (else "")
	const auto drawGroup = [&](const char* title,
		std::vector<GitFileEntry> const& entries, bool stagedGroup,
		bool untrackedGroup)
	{
		if (entries.empty())
		{
			return;
		}
		ImGui::PushID(title);
		// header row: a "Stage All"/"Unstage All" affordance for the group
		ImGui::AlignTextToFramePadding();
		ImGui::Text("%s (%zu)", title, entries.size());
		ImGui::SameLine(ImGui::GetContentRegionAvail().x -
			ImGui::GetFrameHeight());
		ImGui::BeginDisabled(busy);
		if (stagedGroup)
		{
			if (ImGui::SmallButton(ICON_FA_MINUS "###allUnstage"))
			{
				requestJob("unstage all",
					[](GitRepo const& r) { return r.unstageAll(); });
			}
			ImGui::SetItemTooltip("Unstage all");
		}
		else
		{
			if (ImGui::SmallButton(ICON_FA_PLUS "###allStage"))
			{
				requestJob("stage all",
					[](GitRepo const& r) { return r.stageAll(); });
			}
			ImGui::SetItemTooltip("Stage all");
		}
		ImGui::EndDisabled();

		for (GitFileEntry const& entry : entries)
		{
			ImGui::PushID(entry.path.c_str());
			char letter = 'M';
			ImVec4 color = kModifiedColor;
			statusGlyph(entry, stagedGroup, letter, color);
			// stage/unstage control at the row's LEADING edge
			ImGui::BeginDisabled(busy);
			if (stagedGroup)
			{
				if (ImGui::SmallButton(ICON_FA_MINUS))
				{
					const std::string path = entry.path;
					requestJob("unstage " + path,
						[path](GitRepo const& r) { return r.unstage(path); });
				}
				ImGui::SetItemTooltip("Unstage");
			}
			else
			{
				if (ImGui::SmallButton(ICON_FA_PLUS))
				{
					const std::string path = entry.path;
					requestJob("stage " + path,
						[path](GitRepo const& r) { return r.stage(path); });
				}
				ImGui::SetItemTooltip("Stage");
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			ImGui::TextColored(color, "%c", letter);
			ImGui::SameLine();
			// the path: a click opens it in the embedded editor (the diff gutter
			// shows the very changes listed here - the synergy)
			const std::string label = rowLabel(entry);
			if (ImGui::Selectable(label.c_str(), false,
				ImGuiSelectableFlags_AllowDoubleClick,
					ImVec2(ImGui::CalcTextSize(label.c_str()).x, 0.0f)))
			{
				const std::string abs = repoRelToAbsolute(svc, entry.path);
				if (!abs.empty())
				{
					scriptPanelOpenFile(state, viewSettings, abs, 0);
				}
			}
			// per-row destructive discard for a TRACKED file (never untracked -
			// deleting a brand-new file from here is a footgun the file browser
			// owns; @see Docs/editor.md)
			if (!untrackedGroup && !entry.conflicted)
			{
				ImGui::SameLine();
				ImGui::BeginDisabled(busy);
				if (ImGui::SmallButton(ICON_FA_TRASH_CAN))
				{
					sDiscardRepoRel = entry.path;
					sDiscardDisplay = entry.path;
					sOpenDiscardPopup = true;	// opened at root level below
				}
				ImGui::EndDisabled();
				ImGui::SetItemTooltip("Discard changes (reset to committed)");
			}
			ImGui::PopID();
		}
		ImGui::Spacing();
		ImGui::PopID();
	};

	if (svc.status.clean())
	{
		ImGui::TextDisabled("No changes - the working tree is clean.");
	}
	else
	{
		drawGroup("Staged", svc.status.staged(), true, false);
		drawGroup("Changes", svc.status.unstaged(), false, false);
		drawGroup("Untracked", svc.status.untrackedFiles(), false, true);
		const std::vector<GitFileEntry> conflicts = svc.status.conflicts();
		if (!conflicts.empty())
		{
			ImGui::TextColored(kConflictColor, "Conflicts (%zu) - resolve in a "
				"merge tool, then stage", conflicts.size());
			for (GitFileEntry const& entry : conflicts)
			{
				ImGui::BulletText("%s", entry.path.c_str());
			}
			ImGui::Spacing();
		}
	}

	// --- commit --------------------------------------------------------------
	ImGui::SeparatorText("Commit");
	ImGui::InputTextMultiline("###commitMessage", sCommitMessage,
		sizeof(sCommitMessage), ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 3),
		ImGuiInputTextFlags_None);
	const bool hasMessage = sCommitMessage[0] != '\0';
	const bool canCommit = hasMessage && svc.status.anyStaged() && !busy;
	ImGui::BeginDisabled(!canCommit);
	if (ImGui::Button(ICON_FA_CODE_COMMIT " Commit"))
	{
		const std::string message = sCommitMessage;
		requestJob("commit",
			[message](GitRepo const& r) { return r.commit(message); });
		sCommitMessage[0] = '\0';
	}
	ImGui::EndDisabled();
	if (!svc.status.anyStaged())
	{
		ImGui::SameLine();
		ImGui::TextDisabled("(stage a file first)");
	}

	// --- push / publish -------------------------------------------------------
	ImGui::SameLine();
	if (svc.status.hasUpstream)
	{
		const bool canPush = svc.status.ahead > 0 && !busy;
		ImGui::BeginDisabled(!canPush);
		std::string pushLabel = ICON_FA_CLOUD_ARROW_UP " Push";
		if (svc.status.ahead > 0)
		{
			pushLabel += " (" + std::to_string(svc.status.ahead) + ")";
		}
		if (ImGui::Button(pushLabel.c_str()))
		{
			requestJob("push", [](GitRepo const& r) { return r.push(); });
		}
		ImGui::EndDisabled();
	}
	else if (!svc.status.detached && !svc.status.initialCommit &&
		!svc.status.branch.empty())
	{
		// no upstream yet: offer the FIRST push (git push -u origin <branch>),
		// which both uploads the branch and sets its upstream so later Push works
		const std::string branch = svc.status.branch;
		ImGui::BeginDisabled(busy);
		if (ImGui::Button(ICON_FA_CLOUD_ARROW_UP " Publish branch"))
		{
			requestJob("publish " + branch,
				[branch](GitRepo const& r) { return r.publishBranch(branch); });
		}
		ImGui::EndDisabled();
		ImGui::SetItemTooltip(
			"push this branch to origin and set it as upstream");
	}
	else
	{
		// detached HEAD or no commits yet: nothing to push/publish
		ImGui::BeginDisabled(true);
		ImGui::Button(ICON_FA_CLOUD_ARROW_UP " Push");
		ImGui::EndDisabled();
		ImGui::SetItemTooltip(svc.status.initialCommit
			? "make a commit before publishing this branch"
			: "detached HEAD - no branch to publish");
	}

	// --- status strip --------------------------------------------------------
	if (!svc.statusLine.empty())
	{
		ImGui::Separator();
		if (svc.statusLineError)
		{
			ImGui::TextColored(kConflictColor, "%s", svc.statusLine.c_str());
		}
		else
		{
			ImGui::TextDisabled("%s", svc.statusLine.c_str());
		}
	}

	// --- discard confirm (mandatory, names the file) ------------------------
	// open at the window-root ID level (a trash click set the flag inside a
	// per-row PushID scope, where OpenPopup would key a different id than this
	// BeginPopupModal)
	if (sOpenDiscardPopup)
	{
		ImGui::OpenPopup("Discard changes?");
		sOpenDiscardPopup = false;
	}
	if (ImGui::BeginPopupModal("Discard changes?", nullptr,
		ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::TextWrapped("Discard all uncommitted changes to");
		ImGui::TextColored(kModifiedColor, "%s", sDiscardDisplay.c_str());
		ImGui::TextWrapped("and reset it to the committed version? This cannot "
			"be undone - saved-but-uncommitted edits are lost.");
		ImGui::Separator();
		if (ImGui::Button("Discard"))
		{
			const std::string path = sDiscardRepoRel;
			const std::string abs = repoRelToAbsolute(svc, path);
			requestJob("discard " + path,
				[path](GitRepo const& r) { return r.discard(path); },
				/*isDiscard=*/true, abs);
			sDiscardRepoRel.clear();
			sDiscardDisplay.clear();
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel"))
		{
			sDiscardRepoRel.clear();
			sDiscardDisplay.clear();
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	ImGui::End();
}
