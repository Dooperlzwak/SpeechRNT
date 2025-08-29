#pragma once

#include <string>
#include <memory>

namespace utils {

class Config {
public:
    static Config load(const std::string& configPath);
    
    int getPort() const { return port_; }
    std::string getLogLevel() const { return logLevel_; }
    
private:
    Config() = default;
    
    int port_ = 8080;
    std::string logLevel_ = "INFO";
};

} // namespace utils