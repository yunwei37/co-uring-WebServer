#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <coroutine>
#include <iostream>
#include <stdexcept>
#include <map>

#include "liburing.h"

#define MAX_CONNECTIONS     4096
#define BACKLOG             512
#define MAX_MESSAGE_LEN     2048
#define BUFFERS_COUNT       MAX_CONNECTIONS

void add_accept(struct io_uring *ring, int fd, struct sockaddr *client_addr, socklen_t *client_len, unsigned flags);
void add_socket_read(struct io_uring *ring, int fd, unsigned gid, size_t size, unsigned flags);
void add_socket_write(struct io_uring *ring, int fd, __u16 bid, size_t size, unsigned flags);
void add_provide_buf(struct io_uring *ring, __u16 bid, unsigned gid);

enum {
    ACCEPT,
    READ,
    WRITE,
    PROV_BUF,
};

struct conn_info {
    __u32 fd;
    __u16 type;
    __u16 bid;
};

char bufs[BUFFERS_COUNT][MAX_MESSAGE_LEN] = {0};
constexpr int group_id = 1337;

struct conn_task {
    struct promise_type
    {
        using Handle = std::coroutine_handle<promise_type>;
        conn_task get_return_object()
        {
            return conn_task{Handle::from_promise(*this)};
        }
        std::suspend_always initial_suspend() noexcept { 
            return {}; 
        }
        std::suspend_never final_suspend() noexcept { return {}; }
        void return_void() noexcept {}
        void unhandled_exception() noexcept {}
        struct io_uring *ring;
        struct conn_info conn_info;  
        size_t res;
    };
    explicit conn_task(promise_type::Handle handler) : handler(handler) {}
    void destroy() { handler.destroy(); }
    conn_task(const conn_task &) = delete;
    conn_task &operator=(const conn_task &) = delete;
    conn_task(conn_task &&t) noexcept : handler(t.handler) { t.handler = {}; }
    conn_task &operator=(conn_task &&t) noexcept
    {
        if (this == &t)
            return *this;
        if (handler)
            handler.destroy();
        handler = t.handler;
        t.handler = {};
        return *this;
    }
    promise_type::Handle handler;
};

auto echo_read(size_t message_size, unsigned flags) {
  struct awaitable {
    bool await_ready() { return false; }
    void await_suspend(std::coroutine_handle<conn_task::promise_type> h) {
        auto &p = h.promise();
        struct io_uring_sqe *sqe = io_uring_get_sqe(p.ring);
        io_uring_prep_recv(sqe, p.conn_info.fd, NULL, message_size, 0);
        io_uring_sqe_set_flags(sqe, flags);
        sqe->buf_group = group_id;
        p.conn_info.type = READ;
        memcpy(&sqe->user_data, &p.conn_info, sizeof(conn_info));
        this->p = &p;
    }
    size_t await_resume() {
        return p->res;
    }
    size_t message_size;
    unsigned flags;
    conn_task::promise_type* p = NULL;
  };
  return awaitable{message_size, flags};
}

auto echo_write(size_t message_size, unsigned flags) {
  struct awaitable {
    bool await_ready() { return false; }
    void await_suspend(std::coroutine_handle<conn_task::promise_type> h) {
        auto &p = h.promise();
        struct io_uring_sqe *sqe = io_uring_get_sqe(p.ring);
        io_uring_prep_send(sqe, p.conn_info.fd, &bufs[p.conn_info.bid], message_size, 0);
        io_uring_sqe_set_flags(sqe, flags);
        p.conn_info.type = WRITE;
        memcpy(&sqe->user_data, &p.conn_info, sizeof(conn_info));
    }
    size_t await_resume() {
        return 0;
    }
    size_t message_size;
    unsigned flags;
  };
  return awaitable{message_size, flags};
}

auto echo_add_buffer() {
  struct awaitable {
    bool await_ready() { return false; }
    void await_suspend(std::coroutine_handle<conn_task::promise_type> h) {
        auto &p = h.promise();
        struct io_uring_sqe *sqe = io_uring_get_sqe(p.ring);
        io_uring_prep_provide_buffers(sqe, bufs[p.conn_info.bid], MAX_MESSAGE_LEN, 1, group_id, p.conn_info.bid);
        p.conn_info.type = PROV_BUF;
        memcpy(&sqe->user_data, &p.conn_info, sizeof(conn_info));
        h.resume();
    }
    size_t await_resume() {
        return 0;
    }
  };
  return awaitable{};
}

std::map<int, conn_task> connections;

