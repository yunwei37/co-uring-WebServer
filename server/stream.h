#ifndef STREAM_H
#define STREAM_H

#include <coroutine>
#include <memory>
#include "io_uring.h"

struct stream_base
{
    stream_base(task::promise_type *promise, size_t message_size)
        : promise(promise),
          message_size(message_size) {}
    task::promise_type *promise = NULL;
    size_t message_size;
};

struct read_awaitable : public stream_base
{
    read_awaitable(task::promise_type *promise, size_t message_size, char **buffer_pointer)
        : stream_base(promise, message_size),
          buffer_pointer(buffer_pointer) {}
    bool await_ready() { return false; }
    void await_suspend(std::coroutine_handle<task::promise_type> h)
    {
        auto &promise = h.promise();
        this->promise = &promise;
        promise.uring->add_read_request(promise.request_info.client_socket, promise.request_info);
    }
    size_t await_resume()
    {
        *buffer_pointer = promise->uring->get_buffer_pointer(promise->request_info.bid);
        return promise->res;
    }
    char **buffer_pointer;
};

struct write_awaitable : public stream_base
{
    write_awaitable(task::promise_type *promise, size_t message_size, char *buffer)
        : stream_base(promise, message_size),
          buffer(buffer) {}
    bool await_ready() { return false; }
    void await_suspend(std::coroutine_handle<task::promise_type> h)
    {
        auto &promise = h.promise();
        this->promise = &promise;
        promise.request_info.bid = promise.uring->get_buffer_id(buffer);
        promise.uring->add_write_request(promise.request_info.client_socket, message_size, promise.request_info);
        log("write await_suspend %lu", message_size);
    }
    size_t await_resume()
    {
        promise->uring->add_buffer_request(promise->request_info);
        return promise->res;
    }
    char *buffer;
};

struct read_file_awaitable : public read_awaitable
{   
    read_file_awaitable(task::promise_type *promise, size_t message_size, char **buffer_pointer, int read_fd)
        : read_awaitable(promise, message_size, buffer_pointer),
          read_fd(read_fd) {}
    void await_suspend(std::coroutine_handle<task::promise_type> h)
    {
        auto &promise = h.promise();
        this->promise = &promise;
        promise.uring->add_read_request(read_fd, promise.request_info);
    }
    int read_fd;
};

struct write_file_awaitable : public write_awaitable
{
    write_file_awaitable(task::promise_type *promise, size_t message_size, char *buffer, int write_fd)
        : write_awaitable(promise, message_size, buffer),
          write_fd(write_fd) {}
    void await_suspend(std::coroutine_handle<task::promise_type> h)
    {
        auto &promise = h.promise();
        this->promise = &promise;
        promise.request_info.bid = promise.uring->get_buffer_id(buffer);
        promise.uring->add_write_request(write_fd, message_size, promise.request_info);
    }
    int write_fd;
};

struct close_awaitable
{   
    close_awaitable(int fd): fd(fd) {};
    bool await_ready() { return true; }
    void await_suspend(std::coroutine_handle<task::promise_type> h)
    {
        auto &promise = h.promise();
        promise.uring->add_close_request(fd);
    }
    void await_resume()
    {
    }
    int fd;
};

auto read_socket(char **buffer_pointer)
{
    return read_awaitable(nullptr, 0, buffer_pointer);
}

auto read_fd(int fd, char **buffer_pointer)
{
    return read_file_awaitable(nullptr, 0, buffer_pointer, fd);
}

auto write_fd(int fd, char *buffer, size_t message_size)
{
    return write_file_awaitable(nullptr, message_size, buffer, fd);
}

auto write_socket(char *buffer, size_t message_size)
{   
    log("write write_socket %lu", message_size);
    return write_awaitable(nullptr, message_size, buffer);
}

auto shutdown_socket(int fd)
{
    return close_awaitable(fd);
}

#endif
