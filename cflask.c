#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "http-parser.h"


/* 
* Implement all the cflask logic here.
* 1. web server initialization 
* 2. accept http requests
* 3. parse http requests
* 4. dispatch http requests
* 5. process, generate and send http responses
*/


int main() {
    // Example raw HTTP POST request string
    char raw_request[] = 
        "POST /submit-form?name=Buzz HTTP/1.1\r\n"
        "Host: localhost:8080\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: 18\r\n"
        "\r\n"
        "{\"username\":\"dev\"}";

    ParsedRequest req;
    parse_http_request(raw_request, &req);

    // Print parsed output to verify
    printf("--- Request Line ---\n");
    printf("Method:  %s\n", req.request_line.method);
    printf("URI:     %s\n", req.request_line.uri);
    printf("Version: %s\n\n", req.request_line.version);

    printf("--- Headers parsed: %d ---\n", req.header_count);
    for (int i = 0; i < req.header_count; i++) {
        printf("%s -> %s\n", req.headers[i].key, req.headers[i].value);
    }

    if (req.body) {
        printf("\n--- Body ---\n%s\n\n", req.body);
    }

    
    const char *body = "{\"status\":\"success\",\"message\":\"Hello from C server!\"}";
    size_t len = strlen(body);
    size_t response_size = 0;

    char *http_response = create_http_response("200 OK", "text/html", body, len, &response_size);

    if (http_response) {
        printf("--- Generated HTTP Response (%zu bytes) ---\n", response_size);
        printf("%s\n", http_response);
        
        // Clean up allocated memory
        free(http_response);
    }

    return 0;
}

