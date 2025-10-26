#include <httplib.h>

int main(void) {
    using namespace httplib;

    Server svr;

    svr.Get("/status", [](const Request &, Response &res) {
        res.set_content("OK", "text/plain");
    });

    svr.Post("/echo", [](const Request &req, Response &res) {
        res.set_content(req.body, "text/plain");
    });

    svr.listen("0.0.0.0", 8080);
}
