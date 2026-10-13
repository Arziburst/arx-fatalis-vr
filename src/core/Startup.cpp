/*
 * Copyright 2011-2022 Arx Libertatis Team (see the AUTHORS file)
 *
 * This file is part of Arx Libertatis.
 *
 * Arx Libertatis is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Arx Libertatis is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Arx Libertatis.  If not, see <http://www.gnu.org/licenses/>.
 */


#include <stddef.h>
#include <string>
#include <string_view>
#include <vector>
#include <list>
#include <set>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <ctime>

#include "platform/Platform.h"

#if ARX_PLATFORM == ARX_PLATFORM_WIN32
#include <windows.h>
#include <tlhelp32.h>
#include <stdlib.h>
#include "platform/WindowsUtils.h"
#endif

#include <boost/version.hpp>

#include <glm/glm.hpp>

#include <zlib.h>

#include "core/Benchmark.h"
#include "core/Config.h"
#include "core/Core.h"
#include "core/Version.h"

#include "gui/Console.h"
#include "gui/Credits.h"

#include "io/fs/FilePath.h"
#include "io/fs/Filesystem.h"
#include "io/fs/SystemPaths.h"
#include "io/log/CriticalLogger.h"
#include "io/log/FileLogger.h"
#include "io/log/Logger.h"

#include "math/Random.h"

#include "platform/Architecture.h"
#include "platform/Compiler.h"
#include "platform/CrashHandler.h"
#include "platform/Environment.h"
#include "platform/Process.h"
#include "platform/profiler/Profiler.h"
#include "platform/Thread.h"
#include "platform/Time.h"
#include "platform/WindowsMain.h"

#include "util/String.h"
#include "util/cmdline/CommandLine.h"

/*
 * Under macOS we want SDLmain to replace the entry point with its own.
 * This is needed to initialize NSApplication - otherwise we will later
 * crash when trying to use SDL windowing functions.
 */
#if ARX_PLATFORM == ARX_PLATFORM_MACOS
	#include <SDL_main.h>
#else
	#undef main /* in case SDL.h was already included */
#endif

namespace {

//! Runs whose logs are kept: arx.log plus arx.1.log .. arx.<LogHistory-1>.log (#129, research row F-92)
constexpr size_t LogHistory = 10;

std::string formatTime(std::time_t time, bool utc) {
	std::tm * tm = utc ? std::gmtime(&time) : std::localtime(&time);
	char buffer[32];
	if(!tm || !std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", tm)) {
		return "?";
	}
	return buffer;
}

/*!
 * DLLs that overlays, injectors and graphics mods drop next to a game executable to hook into it
 * (research row F-146; opengl32.dll is ReShade's OpenGL mode). None of them ships with this build.
 */
const char * const ProxyDlls[] = {
	"opengl32.dll", "dxgi.dll", "d3d9.dll", "d3d11.dll", "dinput8.dll", "version.dll", "winmm.dll"
};

#if ARX_PLATFORM == ARX_PLATFORM_WIN32

//! The parent process: "<exe>" (pid <n>), or why it is unknown
std::string describeParentProcess() {

	DWORD self = GetCurrentProcessId();
	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if(snapshot == INVALID_HANDLE_VALUE) {
		return "unknown (" + platform::getErrorString() + ")";
	}

	PROCESSENTRY32W entry;
	entry.dwSize = sizeof(entry);
	DWORD parent = 0;
	bool found = false;
	if(Process32FirstW(snapshot, &entry)) {
		do {
			if(entry.th32ProcessID == self) {
				parent = entry.th32ParentProcessID;
				found = true;
				break;
			}
		} while(Process32NextW(snapshot, &entry));
	}

	std::string name;
	if(found && Process32FirstW(snapshot, &entry)) {
		do {
			if(entry.th32ProcessID == parent) {
				name = platform::WideString::toUTF8(entry.szExeFile);
				break;
			}
		} while(Process32NextW(snapshot, &entry));
	}
	CloseHandle(snapshot);

	if(!found) {
		return "unknown (not in the process list)";
	}
	std::ostringstream oss;
	if(name.empty()) {
		oss << "exited";
	} else {
		oss << '"' << name << '"';
	}
	oss << " (pid " << parent << ')';
	return oss.str();
}

//! "yes" / "no" for an elevated (administrator) process token, or why it is unknown
std::string describeElevation() {
	HANDLE token = nullptr;
	if(!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
		return "unknown (" + platform::getErrorString() + ")";
	}
	TOKEN_ELEVATION elevation = { };
	DWORD size = 0;
	bool ok = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size) != FALSE;
	std::string error = ok ? std::string() : platform::getErrorString();
	CloseHandle(token);
	if(!ok) {
		return "unknown (" + error + ")";
	}
	return elevation.TokenIsElevated ? "yes" : "no";
}

