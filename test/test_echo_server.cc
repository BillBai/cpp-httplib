#include <httplib.h>
#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <iostream>
#include <sys/wait.h>
#include <unistd.h>

class EchoServerTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        server_pid_ = fork();
        if (server_pid_ == 0) {
            // Child process
            execl("./example/echo_server", "echo_server", nullptr);
            exit(1); // Should not be reached
        }
        // Give the server a moment to start up
        std::this_thread::sleep_for(std::chrono::seconds(1));

        // Wait for the server to be ready
        httplib::Client cli("localhost", 8080);
        cli.set_connection_timeout(5, 0); // 5 seconds
        int retries = 5;
        while (retries > 0) {
            auto res = cli.Delete("/history");
            if (res && res->status == 204) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::seconds(1));
            retries--;
        }
        if (retries == 0) {
            kill(server_pid_, SIGKILL);
            FAIL() << "Server failed to start";
        }
    }

    static void TearDownTestSuite() {
        httplib::Client cli("localhost", 8080);
        cli.Get("/stop");
        int status;
        waitpid(server_pid_, &status, 0);
    }

    void SetUp() override {
        httplib::Client cli("localhost", 8080);
        cli.Delete("/history");
    }

    static pid_t server_pid_;
};

pid_t EchoServerTest::server_pid_ = 0;

TEST_F(EchoServerTest, PostEcho) {
    httplib::Client cli("localhost", 8080);
    auto res = cli.Post("/echo", "hello world", "text/plain");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 200);
    EXPECT_EQ(res->body, "hello world");

    auto history = cli.Get("/history");
    ASSERT_TRUE(history);
    EXPECT_EQ(history->status, 200);
    EXPECT_EQ(history->body, "[\"hello world\"]");
}

TEST_F(EchoServerTest, GetHistory) {
    httplib::Client cli("localhost", 8080);
    cli.Post("/echo", "test1", "text/plain");
    cli.Post("/echo", "test2", "text/plain");

    auto res = cli.Get("/history");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 200);
    EXPECT_EQ(res->body, "[\"test1\", \"test2\"]");
}

TEST_F(EchoServerTest, EmptyBody) {
    httplib::Client cli("localhost", 8080);
    auto res = cli.Post("/echo", "", "text/plain");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 200);
    EXPECT_EQ(res->body, "");

    auto history_res = cli.Get("/history");
    ASSERT_TRUE(history_res);
    EXPECT_EQ(history_res->status, 200);
    EXPECT_EQ(history_res->body, "[\"\"]");
}
