#include "core/websocket_server.hpp"
#include "core/client_session.hpp"
#include "stt/stt_health_checker.hpp"
#include "utils/logging.hpp"
#include <App.h>
#include <iostream>
#include <thread>
#include <chrono>
#include <random>
#include <sstream>
#include <iomanip>

namespace core {

WebSocketServer::WebSocketServer(int port) 
    : port_(port), running_(false), app_(nullptr) {
}

WebSocketServer::~WebSocketServer() {
    stop();
}

std::string WebSocketServer::generateSessionId() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 15);
    
    std::stringstream ss;
    for (int i = 0; i < 32; ++i) {
        if (i == 8 || i == 12 || i == 16 || i == 20) {
            ss << '-';
        }
        ss << std::hex << dis(gen);
    }
    return ss.str();
}

void WebSocketServer::start() {
    speechrnt::utils::Logger::info("Starting WebSocket server on port " + std::to_string(port_));
    running_ = true;
    
    app_ = std::make_unique<uWS::App>();
    
    // Configure WebSocket behavior
    uWS::App::WebSocketBehavior behavior;
    behavior.upgrade = [this](uWS::HttpResponse* res, uWS::HttpRequest* req, void* context) {
        std::string sessionId = generateSessionId();
        speechrnt::utils::Logger::info("WebSocket upgrade request, assigning session ID: " + sessionId);
        
        res->upgrade(
            new PerSocketData{sessionId, nullptr},
            req->getHeader("sec-websocket-key"),
            req->getHeader("sec-websocket-protocol"),
            req->getHeader("sec-websocket-extensions"),
            context
        );
    };
    
    behavior.open = [this](uWS::WebSocket<false>* ws) {
        auto* data = static_cast<PerSocketData*>(ws->getUserData());
        if (data) {
            data->ws = ws;
            handleNewConnection(data->sessionId, ws);
        }
    };
    
    behavior.message = [this](uWS::WebSocket<false>* ws, std::string_view message, uWS::OpCode opCode) {
        auto* data = static_cast<PerSocketData*>(ws->getUserData());
        if (data) {
            if (opCode == uWS::OpCode::TEXT) {
                handleMessage(data->sessionId, std::string(message));
            } else if (opCode == uWS::OpCode::BINARY) {
                handleBinaryMessage(data->sessionId, message);
            }
        }
    };
    
    behavior.close = [this](uWS::WebSocket<false>* ws, int code, std::string_view message) {
        auto* data = static_cast<PerSocketData*>(ws->getUserData());
        if (data) {
            handleDisconnection(data->sessionId);
            delete data;
        }
    };
    
    app_->ws("/*", std::move(behavior));
    
    // Add HTTP endpoints for health monitoring
    app_->get("/health", [this](auto* res, auto* req) {
        handleHealthCheck(res, req);
    });
    
    app_->get("/health/detailed", [this](auto* res, auto* req) {
        handleDetailedHealthCheck(res, req);
    });
    
    app_->get("/health/metrics", [this](auto* res, auto* req) {
        handleHealthMetrics(res, req);
    });
    
    app_->get("/health/history", [this](auto* res, auto* req) {
        handleHealthHistory(res, req);
    });
    
    app_->get("/health/alerts", [this](auto* res, auto* req) {
        handleHealthAlerts(res, req);
    });
}

void WebSocketServer::run() {
    if (!app_) {
        speechrnt::utils::Logger::error("Server not started. Call start() first.");
        return;
    }
    
    app_->listen(port_, [this](auto* listen_socket) {
        if (listen_socket) {
            speechrnt::utils::Logger::info("WebSocket server listening on port " + std::to_string(port_));
        } else {
            speechrnt::utils::Logger::error("Failed to listen on port " + std::to_string(port_));
            running_ = false;
        }
    });
    
    if (running_) {
        speechrnt::utils::Logger::info("Server started successfully. Press Ctrl+C to stop.");
        app_->run();
    }
}

void WebSocketServer::stop() {
    if (running_) {
        speechrnt::utils::Logger::info("Stopping WebSocket server");
        running_ = false;
        sessions_.clear();
        websockets_.clear();
        app_.reset();
    }
}

void WebSocketServer::setHealthChecker(std::shared_ptr<stt::STTHealthChecker> healthChecker) {
    health_checker_ = healthChecker;
    speechrnt::utils::Logger::info("Health checker integrated with WebSocket server");
}

