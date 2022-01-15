#ifndef IO_URING_H
#define IO_URING_H

#include <liburing.h>
#include <memory>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <cstring>
#include <map>
#include <sys/socket.h>
#include <netinet/in.h>
#include "utils.h"
#include "task.h"

constexpr size_t MAX_MESSAGE_LEN = 2048;
constexpr size_t BUFFERS_COUNT = 4096;

class io_uring_handler
{
public:
    io_uring_handler(unsigned entries, int sock_listen_fd);
    void event_loop(task func(int));
    void setup_first_buffer();
    ~io_uring_handler();
    void add_read_request(int fd, request &req);
    void add_write_request(int fd, size_t message_size, request &req);
    void add_accept_request(int fd, struct sockaddr *client_addr, socklen_t *client_len, unsigned flags);
    void add_buffer_request(request &req);
    void add_open_request();
    void add_close_request(int fd);

    char* get_buffer_pointer(int bid) {
        return buffer[bid];
    }

    int get_buffer_id(char* buffer) {
        return (buffer - (char*)this->buffer.get()) / MAX_MESSAGE_LEN;
    }

private:
    struct io_uring ring;
    std::unique_ptr<char[][2048]> buffer;
    std::map<int, task> connections;
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int sock_listen_fd;
};

io_uring_handler::io_uring_handler(unsigned entries, int sock_listen_fd)
{
    struct io_uring_params params;
    memset(&params, 0, sizeof(params));
    this->sock_listen_fd = sock_listen_fd;

    if (io_uring_queue_init_params(entries, &ring, &params) < 0)
    {
        perror("io_uring_init_failed...\n");
        exit(1);
    }

    // check if IORING_FEAT_FAST_POLL is supported
    if (!(params.features & IORING_FEAT_FAST_POLL))
    {
        printf("IORING_FEAT_FAST_POLL not available in the kernel, quiting...\n");
        exit(0);
    }

    struct io_uring_probe *probe;
    probe = io_uring_get_probe_ring(&ring);
    if (!probe || !io_uring_opcode_supported(probe, IORING_OP_PROVIDE_BUFFERS))
    {
        printf("Buffer select not supported, skipping...\n");
        exit(0);
    }
    free(probe);

    setup_first_buffer();
}

void io_uring_handler::event_loop(task handle_event(int))
{
    // start event loop
    log("start event loop");
    add_accept_request(sock_listen_fd, (struct sockaddr *)&client_addr, &client_len, 0);
    while (1)
    {
        io_uring_submit_and_wait(&ring, 1);
        struct io_uring_cqe *cqe;
        unsigned head;
        unsigned count = 0;

        // go through all CQEs
        io_uring_for_each_cqe(&ring, head, cqe)
        {
            ++count;
            request conn_i;
            memcpy(&conn_i, &cqe->user_data, sizeof(conn_i));

            int type = conn_i.event_type;
            if (cqe->res == -ENOBUFS)
            {
                fprintf(stdout, "bufs in automatic buffer selection empty, this should not happen...\n");
                fflush(stdout);
                exit(1);
            }
            else if (type == PROV_BUF)
            {
                if (cqe->res < 0)
                {
                    printf("cqe->res = %d\n", cqe->res);
                    exit(1);
                }
            }
            else if (type == ACCEPT)
            {
                int sock_conn_fd = cqe->res;
                // only read when there is no error, >= 0
                log("accept in io_uring_for_each_cqe");
                if (sock_conn_fd >= 0)
                {
                    connections.emplace(sock_conn_fd, handle_event(sock_conn_fd));
                    auto &h = connections.at(sock_conn_fd).handler;
                    auto &p = h.promise();
                    p.request_info.client_socket = sock_conn_fd;
                    p.uring = this;
                    h.resume();
                }

                // new connected client; read data from socket and re-add accept to monitor for new connections
                add_accept_request(sock_listen_fd, (struct sockaddr *)&client_addr, &client_len, 0);
            }
            else if (type == READ)
            {
                auto &h = connections.at(conn_i.client_socket).handler;
                auto &p = h.promise();
                p.request_info.bid = cqe->flags >> 16;
                p.res = cqe->res;
                h.resume();
            }
            else if (type == WRITE)
            {
                auto &h = connections.at(conn_i.client_socket).handler;
                h.promise().res = cqe->res;
                h.resume();
            }
        }

        io_uring_cq_advance(&ring, count);
    }
}

void io_uring_handler::setup_first_buffer()
{
    buffer.reset(new char[BUFFERS_COUNT][MAX_MESSAGE_LEN]);

    // register buffers for buffer selection
    struct io_uring_sqe *sqe;
    struct io_uring_cqe *cqe;

    sqe = io_uring_get_sqe(&ring);
    io_uring_prep_provide_buffers(sqe, buffer.get(), MAX_MESSAGE_LEN, BUFFERS_COUNT, group_id, 0);

    io_uring_submit(&ring);
    io_uring_wait_cqe(&ring, &cqe);
    if (cqe->res < 0)
    {
        printf("cqe->res = %d\n", cqe->res);
        exit(1);
    }
    io_uring_cqe_seen(&ring, cqe);
}

io_uring_handler::~io_uring_handler()
{
    io_uring_queue_exit(&ring);
}

void io_uring_handler::add_read_request(int fd, request &req)
{
    struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
    io_uring_prep_recv(sqe, fd, NULL, MAX_MESSAGE_LEN, 0);
    io_uring_sqe_set_flags(sqe, IOSQE_BUFFER_SELECT);
    sqe->buf_group = group_id;
    req.event_type = READ;
    sqe->user_data = req.uring_data;
}

void io_uring_handler::add_close_request(int fd)
{
    shutdown(fd, SHUT_RDWR);
    connections.erase(fd);
}


void io_uring_handler::add_write_request(int fd, size_t message_size, request &req)
{
    struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
    io_uring_prep_send(sqe, fd, &buffer[req.bid], message_size, 0);
    io_uring_sqe_set_flags(sqe, 0);
    req.event_type = WRITE;
    sqe->user_data = req.uring_data;
    log("add_write_request %lu", message_size);
}

void io_uring_handler::add_accept_request(int fd, struct sockaddr *client_addr, socklen_t *client_len, unsigned flags)
{
    struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
    io_uring_prep_accept(sqe, fd, client_addr, client_len, 0);
    io_uring_sqe_set_flags(sqe, flags);

    request conn_i;
    conn_i.event_type = ACCEPT;
    conn_i.bid = 0;
    conn_i.client_socket = fd;

    sqe->user_data = conn_i.uring_data;
}

void io_uring_handler::add_buffer_request(request &req)
{
    struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
    io_uring_prep_provide_buffers(sqe, buffer[req.bid], MAX_MESSAGE_LEN, 1, group_id, req.bid);
    req.event_type = PROV_BUF;
    sqe->user_data = req.uring_data;
}

void io_uring_handler::add_open_request()
{
}

#endif
