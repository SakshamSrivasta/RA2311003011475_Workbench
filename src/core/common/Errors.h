// Error types shared by every core module.
//
// The core never prints or exits. It throws a WorkbenchError whose message
// says what went wrong and what the user can do about it, so the UI can show
// the message as-is.
#pragma once

#include <stdexcept>
#include <string>

namespace wb {

enum class ErrorCode {
    InvalidParameter,  // a numeric input is out of range or non-finite
    MalformedFile,     // a file could not be parsed
    EmptyFile,         // a file has no usable content
    NumericalFailure,  // the solver hit a zero pivot or produced non-finite values
    Cancelled,         // the user cancelled a long-running operation
    DeviceFailure      // a (simulated) hardware adapter reported a fault
};

const char* toString(ErrorCode code) noexcept;

class WorkbenchError : public std::runtime_error {
public:
    WorkbenchError(ErrorCode code, const std::string& message)
        : std::runtime_error(message), code_(code) {}

    ErrorCode code() const noexcept { return code_; }

private:
    ErrorCode code_;
};

// Throws InvalidParameter unless `value` is finite and strictly positive.
void requirePositiveFinite(double value, const char* name, const char* unit);

// Throws InvalidParameter unless `value` is finite.
void requireFinite(double value, const char* name, const char* unit);

}  // namespace wb
