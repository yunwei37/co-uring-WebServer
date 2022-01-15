#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H

#include <coroutine>
#include <string>
#include <unistd.h>
#include <fcntl.h>
#include <cstdlib>
#include <cstdio>
#include "stream.h"
#include <sys/stat.h>

constexpr int FILENAME_LEN = 200;
constexpr int READ_BUFFER_SIZE = 2048;
constexpr int WRITE_BUFFER_SIZE = 1024;

class http_conn
{
public:
    enum METHOD
    {
        GET = 0,
        POST,
        HEAD,
        PUT,
        DELETE,
        TRACE,
        OPTIONS,
        CONNECT,
        PATH,
        UNIMPLEMENT_MOTHOD
    };
    enum HTTP_CODE
    {
        NO_REQUEST,
        GET_REQUEST,
        BAD_REQUEST,
        NO_FOUND,
        FORBIDDEN_REQUEST,
        FILE_REQUEST,
        INTERNAL_ERROR,
        CLOSED_CONNECTION,
    };

public:
    http_conn() {}
    ~http_conn() {}
    HTTP_CODE handle_request(char *text_buffer);
    size_t get_response_size();

private:
    HTTP_CODE parse_request(char *text);
    bool process_write(HTTP_CODE ret);
    HTTP_CODE do_request();
    void add_content();
    void add_headers();
    void add_content_type();
    void add_content_length();
    bool add_linger();
    bool add_blank_line();

private:
    int fd;
    METHOD method;
    size_t response_size;
    char final_path[256];
    char* response_pointer;
    size_t content_size;
    static const char *unimplemented_content;
    static const char *http_404_content;
    static const char *ok_header;
};

const char *http_conn::unimplemented_content = \
        "HTTP/1.0 400 Bad Request\r\n"
        "Content-type: text/html\r\n"
        "\r\n"
        "<html>"
        "<head>"
        "<title>ZeroHTTPd: Unimplemented</title>"
        "</head>"
        "<body>"
        "<h1>Bad Request (Unimplemented)</h1>"
        "<p>Your client sent a request ZeroHTTPd did not understand and it is probably not your fault.</p>"
        "</body>"
        "</html>";

const char *http_conn::http_404_content = \
        "HTTP/1.0 404 Not Found\r\n"
        "Content-type: text/html\r\n"
        "\r\n"
        "<html>"
        "<head>"
        "<title>ZeroHTTPd: Not Found</title>"
        "</head>"
        "<body>"
        "<h1>Not Found (404)</h1>"
        "<p>Your client is asking for an object that was not found on this server.</p>"
        "</body>"
        "</html>";

const char* http_conn::ok_header = "HTTP/1.0 200 OK\r\n"
                                   "Server: zerohttpd/0.1\r\n";

void http_conn::add_content() {
    int fd;

    fd = open(final_path, O_RDONLY);
    if (fd < 0)
        fatal_error("open");

    /* We should really check for short reads here */
    size_t ret = read(fd, response_pointer, 2048);
    response_pointer[ret] = 0;
    close(fd);
}

/*
 * Simple function to get the file extension of the file that we are about to serve.
 * */

const char *get_filename_ext(const char *filename) {
    const char *dot = strrchr(filename, '.');
    if (!dot || dot == filename)
        return "";
    return dot + 1;
}

void http_conn::add_content_type() {
    const char *file_ext = get_filename_ext(final_path);
    if (strcmp("jpg", file_ext) == 0)
        strcpy(response_pointer, "Content-Type: image/jpeg\r\n");
    if (strcmp("jpeg", file_ext) == 0)
        strcpy(response_pointer, "Content-Type: image/jpeg\r\n");
    if (strcmp("png", file_ext) == 0)
        strcpy(response_pointer, "Content-Type: image/png\r\n");
    if (strcmp("gif", file_ext) == 0)
        strcpy(response_pointer, "Content-Type: image/gif\r\n");
    if (strcmp("htm", file_ext) == 0)
        strcpy(response_pointer, "Content-Type: text/html\r\n");
    if (strcmp("html", file_ext) == 0)
        strcpy(response_pointer, "Content-Type: text/html\r\n");
    if (strcmp("js", file_ext) == 0)
        strcpy(response_pointer, "Content-Type: application/javascript\r\n");
    if (strcmp("css", file_ext) == 0)
        strcpy(response_pointer, "Content-Type: text/css\r\n");
    if (strcmp("txt", file_ext) == 0)
        strcpy(response_pointer, "Content-Type: text/plain\r\n");
    response_pointer += strlen(response_pointer);
}

void http_conn::add_content_length() {
    sprintf(response_pointer, "content-length: %ld\r\n", content_size);
    response_pointer += strlen(response_pointer);
}

void http_conn::add_headers() {
    strcpy(response_pointer, ok_header);
    response_pointer += strlen(response_pointer);
    add_content_type();
    add_content_length();
    strcpy(response_pointer, "\r\n");
    response_pointer += strlen(response_pointer);
}

http_conn::HTTP_CODE http_conn::handle_request(char *text_buffer) {
    HTTP_CODE code = parse_request(text_buffer);
    if (code == BAD_REQUEST) {
        strcpy(text_buffer, unimplemented_content);
        response_size = strlen(unimplemented_content);
    } else if (code == NO_FOUND) {
        strcpy(text_buffer, http_404_content);
        response_size = strlen(unimplemented_content);
    } else if (code == GET_REQUEST) {
        response_pointer = text_buffer;
        add_headers();
        add_content();
    }
    return code;
}

int get_first_line(char *src, size_t max_length) {
    size_t length = strlen(src) > max_length? max_length:strlen(src);
    for (size_t i = 0; i < length; i++) {
        if (src[i] == '\r' && src[i+1] == '\n') {
            src[i] = '\0';
            return 1;
        }
    }
    return 0;
}

http_conn::HTTP_CODE http_conn::parse_request(char *text) {
    char *method, *path, *saveptr;
    struct stat path_stat;

    get_first_line(text, 2048);
    method = strtok_r(text, " ", &saveptr);
    strtolower(method);
    path = strtok_r(NULL, " ", &saveptr);

    if (strcmp(method, "get") == 0) {
        this->method = GET;
    } else {
        this->method = UNIMPLEMENT_MOTHOD;
        return BAD_REQUEST;
    }

    if (path[strlen(path) - 1] == '/') {
        strcpy(final_path, "index.html");
    }
    else {
        strcpy(final_path, path);
    }
    if (stat(final_path, &path_stat) == -1) {
        printf("404 Not Found: %s (%s)\n", final_path, path);
        return NO_FOUND;
    }
    if (S_ISREG(path_stat.st_mode)) {
        content_size = path_stat.st_size;
        return GET_REQUEST;
    } else {
        return NO_FOUND;
    }
}

size_t http_conn::get_response_size() {
    return response_size;
}

#endif