void WebSocketServer::sendMessage(const std::string& sessionId, const std::string& message) {
    auto wsIt = websockets_.find(sessionId);
    if (wsIt != websockets_.end()) {
        wsIt->second->send(message, uWS::OpCode::TEXT);
        speechrnt::utils::Logger::debug("Sent JSON message to " + sessionId + ": " + message);
    } else {
        speechrnt::utils::Logger::warn("Attempted to send message to unknown session: " + sessionId);
    }
}

void WebSocketServer::sendBinaryMessage(const std::string& sessionId, const std::vector<uint8_t>& data) {
    auto wsIt = websockets_.find(sessionId);
    if (wsIt != websockets_.end()) {
        std::string_view binaryData(reinterpret_cast<const char*>(data.data()), data.size());
        wsIt->second->send(binaryData, uWS::OpCode::BINARY);
        speechrnt::utils::Logger::debug("Sent binary message to " + sessionId + ", size: " + std::to_string(data.size()));
    } else {
        speechrnt::utils::Logger::warn("Attempted to send binary data to unknown session: " + sessionId);
    }
}

void WebSocketServer::handleNewConnection(const std::string& sessionId, uWS::WebSocket<false>* ws) {
    speechrnt::utils::Logger::info("New client connection: " + sessionId);
    
    auto session = std::make_shared<ClientSession>(sessionId);
    session->setWebSocketServer(this);
    sessions_[sessionId] = session;
    websockets_[sessionId] = ws;
    
    speechrnt::utils::Logger::info("Created new session. Total active sessions: " + 
                       std::to_string(sessions_.size()));
}

void WebSocketServer::handleMessage(const std::string& sessionId, const std::string& message) {
    speechrnt::utils::Logger::debug("JSON message from " + sessionId + ": " + message);
    
    auto it = sessions_.find(sessionId);
    if (it != sessions_.end()) {
        it->second->handleMessage(message);
    } else {
        speechrnt::utils::Logger::warn("Message from unknown session: " + sessionId);
    }
}

void WebSocketServer::handleBinaryMessage(const std::string& sessionId, std::string_view data) {
    speechrnt::utils::Logger::debug("Binary message from " + sessionId + ", size: " + std::to_string(data.size()));
    
    auto it = sessions_.find(sessionId);
    if (it != sessions_.end()) {
        it->second->handleBinaryMessage(data);
    } else {
        speechrnt::utils::Logger::warn("Binary message from unknown session: " + sessionId);
    }
}

void WebSocketServer::handleDisconnection(const std::string& sessionId) {
    speechrnt::utils::Logger::info("Client disconnected: " + sessionId);
    
    auto sessionIt = sessions_.find(sessionId);
    if (sessionIt != sessions_.end()) {
        sessions_.erase(sessionIt);
    }
    
    auto wsIt = websockets_.find(sessionId);
    if (wsIt != websockets_.end()) {
        websockets_.erase(wsIt);
    }
    
    speechrnt::utils::Logger::info("Session removed. Remaining active sessions: " + 
                       std::to_string(sessions_.size()));
}

void WebSocketServer::handleHealthCheck(uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
    try {
        if (!health_checker_) {
            res->writeStatus("503 Service Unavailable")
               ->writeHeader("Content-Type", "application/json")
               ->end("{\"status\":\"unavailable\",\"message\":\"Health checker not initialized\"}");
            return;
        }
        
        auto healthStatus = health_checker_->checkHealth(false);
        std::string statusStr;
        
        switch (healthStatus.overall_status) {
            case stt::HealthStatus::HEALTHY:
                statusStr = "healthy";
                res->writeStatus("200 OK");
                break;
            case stt::HealthStatus::DEGRADED:
                statusStr = "degraded";
                res->writeStatus("200 OK");
                break;
            case stt::HealthStatus::UNHEALTHY:
                statusStr = "unhealthy";
                res->writeStatus("503 Service Unavailable");
                break;
            case stt::HealthStatus::CRITICAL:
                statusStr = "critical";
                res->writeStatus("503 Service Unavailable");
                break;
            default:
                statusStr = "unknown";
                res->writeStatus("503 Service Unavailable");
                break;
        }
        
        std::ostringstream json;
        json << "{\n";
        json << "  \"status\": \"" << statusStr << "\",\n";
        json << "  \"service\": \"SpeechRNT STT\",\n";
        json << "  \"message\": \"" << healthStatus.overall_message << "\",\n";
        json << "  \"timestamp\": " << std::chrono::duration_cast<std::chrono::milliseconds>(
                   healthStatus.timestamp.time_since_epoch()).count() << ",\n";
        json << "  \"check_time_ms\": " << healthStatus.total_check_time_ms << ",\n";
        json << "  \"can_accept_requests\": " << (health_checker_->canAcceptNewRequests() ? "true" : "false") << ",\n";
        json << "  \"system_load_factor\": " << health_checker_->getSystemLoadFactor() << "\n";
        json << "}";
        
        res->writeHeader("Content-Type", "application/json")
           ->writeHeader("Cache-Control", "no-cache")
           ->end(json.str());
           
    } catch (const std::exception& e) {
        speechrnt::utils::Logger::error("Exception in health check endpoint: " + std::string(e.what()));
        res->writeStatus("500 Internal Server Error")
           ->writeHeader("Content-Type", "application/json")
           ->end("{\"status\":\"error\",\"message\":\"Internal server error\"}");
    }
}

