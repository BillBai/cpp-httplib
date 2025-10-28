#include <httplib.h>
#include <cassert>
#include <thread>
#include <chrono>
#include <iostream>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>

void run_tests() {
    httplib::Client cli("localhost", 8080);
    cli.set_connection_timeout(5, 0); // 5 seconds

    // Retry loop to wait for the server to be ready
    int retries = 5;
    while (retries > 0) {
        std::cout << "Attempting to connect to server..." << std::endl;
        auto res = cli.Delete("/history");
        if (res && res->status == 204) {
            std::cout << "Server is ready." << std::endl;
            break;
        } else {
            if (res) {
                 std::cerr << "Server not ready. Status: " << res->status << std::endl;
            } else {
                 std::cerr << "Server not ready. Error: " << httplib::to_string(res.error()) << std::endl;
            }
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
        retries--;
    }
    assert(retries > 0);

    // Test 1: PostEcho
    cli.Delete("/history"); // Clear history before test
    auto res1 = cli.Post("/echo", "hello world", "text/plain");
    assert(res1);
    assert(res1->status == 200);
    assert(res1->body == "hello world");
    auto history1 = cli.Get("/history");
    assert(history1 && history1->status == 200 && history1->body == "[\"hello world\"]");
    std::cout << "Test 1 Passed: PostEcho" << std::endl;

    // Test 2: GetHistory
    cli.Delete("/history"); // Clear history before test
    cli.Post("/echo", "test1", "text/plain");
    cli.Post("/echo", "test2", "text/plain");
    auto res2 = cli.Get("/history");
    assert(res2);
    assert(res2->status == 200);
    assert(res2->body == "[\"test1\", \"test2\"]");
    std::cout << "Test 2 Passed: GetHistory" << std::endl;

    // Test 3: EmptyBody
    cli.Delete("/history"); // Clear history before test
    auto res3 = cli.Post("/echo", "", "text/plain");
    assert(res3);
    assert(res3->status == 200);
    assert(res3->body == "");
    auto history_res = cli.Get("/history");
    assert(history_res);
    assert(history_res->status == 200);
    assert(history_res->body == "[\"\"]");
    std::cout << "Test 3 Passed: EmptyBody" << std::endl;
}

int main() {
    pid_t server_pid = fork();
    if (server_pid == 0) {
        // Child process
        int fd = open("server_output.log", O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd == -1) {
            perror("open");
            exit(1);
        }
        dup2(fd, 1); // redirect stdout
        dup2(fd, 2); // redirect stderr
        close(fd);
        execl("./example/echo_server", "echo_server", nullptr);
        perror("execl");
        exit(1); // Should not be reached
    }

    // Give the server a moment to start up
    std::this_thread::sleep_for(std::chrono::seconds(1));

    try {
        run_tests();
    } catch (const std::exception& e) {
        std::cerr << "Tests failed: " << e.what() << std::endl;
        // Stop the server
        httplib::Client cli("localhost", 8080);
        cli.Get("/stop");
        int status;
        waitpid(server_pid, &status, 0);
        return 1;
    }

    // Stop the server
    httplib::Client cli("localhost", 8080);
    cli.Get("/stop");
    int status;
    waitpid(server_pid, &status, 0);

    std::cout << "All tests passed!" << std::endl;
    return 0;
}
