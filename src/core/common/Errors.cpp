#include "core/common/Errors.h"

#include <cmath>
#include <sstream>

namespace wb {

const char* toString(ErrorCode code) noexcept {
    switch (code) {
        case ErrorCode::InvalidParameter: return "InvalidParameter";
        case ErrorCode::MalformedFile:    return "MalformedFile";
        case ErrorCode::EmptyFile:        return "EmptyFile";
        case ErrorCode::NumericalFailure: return "NumericalFailure";
        case ErrorCode::Cancelled:        return "Cancelled";
        case ErrorCode::DeviceFailure:    return "DeviceFailure";
    }
    return "Unknown";
}

void requireFinite(double value, const char* name, const char* unit) {
    if (!std::isfinite(value)) {
        std::ostringstream msg;
        msg << name << " must be a finite number in " << unit
            << " (got " << value << "). Enter a numeric value.";
        throw WorkbenchError(ErrorCode::InvalidParameter, msg.str());
    }
}

void requirePositiveFinite(double value, const char* name, const char* unit) {
    requireFinite(value, name, unit);
    if (value <= 0.0) {
        std::ostringstream msg;
        msg << name << " must be greater than zero in " << unit
            << " (got " << value << "). Enter a positive value.";
        throw WorkbenchError(ErrorCode::InvalidParameter, msg.str());
    }
}

}  // namespace wb