void WebSocketServer::handleDetailedHealthCheck(uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
    try {
        if (!health_checker_) {
            res->writeStatus("503 Service Unavailable")
               ->writeHeader("Content-Type", "application/json")
               ->end("{\"status\":\"unavailable\",\"message\":\"Health checker not initialized\"}");
            return;
        }
        
        auto healthStatus = health_checker_->checkHealth(true);
        std::string jsonResponse = health_checker_->exportHealthStatusJSON(false);
        
        std::string httpStatus = "200 OK";
        if (healthStatus.overall_status == stt::HealthStatus::UNHEALTHY || 
            healthStatus.overall_status == stt::HealthStatus::CRITICAL) {
            httpStatus = "503 Service Unavailable";
        }
        
        res->writeStatus(httpStatus)
           ->writeHeader("Content-Type", "application/json")
           ->writeHeader("Cache-Control", "no-cache")
           ->end(jsonResponse);
           
    } catch (const std::exception& e) {
        speechrnt::utils::Logger::error("Exception in detailed health check endpoint: " + std::string(e.what()));
        res->writeStatus("500 Internal Server Error")
           ->writeHeader("Content-Type", "application/json")
           ->end("{\"status\":\"error\",\"message\":\"Internal server error\"}");
    }
}

void WebSocketServer::handleHealthMetrics(uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
    try {
        if (!health_checker_) {
            res->writeStatus("503 Service Unavailable")
               ->writeHeader("Content-Type", "application/json")
               ->end("{\"status\":\"unavailable\",\"message\":\"Health checker not initialized\"}");
            return;
        }
        
        auto metrics = health_checker_->getHealthMetrics();
        auto monitoringStats = health_checker_->getMonitoringStats();
        
        std::ostringstream json;
        json << "{\n";
        json << "  \"health_metrics\": {\n";
        
        size_t metricIndex = 0;
        for (const auto& [key, value] : metrics) {
            json << "    \"" << key << "\": " << value;
            if (++metricIndex < metrics.size()) json << ",";
            json << "\n";
        }
        
        json << "  },\n";
        json << "  \"monitoring_stats\": {\n";
        
        size_t statIndex = 0;
        for (const auto& [key, value] : monitoringStats) {
            json << "    \"" << key << "\": " << value;
            if (++statIndex < monitoringStats.size()) json << ",";
            json << "\n";
        }
        
        json << "  },\n";
        json << "  \"healthy_instances\": [\n";
        
        auto healthyInstances = health_checker_->getHealthyInstances();
        for (size_t i = 0; i < healthyInstances.size(); ++i) {
            json << "    \"" << healthyInstances[i] << "\"";
            if (i < healthyInstances.size() - 1) json << ",";
            json << "\n";
        }
        
        json << "  ],\n";
        json << "  \"recommended_instance\": \"" << health_checker_->getRecommendedInstance() << "\"\n";
        json << "}";
        
        res->writeStatus("200 OK")
           ->writeHeader("Content-Type", "application/json")
           ->writeHeader("Cache-Control", "no-cache")
           ->end(json.str());
           
    } catch (const std::exception& e) {
        speechrnt::utils::Logger::error("Exception in health metrics endpoint: " + std::string(e.what()));
        res->writeStatus("500 Internal Server Error")
           ->writeHeader("Content-Type", "application/json")
           ->end("{\"status\":\"error\",\"message\":\"Internal server error\"}");
    }
}

