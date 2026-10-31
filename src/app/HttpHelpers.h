// Glue between httplib requests and the core: JSON parsing, field
// validation, and turning exceptions into HTTP errors the UI can show.
#pragma once

#include "core/common/Errors.h"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <string>

namespace wb::app {

using nlohmann::json;

// Error messages can quote bytes from a broken upload, which may not be valid
// UTF-8; replace those instead of letting dump() throw.
inline std::string toText(const json& j) { return j.dump(-1, ' ', false, json::error_handler_t::replace); }

// Turns the exception currently being handled into a JSON error body:
//   { "error": "<message for the user>", "code": "<ErrorCode>" }
// 400 for bad input, 409 for device/state problems, 500 for bugs.
inline void writeCurrentError(httplib::Response& res) {
    auto write = [&](int status, const std::string& msg, const std::string& code) {
        res.status = status;
        res.set_content(toText(json{{"error", msg}, {"code", code}}), "application/json");
    };
    try {
        throw;
    } catch (const WorkbenchError& e) {
        const bool stateProblem = e.code() == ErrorCode::DeviceFailure || e.code() == ErrorCode::Cancelled;
        write(stateProblem ? 409 : 400, e.what(), toString(e.code()));
    } catch (const json::exception& e) {
        write(400, std::string("Request was not valid JSON: ") + e.what(), "BadRequest");
    } catch (const std::exception& e) {
        write(500, std::string("Internal error: ") + e.what(), "Internal");
    } catch (...) {
        write(500, "Internal error.", "Internal");
    }
}

template <typename Fn>
void respondJson(httplib::Response& res, Fn&& fn) {
    try {
        res.set_content(toText(fn()), "application/json");
    } catch (...) {
        writeCurrentError(res);
    }
}

// Same error handling, but the success body is a CSV download.
template <typename Fn>
void respondCsv(httplib::Response& res, const std::string& filename, Fn&& fn) {
    try {
        std::string csv = fn();
        res.set_header("Content-Disposition", "attachment; filename=\"" + filename + "\"");
        res.set_content(std::move(csv), "text/csv");
    } catch (...) {
        writeCurrentError(res);
    }
}

inline json parseBody(const httplib::Request& req) {
    if (req.body.empty()) return json::object();
    json body = json::parse(req.body);
    if (!body.is_object()) throw WorkbenchError(ErrorCode::InvalidParameter, "Request body must be a JSON object.");
    return body;
}

// Optional numeric field with a default. A present-but-wrong value is an error,
// never silently replaced by the default.
inline double numberField(const json& body, const char* key, double fallback) {
    const auto it = body.find(key);
    if (it == body.end() || it->is_null()) return fallback;
    if (!it->is_number()) {
        throw WorkbenchError(ErrorCode::InvalidParameter, std::string("Field '") + key + "' must be a number.");
    }
    return it->get<double>();
}

inline int intField(const json& body, const char* key, int fallback) {
    const double v = numberField(body, key, fallback);
    if (v != static_cast<double>(static_cast<long long>(v)) || v > 2e9 || v < -2e9) {
        throw WorkbenchError(ErrorCode::InvalidParameter, std::string("Field '") + key + "' must be a whole number.");
    }
    return static_cast<int>(v);
}

}  // namespace wb::app
