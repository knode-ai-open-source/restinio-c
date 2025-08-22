// SPDX-FileCopyrightText: 2025 Andy Curtis <contactandyc@gmail.com>
// SPDX-FileCopyrightText: 2024–2025 Knode.ai — technical questions: contact Andy (above)
// SPDX-License-Identifier: Apache-2.0

#include "restinio-c/restinio_c.h"
#include <restinio/all.hpp>  // for restinio::run, on_thread_pool, create_response, etc.
#include <thread>
#include <memory>
#include <atomic>
#include <iostream>
#include <cstring>
#include <string>
#include <mutex>
#include <map>
#include <unordered_map>


// Anonymous namespace
namespace {

typedef struct restinio_path_handler_s {
    char *method;
    char *path;
    restinio_handle_detached_request_cb detached_cb;
    restinio_handle_request_cb cb;
    void *arg;

    struct restinio_path_handler_s *next;
} restinio_path_handler_t;

restinio_path_handler_t *g_path_map = NULL, *g_path_map_tail = NULL;

static std::atomic_bool g_stop_flag{false};

struct ServerInstance {
    std::unique_ptr<std::thread> server_thread;
};

static std::unique_ptr<ServerInstance> g_server;
static restinio_options_t g_options;

/**
 * 1) Instead of a single signature that takes a `response_builder_t<default_traits_t> &`,
 *    we make apply_headers_from_user a template, so it can accept *any* of the modern
 *    `response_builder_t<Traits>` types that Restinio might produce.
 *
 * 2) We replace `rb.append_header(...)` with `rb.header().set_field(...)`, which is
 *    the typical modern approach in new versions of Restinio.
 */
template<typename Builder>
void apply_headers_from_user(Builder &rb, restinio_header_t *hdr_list)
{
    for (auto *hdr = hdr_list; hdr != nullptr; hdr = hdr->next) {
        if (hdr->key && hdr->value) {
            // Old code: rb.append_header(hdr->key, hdr->value);
            // Modern code:
            rb.header().set_field(hdr->key, hdr->value);
        }
    }
}

/**
 * Creates the request handler with a modern approach.
 * Removes references to restinio::own_string_t, which no longer exist.
 */
auto make_request_handler() {
    return [](auto req) mutable {
        auto method_str = req->header().method();
        auto uri_str    = req->header().request_target();
        std::string body(req->body());
        restinio_response_t *user_resp = nullptr;

        // Handle request dictionary
        restinio_path_handler_t *handler = g_path_map;
        while(handler) {
            // a zero length method matches all methods
            // a path matches if uri_str starts with path
            if((!handler->method[0] || !strcmp(method_str.c_str(), handler->method)) &&
               (!handler->path[0] || !strncmp(uri_str.c_str(), handler->path, strlen(handler->path)))) {
                if(handler->detached_cb) {
                    // Pass the request
                    handler->detached_cb(
                        handler->arg,
                        method_str.c_str(),
                        uri_str.c_str(),
                        body.data(),
                        body.size(),
                        static_cast<void*>(new std::shared_ptr<restinio::request_t>(req))
                    );
                    return restinio::request_accepted(); // Indicate detached handling
                } else {
                    user_resp = handler->cb(
                        handler->arg,
                        method_str.c_str(),
                        uri_str.c_str(),
                        body.data(),
                        body.size()
                    );
                    if(user_resp)
                        break;
                }
            }
            handler = handler->next;
        }
        if(handler && user_resp) {
            if (user_resp->error_code != 0) {
                // For an error
                auto rb = req->create_response(restinio::status_internal_server_error());
                rb.set_body(
                    user_resp->error_message
                    ? user_resp->error_message
                    : "Error occurred, but no message provided");
                apply_headers_from_user(rb, user_resp->headers);
                return rb.done();
            }
            else {
                auto rb = req->create_response(restinio::status_ok());
                apply_headers_from_user(rb, user_resp->headers);

                // Just set a std::string body
                rb.set_body(user_resp->response ? user_resp->response : "");

                if (user_resp->destroy) {
                    restinio_destroy_cb destroy_cb = user_resp->destroy;
                    destroy_cb(user_resp);
                }
                return rb.done();
            }
        }
        else {
            auto rb = req->create_response(restinio::status_not_implemented())
                .set_body("No callback set or callback returned null");
            return rb.done();
        }
    };
}

static void run_server_loop(const restinio_options_t &options) {
    using namespace restinio;

    // 1) Build the same server_settings as run_server_loop does:
    auto settings = server_settings_t<default_traits_t>{}
        .port(options.port)
        .address(options.address ? options.address : "0.0.0.0")
        .request_handler(make_request_handler())
        // Keepalive-like settings:
        .read_next_http_message_timelimit(
            options.enable_keepalive ? std::chrono::seconds(15)
                                     : std::chrono::seconds(60))
        .write_http_response_timelimit(std::chrono::seconds(120))
        .handle_request_timeout(std::chrono::seconds(120));

    // 2) Decide on thread-pool size in the same way:
    std::size_t pool_size = options.enable_thread_pool
        ? options.thread_pool_size
        : 1;

    // 3) Launch the server asynchronously:
    auto server_handle = run_async<default_traits_t>(
        own_io_context(), // The server uses its own Asio io_context
        std::move(settings),
        pool_size
    );

    // Wait until Ctrl+C is pressed (signal sets g_stop_flag to true)
    while(!g_stop_flag.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Now gracefully stop and wait
    server_handle->stop();
    server_handle->wait();
}

} // anonymous namespace

#ifdef __cplusplus
extern "C" {
#endif

void restinio_finish_detached(
    void *response_handle,
    int status_code,
    const char *response_body,
    size_t response_body_length,
    restinio_header_t *headers) {

    // Cast the void* back to std::shared_ptr<request_t>
    auto req_ptr = static_cast<std::shared_ptr<restinio::request_t>*>(response_handle);
    if (!req_ptr || !(*req_ptr)) {
        std::cerr << "Invalid response handle!" << std::endl;
        return;
    }

    auto req = *req_ptr;

    // Build and finalize the response
    auto rb = req->create_response();
    rb.header().status_code(restinio::http_status_code_t{static_cast<uint16_t>(status_code)});
    rb.set_body(std::string(response_body, response_body_length));

    // Apply headers
    if (headers) {
        for (auto *hdr = headers; hdr != nullptr; hdr = hdr->next) {
            if (hdr->key && hdr->value) {
                rb.header().set_field(hdr->key, hdr->value);
            }
        }
    }

    rb.done();

    // Clean up the request pointer
    delete req_ptr;
}

void restinio_finish_detached_error(
    void *response_handle,
    int status_code,
    const char *response) {
    restinio_header_t headers = {(char *)"Content-Type", (char *)"text/plain", NULL};
    restinio_finish_detached(
        response_handle,
        status_code,
        response,
        response ? strlen(response) : 0,
        &headers
    );
}

void restinio_finish_detached_text(
    void *response_handle,
    const char *response) {
    restinio_finish_detached_error(
        response_handle,
        200,
        response
    );
}

void restinio_finish_detached_json(
    void *response_handle,
    const char *response_body,
    size_t response_body_length) {
    restinio_header_t headers = {(char *)"Content-Type", (char *)"application/json", NULL};
    restinio_finish_detached(
        response_handle,
        200,
        response_body,
        response_body_length,
        &headers
    );
}

static void _restinio_use(const char *method,
                          const char *path,
                          restinio_handle_request_cb cb,
                          restinio_handle_detached_request_cb detached_cb,
                          void *arg) {
    size_t path_length = path ? strlen(path) : 0;
    size_t method_length = method ? strlen(method) : 0;

    restinio_path_handler_t *handler =
        (restinio_path_handler_t *)calloc(1, sizeof(*handler) + method_length + path_length + 2);
    handler->method = (char *)(handler + 1);
    if(method_length) {
        strcpy(handler->method, method);
        // uppercase handler->method
        for (char *c = handler->method; *c; c++) {
            *c = toupper(*c);
        }
    } else
        handler->method[0] = 0;
    handler->path = handler->method + method_length + 1;
    if(path_length)
        strcpy(handler->path, path);
    else
        handler->path[0] = 0;

    if(!g_path_map)
        g_path_map = g_path_map_tail = handler;
    else {
        g_path_map_tail->next = handler;
        g_path_map_tail = handler;
    }
    handler->detached_cb = detached_cb;
    handler->cb = cb;
    handler->arg = arg;
}

void restinio_use(const char *method,
                  const char *path,
                  restinio_handle_request_cb cb,
                  void *arg) {
    _restinio_use(method, path, cb, NULL, arg);
}

void restinio_use_detached(const char *method,
                           const char *path,
                           restinio_handle_detached_request_cb cb,
                           void *arg) {
    _restinio_use(method, path, NULL, cb, arg);
}

void restinio_init(restinio_options_t *options)
{
    if (g_server) {
        std::cerr << "restinio_init called twice?\n";
        return;
    }

    if (!options) {
        std::cerr << "Invalid options provided to restinio_init.\n";
        return;
    }

    g_options = *options;
    g_server = std::make_unique<ServerInstance>();
}

void restinio_run() {
    // Pass `g_options` by value to avoid capturing a global variable by reference
    g_server->server_thread = std::make_unique<std::thread>([options = g_options]() {
        run_server_loop(options);
    });
}


void restinio_destroy()
{
    if (!g_server) {
        std::cerr << "restinio_destroy called but not initted?\n";
        return;
    }
    g_stop_flag.store(true);

    // Wait for server thread to finish
    if (g_server->server_thread && g_server->server_thread->joinable()) {
        g_server->server_thread->join();
    }

    restinio_path_handler_t *handler = g_path_map;
    while(handler) {
        restinio_path_handler_t *next = handler->next;
        free(handler);
        handler = next;
    }
    g_path_map = g_path_map_tail = NULL;

    // Clean up resources
    g_server.reset();
}

#ifdef __cplusplus
} // extern "C"
#endif