#endif

/*!
 * Launch: parent "<exe>" (pid <n>), elevated yes|no, proxy DLLs next to the executable: none|<names>
 * (research row F-146): how the game was started, and whether something hooks into it, so a
 * tester's "it does not work" log names the launcher and the injector. A proxy DLL is also a [W].
 */
void logLaunchEnvironment() {

	std::ostringstream oss;
	oss << "Launch: ";
	#if ARX_PLATFORM == ARX_PLATFORM_WIN32
	oss << "parent " << describeParentProcess() << ", elevated " << describeElevation() << ", ";
	#endif

	std::string found;
	fs::path exe = platform::getExecutablePath();
	if(!exe.empty()) {
		for(const char * dll : ProxyDlls) {
			if(fs::is_regular_file(exe.parent() / dll)) {
				found += (found.empty() ? "" : " ") + std::string(dll);
			}
		}
	}
	oss << "proxy DLLs next to the executable: "
	    << (exe.empty() ? std::string("unknown (no executable path)") : found.empty() ? std::string("none") : found);
	LogInfo << oss.str();

	if(!found.empty()) {
		LogWarning << "Found " << found << " next to " << exe.filename() << " in " << exe.parent()
		           << " - an overlay, injector or graphics mod hooks into the game this way and can break"
		              " rendering or VR; remove it if the game misbehaves (see docs/vr/TESTING.md)";
	}
}

/*!
 * The log banner (#115, research row F-18): right after "Starting <name> <version>" the log says
 * which build wrote it, how it was started and where it is logging to, so every log a tester
 * sends is self-describing. The lines keep the "Key: value" shape on purpose - the run digest
 * (#131) reads them; their meaning is documented in docs/vr/TESTING.md.
 *
 * \param logFile          The log file of this run (empty when there is none).
 * \param logRotationError What logger::rotateLogs() reported, empty when the previous runs were
 *                         rotated away cleanly - otherwise this run appends to the previous log.
 */
