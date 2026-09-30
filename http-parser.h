#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifndef HTTP_PARSER_H
#define HTTP_PRASER_H


typedef struct {
    char method[16];
    char uri[256];
    char version[16];
} HttpRequestLine;

typedef struct {
    char key[64];
    char value[256];
} HttpHeader;

typedef struct {
    HttpRequestLine request_line;
    HttpHeader headers[32];
    int header_count;
    char *body;
} ParsedRequest;

void parse_http_request(char *raw_request, ParsedRequest *req);

/**
 * Creates a dynamically allocated HTTP response string.
 * 
 * @param status_code   e.g., "200 OK" or "404 Not Found"
 * @param content_type  e.g., "text/html", "application/json", "image/jpeg"
 * @param data          Pointer to the body payload
 * @param data_len      Size of the payload in bytes
 * @param out_resp_len  Pointer to return the total size of the final response block
 * @return              Pointer to the allocated response block (caller must free it)
 */

char* create_http_response(const char *status_code, const char *content_type, const char *data, size_t data_len, size_t *out_resp_len);

ParsedRequest req = {0};

#endif