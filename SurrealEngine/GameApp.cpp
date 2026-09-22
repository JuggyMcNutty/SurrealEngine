
#include "Precomp.h"
#include "Utils/Exception.h"
#include "Utils/Logger.h"
#include "Utils/CommandLine.h"
#include "GameApp.h"
#include "GameFolder.h"
#include "Engine.h"
#include "UI/WidgetResourceData.h"
#include "UI/ErrorWindow/ErrorWindow.h"
#include "UI/Launcher/LauncherWindow.h"
#include "Utils/File.h"
#include <stdexcept>
#include <iostream>
#include "Utils/Logger.h"

int GameApp::main(Array<std::string> args)
{
	InitWidgetResources("dark");
	int exitcode = 0;

	try
	{
		CommandLine cmd(args);
		commandline = &cmd;

		// Stream the engine log to stderr in headless mode. Without this the
		// log only surfaces inside the modal error window, which is no use on
		// a test rig or an embedded target -- and LogUnimplemented() is exactly
		// how we find out which natives a game still needs.
		if (commandline->HasArg("-n", "--no-launcher") || commandline->HasArg("-v", "--verbose"))
		{
			Logger::Get()->SetCallback([](const LogMessageLine& line)
			{
				std::cerr << "[" << line.Source << "] " << line.Text << std::endl;
			});
		}

		if (ErrorWindow::CheckCrashReporter())
			return 0;

		if (commandline->HasArg("-h", "--help"))
		{
			std::cout << "SurrealEngine [--url=<mapname>] [--engineversion=X] [Path to game folder]\n";
			return 0;
		}

		// Skip the desktop launcher window when a game folder is given on the
		// command line, or when --no-launcher is passed.
		//
		// Needed twice over: it is the only way to drive the engine headlessly
		// for testing, and the handheld target has no mouse and no desktop --
		// game selection happens in our own launcher before this process starts.
		int selectedGameIndex;
		if (!commandline->GetItems().empty() || commandline->HasArg("-n", "--no-launcher"))
		{
			GameFolderSelection::UpdateList();
			if (GameFolderSelection::Games.empty())
			{
				std::cout << "No UE1 game found in the given folder." << std::endl;
				selectedGameIndex = -1;
			}
			else
			{
				selectedGameIndex = 0;
				std::cout << "Launching " << GameFolderSelection::Games[0].gameName
				          << " " << GameFolderSelection::Games[0].gameVersionString
				          << std::endl;
			}
		}
		else
		{
			selectedGameIndex = LauncherWindow::ExecModal();
		}
		if (selectedGameIndex >= 0)
		{
			GameLaunchInfo info = GameFolderSelection::GetLaunchInfo(selectedGameIndex);
			Engine engine(info);
			engine.Run();
		}
	}
	catch (const std::exception& e)
	{
		// Always report to stderr as well as the GUI. On a headless test rig or
		// an embedded target there may be no window to read the message in, and
		// a modal error window with nobody to close it just spins.
		exitcode = 1;
		std::cerr << "SurrealEngine error: " << e.what() << std::endl;
		for (const LogMessageLine& line : Logger::Get()->GetLog())
			std::cerr << "  log: " << line.Text << std::endl;
		if (!commandline || !commandline->HasArg("-n", "--no-launcher"))
			ErrorWindow::ExecModal(e.what(), Logger::Get()->GetLog());
	}

	DeinitWidgetResources();
	return exitcode;
}
