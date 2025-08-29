#include <gtest/gtest.h>
#include "core/websocket_server.hpp"
#include <thread>
#include <chrono>

class WebSocketServerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Use a different port for testing to avoid conflicts
        server = std::make_unique<core::WebSocketServer>(8081);
    }
    
    void TearDown() override {
        if (server) {
            server->stop();
        }
        server.reset();
    }
    
    std::unique_ptr<core::WebSocketServer> server;
};

TEST_F(WebSocketServerTest, ServerCreation) {
    EXPECT_NE(server, nullptr);
}

TEST_F(WebSocketServerTest, ServerStartStop) {
    EXPECT_NO_THROW(server->start());
    
    // Give the server a moment to initialize
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    EXPECT_NO_THROW(server->stop());
}

TEST_F(WebSocketServerTest, ServerStartTwice) {
    server->start();
    
    // Starting again should not cause issues
    EXPECT_NO_THROW(server->start());
    
    server->stop();
}

TEST_F(WebSocketServerTest, ServerStopWithoutStart) {
    // Stopping without starting should be safe
    EXPECT_NO_THROW(server->stop());
}

// Note: More comprehensive integration tests would require actual WebSocket clients
// These would be better placed in integration tests with real network connections