void logStartupBanner(int argc, char ** argv, const fs::path & logFile,
                      const std::string & logRotationError) {

	// Build: <commit> on <branch>, <configuration>, <compiler>, <architecture>
	{
		std::ostringstream oss;
		oss << "Build: " << (arx_git_commit.empty() ? std::string("unknown commit") : arx_git_commit);
		if(!arx_git_branch.empty()) {
			oss << " on " << arx_git_branch;
		} else if(!arx_git_commit.empty()) {
			oss << " (detached)";
		}
		oss << ", " << (arx_build_type.empty() ? std::string("unknown configuration") : arx_build_type);
		bool notComparable = (arx_build_type == "Debug");
		#ifdef ARX_DEBUG
		if(!notComparable) {
			oss << " with debug output";
		}
		notComparable = true;
		#endif
		oss << ", " << ARX_COMPILER_VERNAME << ", " << ARX_ARCH_NAME;
		if(notComparable) {
			// Research row F-18: timings of a debug build must never be compared with release ones
			oss << " - performance numbers not comparable";
		}
		LogInfo << oss.str();
	}

	// Executable: "<path>", linked <time>, built from <built-from.txt>
	{
		fs::path exe = platform::getExecutablePath();
		std::ostringstream oss;
		oss << "Executable: ";
		std::string builtCommit;
		if(exe.empty()) {
			oss << "unknown path";
		} else {
			oss << '"' << exe.string() << '"';
			if(std::time_t linked = fs::last_write_time(exe)) {
				oss << ", linked " << formatTime(linked, false);
			}
			// scripts/build-windows.ps1 leaves built-from.txt next to arx.exe: "<sha>" or "<sha> dirty"
			fs::path stamp = exe.parent() / "built-from.txt";
			std::string builtFrom = fs::is_regular_file(stamp) ? fs::read(stamp) : std::string();
			while(!builtFrom.empty() && std::isspace(static_cast<unsigned char>(builtFrom.back()))) {
				builtFrom.pop_back();
			}
			if(builtFrom.empty()) {
				oss << ", built from unknown (no built-from.txt next to the executable)";
			} else {
				oss << ", built from " << builtFrom;
				builtCommit = builtFrom.substr(0, builtFrom.find(' '));
			}
		}
		LogInfo << oss.str();
		// built-from.txt names the commit of the last build, the compiled-in commit is refreshed only
		// when the version rule runs: a mismatch means the build id in this log is a lie
		if(!builtCommit.empty() && !arx_git_commit.empty() && builtCommit != arx_git_commit) {
			LogWarning << "built-from.txt names commit " << builtCommit << " but the executable carries version"
			              " information from " << arx_git_commit << " - the build id in this log is stale"
			              " (see docs/vr/TRAPS.md)";
		}
	}

	// Run: <local time> (<UTC>), pid <n>, command line: <arguments>
	{
		std::time_t now = std::time(nullptr);
		std::ostringstream oss;
		oss << "Run: " << formatTime(now, false) << " local (" << formatTime(now, true) << " UTC), pid "
		    << platform::getProcessId() << ", command line:";
		for(int i = 1; i < argc; i++) {
			std::string_view arg = argv[i];
			// Readable, not shell-exact: quote what has spaces, leave the rest alone
			if(arg.find_first_of(" \t\"") != std::string_view::npos) {
				oss << " \"" << util::escapeString(std::string(arg), "\"") << '"';
			} else {
				oss << ' ' << arg;
			}
		}
		if(argc <= 1) {
			oss << " (none)";
		}
		LogInfo << oss.str();
	}

	logLaunchEnvironment();

	// Log: "<path>", previous runs kept as <name>.1.<ext> .. <name>.N.<ext> | appended to the previous run's log
	if(!logFile.empty()) {
		std::ostringstream oss;
		oss << "Log: " << logFile;
		if(logRotationError.empty()) {
			std::string_view base = logFile.basename();
			std::string_view ext = logFile.ext();
			oss << ", previous runs kept as " << base << ".1" << ext << " .. " << base << '.'
			    << (LogHistory - 1) << ext;
		} else {
			oss << ", appended to the previous run's log";
		}
		LogInfo << oss.str();
		if(!logRotationError.empty()) {
			// Research row F-92: never truncate the previous run when a locked archive stops the rotation
			LogWarning << "Log rotation failed: " << logRotationError << " - nothing was deleted, this run"
			              " appends to the previous log (see docs/vr/TESTING.md)";
		}
	}

	// Steam data: "<folder>" (Steam library <library>) | not found: <why> (#79, D-004)
	LogInfo << "Steam data: " << fs::getSteamDataStatus();

	// User dir: "<path>" (default | command-line | registry | fallback) - our own, never Arx Libertatis' (#417)
	LogInfo << "User dir: " << fs::getUserDir() << " (" << fs::getUserDirSource() << ')';

}

} // anonymous namespace

