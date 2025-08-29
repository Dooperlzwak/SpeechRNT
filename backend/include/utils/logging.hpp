#pragma once

#include <string>

namespace utils {

class Logger {
public:
    static void initialize();
    static void info(const std::string& message);
    static void warn(const std::string& message);
    static void error(const std::string& message);
    static void debug(const std::string& message);
    
private:
    static bool initialized_;
};

} // namespace utils