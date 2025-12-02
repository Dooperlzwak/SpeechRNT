#include "utils/logging.hpp"
#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace speechrnt {
namespace utils {

bool Logger::initialized_ = false;

void Logger::initialize() {
  if (!initialized_) {
    initialized_ = true;
    info("Logger initialized");
  }
}

void Logger::info(const std::string &message) {
  std::cout << "[INFO] " << message << std::endl;
}

void Logger::warn(const std::string &message) {
  std::cout << "[WARN] " << message << std::endl;
}

void Logger::error(const std::string &message) {
  std::cerr << "[ERROR] " << message << std::endl;
}

void Logger::debug(const std::string &message) {
  std::cout << "[DEBUG] " << message << std::endl;
}

} // namespace utils
} // namespace speechrnt