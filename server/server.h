#ifndef SERVER_H
#define SERVER_H

#include <memory>
#include "io_uring.h"
#include "utils.h"
#include <netinet/in.h>
#include <signal.h>
#include <map>
#include "task.h"
#include "stream.h"

constexpr size_t ENTRIES = 2048;

class server
{
public:
    server(int port);
    ~server();
    void start();
private:
    std::unique_ptr<io_uring_handler> uring;
    int sock_fd;

    void setup_listening_socket(int port);
};

void server::setup_listening_socket(int port)
{
    struct sockaddr_in srv_addr = {0};

    sock_fd = socket(PF_INET, SOCK_STREAM, 0);
    if (sock_fd == -1)
        fatal_error("socket()");

    int enable = 1;
    if (setsockopt(sock_fd,
                   SOL_SOCKET, SO_REUSEADDR,
                   &enable, sizeof(int)) < 0)
        fatal_error("setsockopt(SO_REUSEADDR)");

    memset(&srv_addr, 0, sizeof(srv_addr));
    srv_addr.sin_family = AF_INET;
    srv_addr.sin_port = htons(port);
    srv_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    /* We bind to a port and turn this socket into a listening
     * socket.
     * */
    if (bind(sock_fd,
             (const struct sockaddr *)&srv_addr,
             sizeof(srv_addr)) < 0)
        fatal_error("bind()");

    if (listen(sock_fd, 10) < 0)
        fatal_error("listen()");
}

server::server(int port)
{
    setup_listening_socket(port);
    uring.reset(new io_uring_handler(ENTRIES, sock_fd));
}

server::~server()
{
}

task handle_http_request(int fd) {
    char* read_buffer;
    size_t read_bytes = co_await read_socket(&read_buffer);
    co_await write_socket(read_bytes);
    co_return;
}


void server::start() {
    uring->event_loop(handle_http_request);
}

#endif
