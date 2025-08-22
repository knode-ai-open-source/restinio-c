// SPDX-FileCopyrightText: 2025 Andy Curtis <contactandyc@gmail.com>
// SPDX-FileCopyrightText: 2024–2025 Knode.ai — technical questions: contact Andy (above)
// SPDX-License-Identifier: Apache-2.0

#ifndef _restinio_c_H
#define _restinio_c_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct restinio_header_s {
    char *key;
    char *value;
    struct restinio_header_s *next;
} restinio_header_t;

struct restinio_response_s;
typedef struct restinio_response_s restinio_response_t;

// if this returns NULL, it is skipped
typedef restinio_response_t *(*restinio_handle_request_cb)(
    void *arg,
    const char *method,
    const char *uri,
    const char *body,
    size_t body_length
);

// Detached callback for asynchronous handling
typedef void (*restinio_handle_detached_request_cb)(
    void *arg,
    const char *method,
    const char *uri,
    const char *body,
    size_t body_length,
    void *response_handle /* Handle for finalizing response */
);

typedef void (*restinio_destroy_cb)(restinio_response_t *r);

struct restinio_response_s {
    char *response;
    size_t response_length;

    int error_code;     // zero if success
    char *error_message;  // null if no error
    restinio_header_t *headers;

    restinio_destroy_cb destroy;
};

typedef struct {
    bool enable_ssl;        // if off, the following are ignored
    const char *cert_file;
    const char *key_file;

    bool enable_http2;       // if the client supports it (allow http/2 upgrade)
    bool enable_keepalive;   // if the client supports it
    bool enable_thread_pool; // as opposed to running on a single thread
    int thread_pool_size;    // number of worker threads if enable_thread_pool
    unsigned short port;     // port number to bind the server
    const char *address;     // address to bind the server (e.g., "0.0.0.0")
} restinio_options_t;

void restinio_init(
    restinio_options_t *options
);

void restinio_use(const char *method,
                  const char *path,
                  restinio_handle_request_cb cb,
                  void *arg);

void restinio_use_detached(const char *method,
                           const char *path,
                           restinio_handle_detached_request_cb cb,
                           void *arg);
void restinio_run();

void restinio_destroy();

void restinio_finish_detached(
    void *response_handle,
    int status_code,
    const char *response_body,
    size_t response_body_length,
    restinio_header_t *headers);

void restinio_finish_detached_error(
    void *response_handle,
    int status_code,
    const char *response);

void restinio_finish_detached_text(
    void *response_handle,
    const char *response);

void restinio_finish_detached_json(
    void *response_handle,
    const char *response_body,
    size_t response_body_length);

#ifdef __cplusplus
}
#endif

#endif
