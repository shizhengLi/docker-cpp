#include <docker-cpp/core/error.hpp>
#include <sstream>

namespace docker_cpp {

const ContainerErrorCategory& getContainerErrorCategory() {
    static ContainerErrorCategory category;
    return category;
}

ContainerError::ContainerError(ErrorCode code, const std::string& message)
    : error_code_(code), message_(message) {
}

ContainerError::ContainerError(const ContainerError& other) noexcept
    : error_code_(other.error_code_), message_(other.message_), full_message_(other.full_message_) {
}

ContainerError::ContainerError(ContainerError&& other) noexcept
    : error_code_(other.error_code_), message_(std::move(other.message_)), full_message_(std::move(other.full_message_)) {
    other.error_code_ = ErrorCode::UNKNOWN_ERROR;
}

ContainerError& ContainerError::operator=(const ContainerError& other) noexcept {
    if (this != &other) {
        error_code_ = other.error_code_;
        message_ = other.message_;
        full_message_ = other.full_message_;
    }
    return *this;
}

ContainerError& ContainerError::operator=(ContainerError&& other) noexcept {
    if (this != &other) {
        error_code_ = other.error_code_;
        message_ = std::move(other.message_);
        full_message_ = std::move(other.full_message_);

        other.error_code_ = ErrorCode::UNKNOWN_ERROR;
    }
    return *this;
}

const char* ContainerError::what() const noexcept {
    if (full_message_.empty()) {
        std::ostringstream oss;
        oss << "[" << getContainerErrorCategory().name() << " " << static_cast<int>(error_code_) << "] "
            << getContainerErrorCategory().message(static_cast<int>(error_code_));

        if (!message_.empty()) {
            oss << ": " << message_;
        }

        full_message_ = oss.str();
    }
    return full_message_.c_str();
}

ErrorCode ContainerError::getErrorCode() const noexcept {
    return error_code_;
}

std::error_code ContainerError::code() const noexcept {
    return std::error_code(static_cast<int>(error_code_), getContainerErrorCategory());
}

ContainerError makeSystemError(ErrorCode code, const std::system_error& sys_error) {
    std::ostringstream oss;
    oss << sys_error.what() << " (system error " << sys_error.code().value() << ")";
    return ContainerError(code, oss.str());
}

} // namespace docker_cpp