conn_task handle_echo(int fd) {
    while (true) {
        size_t size_r = co_await echo_read(MAX_MESSAGE_LEN, IOSQE_BUFFER_SELECT);
        if (size_r <= 0) {
            co_await echo_add_buffer();
            shutdown(fd, SHUT_RDWR);
            connections.erase(fd);
            co_return;
        }
        co_await echo_write(size_r, 0);
        co_await echo_add_buffer();
    }

}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Please give a port number: ./io_uring_echo_server [port]\n");
        exit(0);
    }

    // some variables we need
    int portno = strtol(argv[1], NULL, 10);
    struct sockaddr_in serv_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);

    // setup socket
    int sock_listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    const int val = 1;
    setsockopt(sock_listen_fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(portno);
    serv_addr.sin_addr.s_addr = INADDR_ANY;

    // bind and listen
    if (bind(sock_listen_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Error binding socket...\n");
        exit(1);
    }
    if (listen(sock_listen_fd, BACKLOG) < 0) {
        perror("Error listening on socket...\n");
        exit(1);
    }
    printf("io_uring echo server listening for connections on port: %d\n", portno);

    // initialize io_uring
    struct io_uring_params params;
    struct io_uring ring;
    memset(&params, 0, sizeof(params));

    if (io_uring_queue_init_params(2048, &ring, &params) < 0) {
        perror("io_uring_init_failed...\n");
        exit(1);
    }

    // check if IORING_FEAT_FAST_POLL is supported
    if (!(params.features & IORING_FEAT_FAST_POLL)) {
        printf("IORING_FEAT_FAST_POLL not available in the kernel, quiting...\n");
        exit(0);
    }

    // check if buffer selection is supported
    struct io_uring_probe *probe;
    probe = io_uring_get_probe_ring(&ring);
    if (!probe || !io_uring_opcode_supported(probe, IORING_OP_PROVIDE_BUFFERS)) {
        printf("Buffer select not supported, skipping...\n");
        exit(0);
    }
    free(probe);

    // register buffers for buffer selection
    struct io_uring_sqe *sqe;
    struct io_uring_cqe *cqe;

    sqe = io_uring_get_sqe(&ring);
    io_uring_prep_provide_buffers(sqe, bufs, MAX_MESSAGE_LEN, BUFFERS_COUNT, group_id, 0);

    io_uring_submit(&ring);
    io_uring_wait_cqe(&ring, &cqe);
    if (cqe->res < 0) {
        printf("cqe->res = %d\n", cqe->res);
        exit(1);
    }
    io_uring_cqe_seen(&ring, cqe);

    // add first accept SQE to monitor for new incoming connections
    add_accept(&ring, sock_listen_fd, (struct sockaddr *)&client_addr, &client_len, 0);

    // start event loop
    while (1) {
        io_uring_submit_and_wait(&ring, 1);
        struct io_uring_cqe *cqe;
        unsigned head;
        unsigned count = 0;

        // go through all CQEs
        io_uring_for_each_cqe(&ring, head, cqe) {
            ++count;
            struct conn_info conn_i;
            memcpy(&conn_i, &cqe->user_data, sizeof(conn_i));

            int type = conn_i.type;
            if (cqe->res == -ENOBUFS) {
                fprintf(stdout, "bufs in automatic buffer selection empty, this should not happen...\n");
                fflush(stdout);
                exit(1);
            } else if (type == PROV_BUF) {
                if (cqe->res < 0) {
                    printf("cqe->res = %d\n", cqe->res);
                    exit(1);
                }
            } else if (type == ACCEPT) {
                int sock_conn_fd = cqe->res;
                // only read when there is no error, >= 0
                if (sock_conn_fd >= 0) {
                    connections.emplace(sock_conn_fd, handle_echo(sock_conn_fd));
                    auto &h = connections.at(sock_conn_fd).handler;
                    auto &p = h.promise();
                    p.conn_info.fd = sock_conn_fd;
                    p.ring = &ring;
                    h.resume();
                }

                // new connected client; read data from socket and re-add accept to monitor for new connections
                add_accept(&ring, sock_listen_fd, (struct sockaddr *)&client_addr, &client_len, 0);
            } else if (type == READ) {
                auto &h = connections.at(conn_i.fd).handler;
                auto &p = h.promise();
                p.conn_info.bid = cqe->flags >> 16;
                p.res = cqe->res;
                h.resume();
            } else if (type == WRITE) {
                auto &h = connections.at(conn_i.fd).handler;
                h.resume();
            }
        }

        io_uring_cq_advance(&ring, count);
    }
}

void add_accept(struct io_uring *ring, int fd, struct sockaddr *client_addr, socklen_t *client_len, unsigned flags) {
    struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
    io_uring_prep_accept(sqe, fd, client_addr, client_len, 0);
    io_uring_sqe_set_flags(sqe, flags);

    conn_info conn_i = {
        .fd = (unsigned int)fd,
        .type = ACCEPT,
        .bid = 0
    };
    memcpy(&sqe->user_data, &conn_i, sizeof(conn_i));
}
