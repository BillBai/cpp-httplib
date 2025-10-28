#include <httplib.h>
#include <vector>
#include <string>
#include <mutex>
#include <sstream>

int main(void) {
    using namespace httplib;

    Server svr;
    std::vector<std::string> history;
    std::mutex mtx;

    svr.Post("/echo", [&](const Request& req, Response& res) {
        {
            std::lock_guard<std::mutex> lock(mtx);
            history.push_back(req.body);
        }
        res.set_content(req.body, "text/plain");
    });

    svr.Get("/history", [&](const Request&, Response& res) {
        std::stringstream ss;
        ss << "[";
        {
            std::lock_guard<std::mutex> lock(mtx);
            for (size_t i = 0; i < history.size(); ++i) {
                ss << "\"" << history[i] << "\"";
                if (i < history.size() - 1) {
                    ss << ", ";
                }
            }
        }
        ss << "]";
        res.set_content(ss.str(), "application/json");
    });

    svr.Get("/stop",
            [&](const Request & /*req*/, Response & /*res*/) { svr.stop(); });

    svr.Delete("/history", [&](const Request&, Response& res) {
        {
            std::lock_guard<std::mutex> lock(mtx);
            history.clear();
        }
        res.status = 204;
    });

    svr.listen("0.0.0.0", 8080);

    return 0;
}
