// OS-specific bits the launcher needs. Everything else in the app is portable.
#pragma once

#include <filesystem>
#include <string>

namespace wb::app {

// Folder that contains the running executable. Used instead of the current
// directory, which is unreliable when the exe is double-clicked or started
// from a shortcut.
std::filesystem::path executableDir();

// Opens the default browser. Returns false if the OS call failed.
bool openBrowser(const std::string& url);

// Seconds since the OS created this process, when the platform can tell us
// (Windows). Returns a negative value otherwise.
double secondsSinceProcessStart();

}  // namespace wb::app
