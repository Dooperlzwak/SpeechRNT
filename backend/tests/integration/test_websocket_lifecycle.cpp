#include <gtest/gtest.h>
#include "core/websocket_server.hpp"
#include <thread>
#include <chrono>
#include <memory>

class WebSocketLifecycleTest : public ::testing::Test {
protected:
    void SetUp() override {
        server = std::make_unique<core::WebSocketServer>(8082);
    }
    
    void TearDown() override {
        if (server) {
            server->stop();
        }
        server.reset();
    }
    
    std::unique_ptr<core::WebSocketServer> server;
};

TEST_F(WebSocketLifecycleTest, ServerStartupAndShutdown) {
    // Test that server can start and stop cleanly
    EXPECT_NO_THROW(server->start());
    
    // Let server initialize
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    EXPECT_NO_THROW(server->stop());
}

TEST_F(WebSocketLifecycleTest, MultipleStartStopCycles) {
    // Test multiple start/stop cycles
    for (int i = 0; i < 3; ++i) {
        EXPECT_NO_THROW(server->start());
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        EXPECT_NO_THROW(server->stop());
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

// Note: Full WebSocket client connection tests would require a WebSocket client library
// For now, we focus on server lifecycle management