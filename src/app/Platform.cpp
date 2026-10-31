#include "app/Platform.h"

#include <cstdint>
#include <cstdlib>
#include <iterator>

#if defined(_WIN32)
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <shellapi.h>
#elif defined(__APPLE__)
#  include <mach-o/dyld.h>
#  include <vector>
#else
#  include <unistd.h>
#endif

namespace wb::app {

#if defined(_WIN32)

std::filesystem::path executableDir() {
    wchar_t buf[MAX_PATH * 4];
    const DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(std::size(buf)));
    return std::filesystem::path(std::wstring(buf, n)).parent_path();
}

bool openBrowser(const std::string& url) {
    const std::wstring wide(url.begin(), url.end());  // URL is plain ASCII
    const auto rc = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", wide.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    return rc > 32;  // ShellExecute's documented success threshold
}

double secondsSinceProcessStart() {
    FILETIME created, exited, kernel, user, now;
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) return -1.0;
    GetSystemTimePreciseAsFileTime(&now);
    auto ticks = [](const FILETIME& ft) {
        return (static_cast<unsigned long long>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    };
    return static_cast<double>(ticks(now) - ticks(created)) * 1e-7;  // FILETIME is in 100 ns units
}

#else

std::filesystem::path executableDir() {
#  if defined(__APPLE__)
    std::uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> buf(size + 1, '\0');
    if (_NSGetExecutablePath(buf.data(), &size) == 0) {
        return std::filesystem::weakly_canonical(buf.data()).parent_path();
    }
    return std::filesystem::current_path();
#  else
    std::error_code ec;
    const auto p = std::filesystem::read_symlink("/proc/self/exe", ec);
    return ec ? std::filesystem::current_path() : p.parent_path();
#  endif
}

bool openBrowser(const std::string& url) {
#  if defined(__APPLE__)
    const std::string cmd = "open '" + url + "'";
#  else
    const std::string cmd = "xdg-open '" + url + "' >/dev/null 2>&1 &";
#  endif
    return std::system(cmd.c_str()) == 0;
}

double secondsSinceProcessStart() { return -1.0; }

#endif

}  // namespace wb::app
