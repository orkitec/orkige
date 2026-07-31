/********************************************************************
	created:	Friday 2026/07/31 at 12:00
	filename: 	ExportProcess.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#include "ExportProcess.h"

#include "ExportFiles.h"

#include <cstdlib>
#include <vector>

#ifdef _WIN32
#	include <windows.h>
#else
#	include <spawn.h>
#	include <sys/wait.h>
#	include <unistd.h>
extern char ** environ;
#endif

namespace OrkigeExport
{
	namespace
	{
		//! does @p text need quoting to read back as one word in the echoed
		//! command line (display only - @see commandLine)
		bool needsQuotes(Orkige::String const & text)
		{
			if(text.empty())
			{
				return true;
			}
			return text.find_first_of(" \t\"'\\$&|;<>()") !=
				Orkige::String::npos;
		}
	}
	//---------------------------------------------------------
	Orkige::String commandLine(std::vector<Orkige::String> const & arguments)
	{
		Orkige::String line;
		for(Orkige::String const & argument : arguments)
		{
			if(!line.empty())
			{
				line += ' ';
			}
			if(needsQuotes(argument))
			{
				line += "'" + argument + "'";
			}
			else
			{
				line += argument;
			}
		}
		return line;
	}
	//---------------------------------------------------------
	Orkige::String findOnPath(Orkige::String const & name)
	{
		const char * pathVariable = std::getenv("PATH");
		if(pathVariable == 0)
		{
			return "";
		}
#ifdef _WIN32
		const char separator = ';';
		const Orkige::String suffix = ".exe";
#else
		const char separator = ':';
		const Orkige::String suffix;
#endif
		const Orkige::String path(pathVariable);
		std::size_t start = 0;
		while(start <= path.size())
		{
			const std::size_t split = path.find(separator, start);
			const Orkige::String directory = (split == Orkige::String::npos)
				? path.substr(start) : path.substr(start, split - start);
			if(!directory.empty())
			{
				const Orkige::String candidate =
					ExportFiles::join(directory, name + suffix);
				if(ExportFiles::isRegularFile(candidate))
				{
					return candidate;
				}
			}
			if(split == Orkige::String::npos)
			{
				break;
			}
			start = split + 1;
		}
		return "";
	}
	//---------------------------------------------------------
#ifdef _WIN32
	ProcessResult runProcess(std::vector<Orkige::String> const & arguments)
	{
		ProcessResult result;
		if(arguments.empty())
		{
			return result;
		}
		// one command line, quoted the way CreateProcess parses it back
		Orkige::String line;
		for(Orkige::String const & argument : arguments)
		{
			if(!line.empty())
			{
				line += ' ';
			}
			line += '"';
			for(char character : argument)
			{
				if(character == '"' || character == '\\')
				{
					line += '\\';
				}
				line += character;
			}
			line += '"';
		}
		SECURITY_ATTRIBUTES security = {};
		security.nLength = sizeof(security);
		security.bInheritHandle = TRUE;
		HANDLE readPipe = 0;
		HANDLE writePipe = 0;
		if(!CreatePipe(&readPipe, &writePipe, &security, 0))
		{
			return result;
		}
		SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);
		STARTUPINFOA startup = {};
		startup.cb = sizeof(startup);
		startup.dwFlags = STARTF_USESTDHANDLES;
		startup.hStdOutput = writePipe;
		startup.hStdError = writePipe;
		startup.hStdInput = 0;
		PROCESS_INFORMATION process = {};
		std::vector<char> mutableLine(line.begin(), line.end());
		mutableLine.push_back('\0');
		if(!CreateProcessA(0, mutableLine.data(), 0, 0, TRUE, 0, 0, 0,
			&startup, &process))
		{
			CloseHandle(readPipe);
			CloseHandle(writePipe);
			return result;
		}
		CloseHandle(writePipe);
		result.launched = true;
		char buffer[4096];
		DWORD readBytes = 0;
		while(ReadFile(readPipe, buffer, sizeof(buffer), &readBytes, 0) &&
			readBytes > 0)
		{
			result.output.append(buffer, readBytes);
		}
		CloseHandle(readPipe);
		WaitForSingleObject(process.hProcess, INFINITE);
		DWORD exitCode = 0;
		GetExitCodeProcess(process.hProcess, &exitCode);
		result.exitCode = static_cast<int>(exitCode);
		CloseHandle(process.hProcess);
		CloseHandle(process.hThread);
		return result;
	}
#else
	ProcessResult runProcess(std::vector<Orkige::String> const & arguments)
	{
		ProcessResult result;
		if(arguments.empty())
		{
			return result;
		}
		int pipeFds[2] = { -1, -1 };
		if(pipe(pipeFds) != 0)
		{
			return result;
		}
		posix_spawn_file_actions_t actions;
		posix_spawn_file_actions_init(&actions);
		posix_spawn_file_actions_addclose(&actions, pipeFds[0]);
		// stderr is merged into stdout: a tool's own complaint is the most
		// useful line an export can show, and it must not be lost to ordering
		posix_spawn_file_actions_adddup2(&actions, pipeFds[1], STDOUT_FILENO);
		posix_spawn_file_actions_adddup2(&actions, pipeFds[1], STDERR_FILENO);
		posix_spawn_file_actions_addclose(&actions, pipeFds[1]);
		std::vector<char *> argv;
		argv.reserve(arguments.size() + 1);
		for(Orkige::String const & argument : arguments)
		{
			argv.push_back(const_cast<char *>(argument.c_str()));
		}
		argv.push_back(0);
		pid_t child = 0;
		const int spawned = posix_spawnp(&child, arguments[0].c_str(), &actions,
			0, argv.data(), environ);
		posix_spawn_file_actions_destroy(&actions);
		close(pipeFds[1]);
		if(spawned != 0)
		{
			close(pipeFds[0]);
			return result;
		}
		result.launched = true;
		char buffer[4096];
		ssize_t readBytes = 0;
		while((readBytes = read(pipeFds[0], buffer, sizeof(buffer))) > 0)
		{
			result.output.append(buffer, static_cast<std::size_t>(readBytes));
		}
		close(pipeFds[0]);
		int status = 0;
		while(waitpid(child, &status, 0) < 0)
		{
			// interrupted by a signal - keep waiting for the real verdict
		}
		if(WIFEXITED(status))
		{
			result.exitCode = WEXITSTATUS(status);
		}
		else if(WIFSIGNALED(status))
		{
			result.exitCode = 128 + WTERMSIG(status);
		}
		return result;
	}
#endif
	//---------------------------------------------------------
	ProcessRunner defaultProcessRunner()
	{
		return [](std::vector<Orkige::String> const & arguments)
		{
			return runProcess(arguments);
		};
	}
}
