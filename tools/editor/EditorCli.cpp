/********************************************************************
	created:	Monday 2026/08/03 at 16:00
	filename: 	EditorCli.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
// The PURE half of the editor's command line: argv in, a decision out
// (@see EditorCli.h). Nothing here touches a disk, a network or a project.

#include "EditorCli.h"

#include <engine_render/RenderSystemSelection.h>

namespace OrkigeEditor
{
	namespace
	{
		//! is @p argument an option rather than a subcommand word?
		//! A lone "-" is not a flag; nothing accepts it, so it falls through to
		//! the unknown-word refusal like any other stray token.
		bool isFlag(Orkige::String const & argument)
		{
			return argument.size() > 1 && argument[0] == '-';
		}
		//---------------------------------------------------------
		//! the subcommand vocabulary. A word not in here is a REFUSAL, never a
		//! silent fall-through to the window (@see the hazard in EditorCli.h).
		bool verbFor(Orkige::String const & word, EditorCliVerb & out)
		{
			if(word == "export")		{ out = EditorCliVerb::Export; return true; }
			if(word == "test")			{ out = EditorCliVerb::Test; return true; }
			if(word == "fetch-payload")	{ out = EditorCliVerb::FetchPayload; return true; }
			if(word == "version")		{ out = EditorCliVerb::Version; return true; }
			if(word == "changelog")		{ out = EditorCliVerb::Changelog; return true; }
			if(word == "help")			{ out = EditorCliVerb::Help; return true; }
			return false;
		}
		//---------------------------------------------------------
		//! a usage refusal: the sentence, plus the verb it was reached under so
		//! the caller can still tell what was attempted
		EditorCliCommand refuse(EditorCliCommand command,
			Orkige::String const & message)
		{
			command.usageError = true;
			command.error = message;
			return command;
		}
		//---------------------------------------------------------
		//! the `export` options. Every credential flag is spelled exactly as
		//! `orkige_export`'s own, so one set of names covers both doors.
		EditorCliCommand parseExport(EditorCliCommand command,
			std::vector<Orkige::String> const & arguments)
		{
			for(std::size_t index = 1; index < arguments.size(); ++index)
			{
				Orkige::String const & argument = arguments[index];
				if(argument == "--help" || argument == "-h")
				{
					command.verb = EditorCliVerb::Help;
					return command;
				}
				// the one valueless export option
				if(argument == "--with-tests")
				{
					command.withTests = true;
					continue;
				}
				if(index + 1 >= arguments.size())
				{
					return refuse(command,
						"missing value for '" + argument + "'");
				}
				Orkige::String const & value = arguments[++index];
				if(argument == "--project") { command.projectPath = value; }
				else if(argument == "--platform") { command.platform = value; }
				else if(argument == "--test-filter")
				{
					command.testFilter = value;
				}
				else if(argument == "--output") { command.outputDirectory = value; }
				else if(argument == "--signing-identity")
				{
					command.credentials.iosIdentity = value;
				}
				else if(argument == "--provisioning-profile")
				{
					command.credentials.iosProfile = value;
				}
				else if(argument == "--distribution-identity")
				{
					command.credentials.iosDistributionIdentity = value;
				}
				else if(argument == "--distribution-profile")
				{
					command.credentials.iosDistributionProfile = value;
				}
				else if(argument == "--android-keystore")
				{
					command.credentials.androidKeystore = value;
				}
				else if(argument == "--android-key-alias")
				{
					command.credentials.androidKeyAlias = value;
				}
				else if(argument == "--bundletool")
				{
					command.credentials.bundletool = value;
				}
				else
				{
					return refuse(command,
						"unknown export argument '" + argument + "'");
				}
			}
			if(command.projectPath.empty() || command.platform.empty())
			{
				return refuse(command, "export needs --project <dir> and "
					"--platform <name>");
			}
			// an option that would silently do nothing is worse than a refusal
			if(!command.testFilter.empty() && !command.withTests)
			{
				return refuse(command, "--test-filter only means something in "
					"a test build - pass --with-tests too");
			}
			return command;
		}
		//---------------------------------------------------------
		//! the `test` options. `--test-filter` is spelled exactly as the
		//! player's own flag, because it IS the player's flag - this door
		//! passes it through rather than reinterpreting it.
		EditorCliCommand parseTest(EditorCliCommand command,
			std::vector<Orkige::String> const & arguments)
		{
			for(std::size_t index = 1; index < arguments.size(); ++index)
			{
				Orkige::String const & argument = arguments[index];
				if(argument == "--help" || argument == "-h")
				{
					command.verb = EditorCliVerb::Help;
					return command;
				}
				if(index + 1 >= arguments.size())
				{
					return refuse(command,
						"missing value for '" + argument + "'");
				}
				Orkige::String const & value = arguments[++index];
				if(argument == "--project") { command.projectPath = value; }
				else if(argument == "--test-filter")
				{
					command.testFilter = value;
				}
				else if(argument == "--report-dir")
				{
					command.reportDirectory = value;
				}
				else
				{
					return refuse(command,
						"unknown test argument '" + argument + "'");
				}
			}
			if(command.projectPath.empty())
			{
				// a suite belongs to a PROJECT (its tests/ directory and its
				// scripts/ libraries), never to a loose scene - the same
				// precondition the player states
				return refuse(command, "test needs --project <dir-or-.orkproj>");
			}
			return command;
		}
		//---------------------------------------------------------
		//! the `fetch-payload` options: one payload id, positionally or named
		EditorCliCommand parseFetchPayload(EditorCliCommand command,
			std::vector<Orkige::String> const & arguments)
		{
			for(std::size_t index = 1; index < arguments.size(); ++index)
			{
				Orkige::String const & argument = arguments[index];
				if(argument == "--help" || argument == "-h")
				{
					command.verb = EditorCliVerb::Help;
					return command;
				}
				if(argument == "--list")
				{
					command.listPayloads = true;
					continue;
				}
				if(argument == "--payload")
				{
					if(index + 1 >= arguments.size())
					{
						return refuse(command,
							"missing value for '" + argument + "'");
					}
					command.payloadId = arguments[++index];
					continue;
				}
				if(isFlag(argument))
				{
					return refuse(command,
						"unknown fetch-payload argument '" + argument + "'");
				}
				if(!command.payloadId.empty())
				{
					return refuse(command,
						"fetch-payload takes ONE payload id");
				}
				command.payloadId = argument;
			}
			if(command.payloadId.empty() && !command.listPayloads)
			{
				return refuse(command, "fetch-payload needs a payload id "
					"(or --list to see them)");
			}
			return command;
		}
		//---------------------------------------------------------
		//! the wordless subcommands take no options of their own
		EditorCliCommand parseBare(EditorCliCommand command,
			std::vector<Orkige::String> const & arguments)
		{
			for(std::size_t index = 1; index < arguments.size(); ++index)
			{
				if(arguments[index] == "--help" || arguments[index] == "-h")
				{
					command.verb = EditorCliVerb::Help;
					return command;
				}
				return refuse(command, "'" + arguments[0] + "' takes no "
					"arguments (got '" + arguments[index] + "')");
			}
			return command;
		}
	}

	//---------------------------------------------------------
	int editorCliUsageExitCode()
	{
		return 2;
	}

	//---------------------------------------------------------
	EditorCliCommand parseEditorCli(
		std::vector<Orkige::String> const & arguments)
	{
		EditorCliCommand command;
		if(arguments.empty())
		{
			return command;		// launch the editor
		}
		Orkige::String const & first = arguments[0];
		if(isFlag(first))
		{
			// `--help` / `-h` anywhere in a flags-only invocation is a request
			// for the usage text, not a window
			for(Orkige::String const & argument : arguments)
			{
				if(argument == "--help" || argument == "-h")
				{
					command.verb = EditorCliVerb::Help;
					command.subcommand = argument;
					return command;
				}
			}
			// every other flag belongs to the windowed editor and is scanned
			// where it always was; an unknown one stays harmless there
			return command;
		}
		command.subcommand = first;
		if(!verbFor(first, command.verb))
		{
			// THE HAZARD (@see EditorCli.h): never fall through to a window
			return refuse(command, "unknown subcommand '" + first + "'");
		}
		switch(command.verb)
		{
		case EditorCliVerb::Export:
			return parseExport(command, arguments);
		case EditorCliVerb::Test:
			return parseTest(command, arguments);
		case EditorCliVerb::FetchPayload:
			return parseFetchPayload(command, arguments);
		default:
			return parseBare(command, arguments);
		}
	}

	//---------------------------------------------------------
	Orkige::String editorDevicelessRefusal(EditorCliCommand const & command,
		Orkige::String const & renderSystemName)
	{
		if(command.headless())
		{
			// a subcommand installs no render system at all, so the variable
			// says nothing about it - and a build server that keeps it set
			// machine-wide must still be able to package a game
			return Orkige::String();
		}
		if(!Orkige::RenderSystemSelection::isDevicelessName(renderSystemName))
		{
			return Orkige::String();
		}
		return "ORKIGE_RENDERSYSTEM=" + renderSystemName + " selects the "
			"deviceless render system, and the editor is a window application: "
			"its scene view, its preview and its whole interface ARE render "
			"targets, so there is nothing for it to be without one. Headless "
			"work goes through the subcommands (orkige_editor help); a live "
			"scene with no display is orkige_player.";
	}

	//---------------------------------------------------------
	Orkige::String editorCliUsage()
	{
		return
			"usage: orkige_editor [subcommand] [options]\n"
			"\n"
			"With no subcommand the editor opens its window as usual.\n"
			"\n"
			"  export         --project <dir> --platform macos|ios-simulator|"
			"ios|ios-ipa|\n"
			"                                  android|android-aab|web\n"
			"                 [--output <dir>]\n"
			"                 [--signing-identity <name>] "
			"[--provisioning-profile <path>]\n"
			"                 [--distribution-identity <name>] "
			"[--distribution-profile <path>]\n"
			"                 [--android-keystore <path>] "
			"[--android-key-alias <name>]\n"
			"                 [--bundletool <path>]\n"
			"                 [--with-tests [--test-filter <substring>]]\n"
			"                 Packages a project with this installation's own\n"
			"                 engine source. Prints "
			"'orkige_editor: OK <artifact>'.\n"
			"                 --with-tests packages a TEST BUILD instead: the\n"
			"                 project's suite rides along and the artifact "
			"runs\n"
			"                 it. Not shippable. macos and iOS targets only.\n"
			"  test           --project <dir-or-.orkproj>\n"
			"                 [--test-filter <substring>] [--report-dir <dir>]\n"
			"                 Runs the project's Lua suite "
			"(tests/*.test.lua) in\n"
			"                 this installation's player. The suite's own "
			"verdict\n"
			"                 is the exit code.\n"
			"  fetch-payload  <id> | --list\n"
			"                 Downloads and installs a platform's player.\n"
			"  version        This build's identity.\n"
			"  changelog      What this build shipped with.\n"
			"  help           This text.\n"
			"\n"
			"Exit codes: 0 success, 1 the operation failed, 2 bad usage.\n"
			"\n"
			"Scene, asset and editor-script operations are NOT available "
			"headlessly:\n"
			"they need a live game world, and this process boots one only "
			"into a window.\n"
			"Drive those through the editor's MCP endpoint (Docs/mcp.md).\n";
	}
}
