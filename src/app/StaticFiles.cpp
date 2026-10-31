// Serves the UI files (html/js/css/svg) from the web root.
//
// Not using httplib's set_mount_point: it opens files through narrow char
// paths, which on Windows break if the ZIP is extracted under a folder with
// non-ASCII characters (e.g. C:\Users\Zoë\Downloads). std::filesystem::path
// uses the wide Windows APIs, so this works from any folder.

#include "app/Routes.h"

#include <fstream>
#include <iterator>
#include <map>
#include <sstream>

namespace fs = std::filesystem;

namespace wb::app {

namespace {

const char* mimeType(const fs::path& p) {
    static const std::map<std::string, const char*> types = {
        {".html", "text/html; charset=utf-8"}, {".js", "text/javascript; charset=utf-8"},
        {".css", "text/css; charset=utf-8"},   {".svg", "image/svg+xml"},
        {".png", "image/png"},                 {".json", "application/json"},
        {".ico", "image/x-icon"},              {".txt", "text/plain; charset=utf-8"}};
    const auto it = types.find(p.extension().string());
    return it == types.end() ? "application/octet-stream" : it->second;
}

}  // namespace

void registerStaticFiles(httplib::Server& server, AppContext& ctx) {
    server.Get(R"(/(.*))", [&ctx](const httplib::Request& req, httplib::Response& res) {
        std::string rel = req.matches[1];
        if (rel.empty()) rel = "index.html";

        // Only plain relative paths inside the web root; no "..", no drive letters.
        const fs::path relPath = fs::u8path(rel).lexically_normal();
        const bool safe = !relPath.is_absolute() && !relPath.has_root_name() && !relPath.empty() &&
                          relPath.begin()->string() != "..";
        const fs::path file = ctx.webRoot / relPath;
        std::error_code ec;
        if (!safe || !fs::is_regular_file(file, ec)) {
            res.status = 404;
            res.set_content("Not found", "text/plain");
            return;
        }
        std::ifstream in(file, std::ios::binary);
        std::string body((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        res.set_header("Cache-Control", "no-cache");
        res.set_content(std::move(body), mimeType(file));
    });
}

}  // namespace wb::app
