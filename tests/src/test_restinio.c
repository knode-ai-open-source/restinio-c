// SPDX-FileCopyrightText: 2025 Andy Curtis <contactandyc@gmail.com>
// SPDX-FileCopyrightText: 2024–2025 Knode.ai — technical questions: contact Andy (above)
// SPDX-License-Identifier: Apache-2.0

#include "restinio-c/restinio_c.h"
#include "restinio-c/handlers/restinio_path.h"
#include "the-io-library/io.h"  // Include io_find_file_in_parents
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Callback function to free resources after the response is sent
void destroy_response(restinio_response_t *response) {
    if (!response) return;

    // Free the response body
    free(response->response);

    // Free the error message if present
    free(response->error_message);

    // Free headers
    restinio_header_t *header = response->headers;
    while (header) {
        restinio_header_t *next = header->next;
        free(header->key);
        free(header->value);
        free(header);
        header = next;
    }

    // Free the response object itself
    free(response);
}

// Callback function to handle incoming HTTP requests
restinio_response_t *handle_request(void *arg, const char *method, const char *uri, const char *body, size_t body_length) {
    printf("[user] %s %s %.*s\n", method, uri, (int)body_length, body);

    // Allocate and populate the response object
    restinio_response_t *response = (restinio_response_t *)malloc(sizeof(restinio_response_t));
    if (!response) {
        fprintf(stderr, "Failed to allocate memory for response\n");
        return NULL;
    }

    // Set the response body
    const char *response_text = "Hello from Restinio with a destroy function!";
    response->response = strdup(response_text);
    response->response_length = strlen(response_text);

    // Set error code and message (indicating success here)
    response->error_code = 0;
    response->error_message = NULL;

    // Add a Content-Type header
    response->headers = (restinio_header_t *)malloc(sizeof(restinio_header_t));
    if (response->headers) {
        response->headers->key = strdup("Content-Type");
        response->headers->value = strdup("text/plain");
        response->headers->next = NULL;
    } else {
        fprintf(stderr, "Failed to allocate memory for headers\n");
        free(response->response);
        free(response);
        return NULL;
    }

    // Set the destroy function
    response->destroy = destroy_response;

    return response;
}

int main() {
    // Define server options
    restinio_options_t options = {
        .enable_http2 = true,
        .enable_keepalive = true,
        .enable_thread_pool = true,
        .thread_pool_size = 4,
        .port = 8080,
        .address = "0.0.0.0"
    };

    // Initialize the Restinio server
    restinio_init(&options);

    // Find Swagger JSON and Swagger UI directory using io_find_file_in_parents
    char *swagger_json = io_find_file_in_parents("swagger_ui/swagger.json");
    char *swagger_dist = io_find_file_in_parents("swagger_ui/dist");

    if (swagger_json && swagger_dist) {
        // Set up Swagger handlers
        restinio_path_handler_t *swagger_handler = restinio_path_handler(swagger_json, "/swagger.json", false);
        restinio_path_handler_t *docs_handler = restinio_path_handler(swagger_dist, "/docs", true);

        restinio_use("GET", "/swagger.json", restinio_path_handler_cb, swagger_handler);
        restinio_use("GET", "/docs", restinio_path_handler_cb, docs_handler);
    } else {
        fprintf(stderr, "Warning: Could not locate Swagger files.\n");
    }

    // Register a general request handler
    restinio_use("GET", "/", handle_request, NULL);

    printf("Starting Restinio server on http://%s:%d...\n", options.address, options.port);

    // Run the server
    restinio_run();

    // Wait for user to terminate the program
    printf("Press Enter to stop the server...\n");
    getchar();

    // Cleanup
    restinio_destroy();

    if (swagger_json) aml_free(swagger_json);
    if (swagger_dist) aml_free(swagger_dist);

    printf("Server stopped. Exiting...\n");
    return 0;
}
