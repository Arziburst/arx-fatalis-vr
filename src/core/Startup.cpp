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
#include <stdlib.h>
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

std::string formatTime(std::time_t time, bool utc) {
	std::tm * tm = utc ? std::gmtime(&time) : std::localtime(&time);
	char buffer[32];
	if(!tm || !std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", tm)) {
		return "?";
	}
	return buffer;
}

/*!
 * The log banner (#115, research row F-18): right after "Starting <name> <version>" the log says
 * which build wrote it and how it was started, so every log a tester sends is self-describing.
 * The lines keep the "Key: value" shape on purpose - the run digest (#131) reads them; their
 * meaning is documented in docs/vr/TESTING.md.
 */
void logStartupBanner(int argc, char ** argv) {

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
				oss << " \"" << util::escapeString(std::string(arg), "\\\"") << '"';
			} else {
				oss << ' ' << arg;
			}
		}
		if(argc <= 1) {
			oss << " (none)";
		}
		LogInfo << oss.str();
	}

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
		{
			fs::path logFile = fs::getUserDir() / "arx.log";
			Logger::add(new logger::File(logFile));
			CrashHandler::addAttachedFile(logFile);
		}
		
		Logger::add(new MemoryLogger(&g_console.buffer()));
		
		profiler::initialize();
		
		// 14: Start the game already!
		LogInfo << "Starting " << arx_name << ' ' << arx_version;
		logStartupBanner(argc, argv);
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
