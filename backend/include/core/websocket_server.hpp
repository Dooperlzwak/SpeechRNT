#pragma once

#include <memory>
#include <unordered_map>
#include <string>
#include <string_view>

// Forward declarations for uWS types
namespace uWS {
    template<bool SSL> struct TemplatedApp;
    template<bool SSL> struct WebSocket;
    using App = TemplatedApp<false>;
}

namespace core {

class ClientSession;

// Forward declaration for STT health checker
namespace stt {
    class STTHealthChecker;
}

// Per-socket data structure
struct PerSocketData {
    std::string sessionId;
    uWS::WebSocket<false>* ws;
};

class WebSocketServer {
public:
    explicit WebSocketServer(int port);
    ~WebSocketServer();
    
    void start();
    void run();
    void stop();
    
    // Health monitoring integration
    void setHealthChecker(std::shared_ptr<stt::STTHealthChecker> healthChecker);
    
private:
    int port_;
    bool running_;
    std::unique_ptr<uWS::App> app_;
    std::unordered_map<std::string, std::shared_ptr<ClientSession>> sessions_;
    std::unordered_map<std::string, uWS::WebSocket<false>*> websockets_;
    
    // Health monitoring
    std::shared_ptr<stt::STTHealthChecker> health_checker_;
    
    std::string generateSessionId();
    void handleNewConnection(const std::string& sessionId, uWS::WebSocket<false>* ws);
    void handleMessage(const std::string& sessionId, const std::string& message);
    void handleBinaryMessage(const std::string& sessionId, std::string_view data);
    void handleDisconnection(const std::string& sessionId);
    
    // Health endpoint handlers
    void handleHealthCheck(uWS::HttpResponse<false>* res, uWS::HttpRequest* req);
    void handleDetailedHealthCheck(uWS::HttpResponse<false>* res, uWS::HttpRequest* req);
    void handleHealthMetrics(uWS::HttpResponse<false>* res, uWS::HttpRequest* req);
    void handleHealthHistory(uWS::HttpResponse<false>* res, uWS::HttpRequest* req);
    void handleHealthAlerts(uWS::HttpResponse<false>* res, uWS::HttpRequest* req);
    
public:
    // Message sending methods
    void sendMessage(const std::string& sessionId, const std::string& message);
    void sendBinaryMessage(const std::string& sessionId, const std::vector<uint8_t>& data);
};

} // namespace core