int utf8_main(int argc, char ** argv) {

	// GCC -ffast-math disables denormal before main() - do the same for other compilers
	Thread::disableFloatDenormals();
	
	// Initialize Random now so that the crash handler can use it
	Random::seed();
	
	// Initialize the crash handler
	{
		CrashHandler::initialize(argc, argv);
		std::string command_line;
		for(int i = 1; i < argc; i++) {
			command_line += util::escapeString(argv[i]);
			command_line += ' ';
		}
		CrashHandler::setVariable("Command line", command_line);
	}
	
	{
		std::ostringstream oss;
		oss << ARX_COMPILER_VERNAME;
		CrashHandler::setVariable("Compiler", oss.str());
		credits::setLibraryCredits("compiler", oss.str());
		oss.str(std::string());
		oss << ARX_STDLIB_VERNAME;
		CrashHandler::setVariable("stdlib", oss.str());
		credits::setLibraryCredits("stdlib", oss.str());
		CrashHandler::setVariable("CMake", cmake_version);
		credits::setLibraryCredits("build", "CMake " + cmake_version);
		oss.str(std::string());
		oss << (BOOST_VERSION / 100000) << '.' << (BOOST_VERSION / 100 % 1000)
		    << '.' << (BOOST_VERSION % 100);
		CrashHandler::setVariable("Boost version", oss.str());
		credits::setLibraryCredits("boost", "Boost " + oss.str());
		oss.str(std::string());
		oss << GLM_VERSION_MAJOR << '.' << GLM_VERSION_MINOR << '.' << GLM_VERSION_PATCH
		    << '.' << GLM_VERSION_REVISION;
		CrashHandler::setVariable("GLM version", oss.str());
		credits::setLibraryCredits("math", "GLM " + oss.str());
		oss.str(std::string());
		oss << zlibVersion();
		CrashHandler::setVariable("zlib version (headers)", ZLIB_VERSION);
		CrashHandler::setVariable("zlib version (runtime)", oss.str());
		credits::setLibraryCredits("deflate", "zlib " + oss.str());
		credits::setLibraryCredits("image", "stb_image");
		#if !ARX_HAVE_CXX17_FROM_CHARS_FLOAT
		credits::setLibraryCredits("from_chars", "fast_float");
		#endif
	}
	
	// Also initialize the logging system early as we might need it
	Logger::initialize();
	CrashHandler::registerCrashCallback(Logger::quickShutdown);
	Logger::add(new logger::CriticalErrorDialog);
	
	// Parse the command line and process options
	ExitStatus status = parseCommandLine(argc, argv);
	
	benchmark::begin(benchmark::Startup);
	
	// Setup user, config and data directories
	if(status == RunProgram) {
		status = fs::initSystemPaths();
	}
	
	if(status == RunProgram) {
		
		// Configure the crash report location
		CrashHandler::setReportLocation(fs::getUserDir() / "crashes");
		CrashHandler::deleteOldReports(/* nb to keep = */1);
		
		// Now that data directories are initialized, create a log file
		fs::path logFile = fs::getUserDir() / "arx.log";
		std::string logRotationError;
		{
			// Keep the last runs (#129, research row F-92): a tester's second crash must not
			// overwrite the first. When a file cannot be moved nothing is deleted - this run
			// appends to the previous log and the banner says why.
			logRotationError = logger::rotateLogs(logFile, LogHistory);
			std::ios_base::openmode mode = std::ios_base::out;
			mode |= logRotationError.empty() ? std::ios_base::trunc : std::ios_base::app;
			Logger::add(new logger::File(logFile, mode));
			CrashHandler::addAttachedFile(logFile);
		}

		Logger::add(new MemoryLogger(&g_console.buffer()));

		profiler::initialize();

		// 14: Start the game already!
		LogInfo << "Starting " << arx_name << ' ' << arx_version;
		logStartupBanner(argc, argv, logFile, logRotationError);
		runGame();
		
	}
	
	benchmark::shutdown();
	
	// Shutdown the logging system
	// If there has been a critical error, a dialog will be shown now
	Logger::shutdown();
	
	CrashHandler::shutdown();
	
	Random::shutdown();
	
	return (status == ExitFailure) ? EXIT_FAILURE : EXIT_SUCCESS;
}
