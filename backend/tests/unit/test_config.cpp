#include <gtest/gtest.h>
#include "utils/config.hpp"

class ConfigTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Setup test environment
    }
    
    void TearDown() override {
        // Cleanup
    }
};

TEST_F(ConfigTest, DefaultValues) {
    auto config = utils::Config::load("nonexistent.json");
    EXPECT_EQ(config.getPort(), 8080);
    EXPECT_EQ(config.getLogLevel(), "INFO");
}

// TODO: Add more tests when JSON loading is implemented