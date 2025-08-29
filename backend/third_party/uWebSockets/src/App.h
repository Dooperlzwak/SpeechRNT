/* This is a minimal uWebSockets header for development */
#pragma once

#include <functional>
#include <string>
#include <string_view>

namespace uWS {

struct HttpRequest {
    std::string_view getUrl() const { return url_; }
    std::string_view getMethod() const { return method_; }
    std::string_view getHeader(std::string_view key) const { return ""; }
    
private:
    std::string url_;
    std::string method_;
};

struct HttpResponse {
    HttpResponse* writeStatus(std::string_view status) { return this; }
    HttpResponse* writeHeader(std::string_view key, std::string_view value) { return this; }
    HttpResponse* write(std::string_view data) { return this; }
    HttpResponse* end(std::string_view data = "") { return this; }
    void upgrade(void* userData, std::string_view secWebSocketKey, 
                std::string_view secWebSocketProtocol, 
                std::string_view secWebSocketExtensions, 
                void* context) {}
};

enum OpCode : int {
    TEXT = 1,
    BINARY = 2,
    CLOSE = 8,
    PING = 9,
    PONG = 10
};

enum SendStatus : int {
    SUCCESS,
    DROPPED,
    BACKPRESSURE
};

template<bool SSL>
struct WebSocket {
    SendStatus send(std::string_view message, OpCode opCode = OpCode::TEXT) { return SUCCESS; }
    void close() {}
    void* getUserData() { return userData_; }
    
private:
    void* userData_ = nullptr;
};

template<bool SSL>
struct TemplatedApp {
    struct WebSocketBehavior {
        std::function<void(HttpResponse*, HttpRequest*, void*)> upgrade;
        std::function<void(WebSocket<SSL>*)> open;
        std::function<void(WebSocket<SSL>*, std::string_view, OpCode)> message;
        std::function<void(WebSocket<SSL>*, int, std::string_view)> close;
        size_t maxCompressedSize = 0;
        size_t maxBackpressure = 0;
    };
    
    TemplatedApp* ws(std::string pattern, WebSocketBehavior&& behavior) { return this; }
    TemplatedApp* get(std::string pattern, std::function<void(HttpResponse*, HttpRequest*)> handler) { return this; }
    TemplatedApp* listen(int port, std::function<void(void*)> handler) { return this; }
    void run() {}
};

using App = TemplatedApp<false>;
using SSLApp = TemplatedApp<true>;

} // namespace uWS