void WebSocketServer::handleHealthHistory(uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
    try {
        if (!health_checker_) {
            res->writeStatus("503 Service Unavailable")
               ->writeHeader("Content-Type", "application/json")
               ->end("{\"status\":\"unavailable\",\"message\":\"Health checker not initialized\"}");
            return;
        }
        
        // Parse hours parameter from query string
        int hours = 24; // default
        std::string query = std::string(req->getQuery());
        size_t hoursPos = query.find("hours=");
        if (hoursPos != std::string::npos) {
            try {
                hours = std::stoi(query.substr(hoursPos + 6));
                hours = std::max(1, std::min(hours, 168)); // Limit to 1-168 hours (1 week)
            } catch (...) {
                hours = 24; // fallback to default
            }
        }
        
        auto history = health_checker_->getHealthHistory(hours);
        
        std::ostringstream json;
        json << "{\n";
        json << "  \"hours_requested\": " << hours << ",\n";
        json << "  \"entries_count\": " << history.size() << ",\n";
        json << "  \"history\": [\n";
        
        for (size_t i = 0; i < history.size(); ++i) {
            const auto& entry = history[i];
            json << "    {\n";
            json << "      \"timestamp\": " << std::chrono::duration_cast<std::chrono::milliseconds>(
                       entry.timestamp.time_since_epoch()).count() << ",\n";
            json << "      \"overall_status\": \"" << 
                    (entry.overall_status == stt::HealthStatus::HEALTHY ? "healthy" :
                     entry.overall_status == stt::HealthStatus::DEGRADED ? "degraded" :
                     entry.overall_status == stt::HealthStatus::UNHEALTHY ? "unhealthy" :
                     entry.overall_status == stt::HealthStatus::CRITICAL ? "critical" : "unknown") << "\",\n";
            json << "      \"message\": \"" << entry.overall_message << "\",\n";
            json << "      \"check_time_ms\": " << entry.total_check_time_ms << ",\n";
            json << "      \"component_count\": " << entry.component_health.size() << "\n";
            json << "    }";
            if (i < history.size() - 1) json << ",";
            json << "\n";
        }
        
        json << "  ]\n";
        json << "}";
        
        res->writeStatus("200 OK")
           ->writeHeader("Content-Type", "application/json")
           ->writeHeader("Cache-Control", "no-cache")
           ->end(json.str());
           
    } catch (const std::exception& e) {
        speechrnt::utils::Logger::error("Exception in health history endpoint: " + std::string(e.what()));
        res->writeStatus("500 Internal Server Error")
           ->writeHeader("Content-Type", "application/json")
           ->end("{\"status\":\"error\",\"message\":\"Internal server error\"}");
    }
}

void WebSocketServer::handleHealthAlerts(uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
    try {
        if (!health_checker_) {
            res->writeStatus("503 Service Unavailable")
               ->writeHeader("Content-Type", "application/json")
               ->end("{\"status\":\"unavailable\",\"message\":\"Health checker not initialized\"}");
            return;
        }
        
        auto alerts = health_checker_->getActiveAlerts();
        
        std::ostringstream json;
        json << "{\n";
        json << "  \"active_alerts_count\": " << alerts.size() << ",\n";
        json << "  \"alerts\": [\n";
        
        for (size_t i = 0; i < alerts.size(); ++i) {
            const auto& alert = alerts[i];
            json << "    {\n";
            json << "      \"alert_id\": \"" << alert.alert_id << "\",\n";
            json << "      \"component_name\": \"" << alert.component_name << "\",\n";
            json << "      \"severity\": \"" << 
                    (alert.severity == stt::HealthStatus::HEALTHY ? "healthy" :
                     alert.severity == stt::HealthStatus::DEGRADED ? "degraded" :
                     alert.severity == stt::HealthStatus::UNHEALTHY ? "unhealthy" :
                     alert.severity == stt::HealthStatus::CRITICAL ? "critical" : "unknown") << "\",\n";
            json << "      \"message\": \"" << alert.message << "\",\n";
            json << "      \"timestamp\": " << std::chrono::duration_cast<std::chrono::milliseconds>(
                       alert.timestamp.time_since_epoch()).count() << ",\n";
            json << "      \"acknowledged\": " << (alert.acknowledged ? "true" : "false") << ",\n";
            json << "      \"context\": {\n";
            
            size_t contextIndex = 0;
            for (const auto& [key, value] : alert.context) {
                json << "        \"" << key << "\": \"" << value << "\"";
                if (++contextIndex < alert.context.size()) json << ",";
                json << "\n";
            }
            
            json << "      }\n";
            json << "    }";
            if (i < alerts.size() - 1) json << ",";
            json << "\n";
        }
        
        json << "  ]\n";
        json << "}";
        
        res->writeStatus("200 OK")
           ->writeHeader("Content-Type", "application/json")
           ->writeHeader("Cache-Control", "no-cache")
           ->end(json.str());
           
    } catch (const std::exception& e) {
        speechrnt::utils::Logger::error("Exception in health alerts endpoint: " + std::string(e.what()));
        res->writeStatus("500 Internal Server Error")
           ->writeHeader("Content-Type", "application/json")
           ->end("{\"status\":\"error\",\"message\":\"Internal server error\"}");
    }
}

} // namespace core