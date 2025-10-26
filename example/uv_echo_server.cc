#include <iostream>
#include <uv.h>
#include <string>
#include <vector>
#include <memory>
#include <algorithm>

#include "httplib.h"

// 前向声明
struct ClientConnection;
void process_request(ClientConnection* client);

// 用于写操作的请求结构体，包含缓冲区和客户端连接信息
typedef struct {
    uv_write_t req;
    uv_buf_t buf;
    ClientConnection* client;
} write_req_t;

// 每个客户端连接的状态
struct ClientConnection {
    uv_tcp_t handle;
    std::string buffer;
    httplib::Request req;
    enum {
        REQUEST_LINE,
        HEADERS,
        BODY,
        COMPLETE
    } state = REQUEST_LINE;
    size_t content_length = 0;

    ClientConnection(uv_loop_t* loop) {
        uv_tcp_init(loop, &handle);
        handle.data = this;
    }
};

// 句柄关闭回调
void on_close(uv_handle_t* handle) {
    delete static_cast<ClientConnection*>(handle->data);
}

// 写操作完成回调
void on_write_end(uv_write_t* req, int status) {
    write_req_t* wr = (write_req_t*) req;
    if (status) {
        fprintf(stderr, "Write error %s\n", uv_strerror(status));
    }
    // 释放为写操作分配的缓冲区
    free(wr->buf.base);
    // 关闭客户端句柄
    uv_close((uv_handle_t*)&wr->client->handle, on_close);
    // 释放写请求结构体
    free(wr);
}

// 数据读取回调
void on_read(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
    auto* client = static_cast<ClientConnection*>(stream->data);

    if (nread > 0) {
        client->buffer.append(buf->base, nread);
        process_request(client);
    } else if (nread < 0) {
        if (nread != UV_EOF) {
            fprintf(stderr, "Read error %s\n", uv_strerror(nread));
        }
        uv_close((uv_handle_t*)stream, on_close);
    }

    if (buf->base) {
        free(buf->base);
    }
}

// 缓冲区分配回调
void alloc_buffer(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
    buf->base = (char*)malloc(suggested_size);
    buf->len = suggested_size;
}

// HTTP 请求处理状态机
void process_request(ClientConnection* client) {
    if (client->state == ClientConnection::COMPLETE) {
        return;
    }

    // 1. 解析请求行
    if (client->state == ClientConnection::REQUEST_LINE) {
        size_t pos = client->buffer.find("\r\n");
        if (pos == std::string::npos) return;

        std::string line = client->buffer.substr(0, pos);
        client->buffer.erase(0, pos + 2);

        httplib::detail::split(line.data(), line.data() + line.size(), ' ', [&](const char *b, const char *e) {
            if (client->req.method.empty()) {
                client->req.method.assign(b, e);
            } else if (client->req.target.empty()) {
                client->req.target.assign(b, e);
            } else if (client->req.version.empty()) {
                client->req.version.assign(b, e);
            }
        });

        size_t q_pos = client->req.target.find('?');
        if (q_pos != std::string::npos) {
            client->req.path = client->req.target.substr(0, q_pos);
        } else {
            client->req.path = client->req.target;
        }
        client->state = ClientConnection::HEADERS;
    }

    // 2. 解析头部
    if (client->state == ClientConnection::HEADERS) {
        while(true) {
            size_t pos = client->buffer.find("\r\n");
            if (pos == std::string::npos) break;

            if (pos == 0) { // 空行表示头部结束
                client->buffer.erase(0, 2);
                if (client->req.has_header("Content-Length")) {
                    client->content_length = std::stoull(client->req.get_header_value("Content-Length"));
                    client->state = (client->content_length > 0) ? ClientConnection::BODY : ClientConnection::COMPLETE;
                } else {
                    client->state = ClientConnection::COMPLETE;
                }
                goto body_check; // 检查是否已缓冲了 body
            }

            std::string line = client->buffer.substr(0, pos);
            client->buffer.erase(0, pos + 2);
            httplib::detail::parse_header(line.data(), line.data() + line.size(), [&](const std::string& key, const std::string& val) {
                client->req.headers.emplace(key, val);
            });
        }
    }

body_check:
    // 3. 解析请求体
    if (client->state == ClientConnection::BODY) {
        if (client->buffer.size() >= client->content_length) {
            client->req.body = client->buffer.substr(0, client->content_length);
            client->buffer.erase(0, client->content_length);
            client->state = ClientConnection::COMPLETE;
        }
    }

    // 4. 请求完成，发送响应
    if (client->state == ClientConnection::COMPLETE) {
        uv_read_stop((uv_stream_t*)&client->handle);

        httplib::Response res;
        if (client->req.path == "/status" && client->req.method == "GET") {
            res.status = 200;
            res.set_content("OK", "text/plain");
        } else if (client->req.path == "/echo" && client->req.method == "POST") {
            res.status = 200;
            res.set_content(client->req.body, "text/plain");
        } else {
            res.status = 404;
            res.set_content("Not Found", "text/plain");
        }

        httplib::detail::BufferStream strm;
        res.set_header("Connection", "close");
        res.set_header("Content-Length", std::to_string(res.body.size()));

        httplib::detail::write_response_line(strm, res.status);
        httplib::detail::write_headers(strm, res.headers);
        if (!res.body.empty()) {
            strm.write(res.body.c_str(), res.body.size());
        }

        const auto& response_str = strm.get_buffer();

        write_req_t* wr = (write_req_t*)malloc(sizeof(write_req_t));
        wr->client = client;
        char* response_buf = (char*)malloc(response_str.size());
        memcpy(response_buf, response_str.c_str(), response_str.size());
        wr->buf = uv_buf_init(response_buf, response_str.size());

        uv_write(&wr->req, (uv_stream_t*)&client->handle, &wr->buf, 1, on_write_end);
    }
}

// 新连接回调
void on_new_connection(uv_stream_t* server, int status) {
    if (status < 0) {
        fprintf(stderr, "New connection error %s\n", uv_strerror(status));
        return;
    }
    auto* client = new ClientConnection(server->loop);
    if (uv_accept(server, (uv_stream_t*)&client->handle) == 0) {
        uv_read_start((uv_stream_t*)&client->handle, alloc_buffer, on_read);
    } else {
        uv_close((uv_handle_t*)&client->handle, on_close);
    }
}

int main() {
    uv_loop_t* loop = uv_default_loop();
    uv_tcp_t server;
    uv_tcp_init(loop, &server);
    struct sockaddr_in addr;
    uv_ip4_addr("0.0.0.0", 8080, &addr);
    uv_tcp_bind(&server, (const struct sockaddr*)&addr, 0);
    int r = uv_listen((uv_stream_t*)&server, 128, on_new_connection);
    if (r) {
        fprintf(stderr, "Listen error %s\n", uv_strerror(r));
        return 1;
    }
    std::cout << "Server listening on port 8080" << std::endl;
    return uv_run(loop, UV_RUN_DEFAULT);
}
