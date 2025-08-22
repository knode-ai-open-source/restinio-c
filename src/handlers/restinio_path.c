// SPDX-FileCopyrightText: 2025 Andy Curtis <contactandyc@gmail.com>
// SPDX-FileCopyrightText: 2024–2025 Knode.ai — technical questions: contact Andy (above)
// SPDX-License-Identifier: Apache-2.0

#include "restinio-c/handlers/restinio_path.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>

// Helper function declarations
static char *load_file_into_memory(const char *filepath, size_t *out_size);
static const char *guess_mime_type(const char *filename);
static restinio_response_t *make_response(
    const char *body, const char *content_type, int status_code, const char *status_message);
static void destroy_response(restinio_response_t *response);

// Structure for handling path mappings
struct restinio_path_handler_s {
    const char *uri_path;   // URL path to match (e.g., "/swagger.json" or "/docs")
    const char *source_path; // Local file or directory path
    bool is_directory;      // If true, source_path is a directory
};

// Request handler function
restinio_response_t *restinio_path_handler_cb(
    void *arg,
    const char *method,
    const char *uri,
    const char *body,
    size_t body_length)
{
    restinio_path_handler_t *handler = (restinio_path_handler_t *)arg;

    // Ensure it's a GET request
    if (strcasecmp(method, "GET") != 0) {
        return make_response("Method Not Allowed", "text/plain", 405, "Method Not Allowed");
    }

    // Handle single file requests
    if (!handler->is_directory) {
        if (strcmp(uri, handler->uri_path) == 0) {
            size_t file_size = 0;
            char *file_contents = load_file_into_memory(handler->source_path, &file_size);
            if (!file_contents) {
                return make_response("File not found", "text/plain", 404, "Not Found");
            }

            const char *mime_type = guess_mime_type(handler->source_path);
            restinio_response_t *resp = make_response(file_contents, mime_type, 200, "OK");
            free(file_contents);
            resp->destroy = destroy_response;
            return resp;
        }
    }
    // Handle directory-based file requests
    else {
        if (strncmp(uri, handler->uri_path, strlen(handler->uri_path)) == 0) {
            const char *subpath = uri + strlen(handler->uri_path);
            if (*subpath == '\0') {
                subpath = "/index.html";  // Default to index.html
            }

            char filepath[512];
            snprintf(filepath, sizeof(filepath), "%s%s", handler->source_path, subpath);

            size_t file_size = 0;
            char *file_contents = load_file_into_memory(filepath, &file_size);
            if (!file_contents) {
                return make_response("File not found", "text/plain", 404, "Not Found");
            }

            const char *mime_type = guess_mime_type(filepath);
            restinio_response_t *resp = make_response(file_contents, mime_type, 200, "OK");
            free(file_contents);
            resp->destroy = destroy_response;
            return resp;
        }
    }

    return NULL;
}

// Factory function to create path handlers
restinio_path_handler_t *restinio_path_handler(
    const char *source_path,
    const char *uri_path,
    bool directory)
{
    restinio_path_handler_t *handler = (restinio_path_handler_t *)malloc(sizeof(restinio_path_handler_t));
    if (!handler) return NULL;

    handler->uri_path = strdup(uri_path);
    handler->source_path = strdup(source_path);
    handler->is_directory = directory;
    return handler;
}

//-----------------------------------------------------
// Helper: create a restinio_response_t
//-----------------------------------------------------
static restinio_response_t *make_response(
    const char *body, const char *content_type, int status_code, const char *status_message)
{
    restinio_response_t *resp = (restinio_response_t *)calloc(1, sizeof(*resp));
    if (!resp) {
        fprintf(stderr, "Out of memory creating response\n");
        return NULL;
    }

    // Copy the body
    if (body) {
        resp->response = strdup(body);
        resp->response_length = strlen(body);
    } else {
        resp->response = NULL;
        resp->response_length = 0;
    }

    // Set the status code and error message
    if (status_code >= 200 && status_code < 300) {
        resp->error_code = 0;
    } else {
        resp->error_code = status_code;
        resp->error_message = strdup(status_message ? status_message : "");
    }

    // Add a Content-Type header
    restinio_header_t *header = (restinio_header_t *)calloc(1, sizeof(restinio_header_t));
    if (header) {
        header->key = strdup("Content-Type");
        header->value = strdup(content_type ? content_type : "text/plain");
        header->next = NULL;
        resp->headers = header;
    } else {
        fprintf(stderr, "Failed to allocate header\n");
    }

    return resp;
}

//-----------------------------------------------------
// Helper: load a file from disk into a char*
//-----------------------------------------------------
static char *load_file_into_memory(const char *filepath, size_t *out_size)
{
    *out_size = 0;
    FILE *f = fopen(filepath, "rb");
    if (!f) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) {
        fclose(f);
        return NULL;
    }
    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t read_n = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[read_n] = '\0';
    *out_size = read_n;
    return buf;
}

//-----------------------------------------------------
// Helper: guess MIME type from file extension
//-----------------------------------------------------
static const char *guess_mime_type(const char *filename)
{
    size_t len = strlen(filename);
    if (len >= 5 && strcasecmp(filename + len - 5, ".html") == 0) {
        return "text/html";
    }
    if (len >= 4 && strcasecmp(filename + len - 4, ".css") == 0) {
        return "text/css";
    }
    if (len >= 3 && strcasecmp(filename + len - 3, ".js") == 0) {
        return "application/javascript";
    }
    if (len >= 4 && strcasecmp(filename + len - 4, ".png") == 0) {
        return "image/png";
    }
    if (len >= 4 && strcasecmp(filename + len - 4, ".json") == 0) {
        return "application/json";
    }
    return "text/plain";
}

//-----------------------------------------------------
// Helper: clean up a response
//-----------------------------------------------------
static void destroy_response(restinio_response_t *response)
{
    if (!response) return;

    if (response->response) free(response->response);
    if (response->error_message) free(response->error_message);

    restinio_header_t *header = response->headers;
    while (header) {
        restinio_header_t *next = header->next;
        free(header->key);
        free(header->value);
        free(header);
        header = next;
    }

    free(response);
}
