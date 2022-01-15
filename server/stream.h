#ifndef STREAM_H
#define STREAM_H

#include <coroutine>
#include <memory>
#include "io_uring.h"

struct stream_base
{
    task::promise_type *promise = NULL;
    size_t message_size;
};

struct read_awaitable : public stream_base
{
    bool await_ready() { return false; }
    void await_suspend(std::coroutine_handle<task::promise_type> h)
    {
        auto &promise = h.promise();
        this->promise = &promise;
        promise.uring->add_read_request(promise.request_info.client_socket, promise.request_info);
    }
    size_t await_resume()
    {   
        *buffer_pointer = promise->uring->
            get_buffer_pointer(promise->request_info.bid);
        return promise->res;
    }
    char** buffer_pointer;
};

struct write_awaitable : public stream_base
{
    bool await_ready() { return false; }
    void await_suspend(std::coroutine_handle<task::promise_type> h)
    {
        auto &promise = h.promise();
        this->promise = &promise;
        promise.request_info.bid = promise.uring->get_buffer_id(buffer);
        promise.uring->add_write_request(promise.request_info.client_socket, message_size, promise.request_info);
    }
    size_t await_resume()
    {   
        promise->uring->add_buffer_request(promise->request_info);
        return promise->res;
    }
    char* buffer;
};

struct read_file_awaitable : public read_awaitable {
    void await_suspend(std::coroutine_handle<task::promise_type> h)
    {
        auto &promise = h.promise();
        this->promise = &promise;
        promise.uring->add_read_request(read_fd, promise.request_info);
    }
    int read_fd;
};

struct write_file_awaitable : public write_awaitable {
    void await_suspend(std::coroutine_handle<task::promise_type> h)
    {
        auto &promise = h.promise();
        this->promise = &promise;
        promise.request_info.bid = promise.uring->get_buffer_id(buffer);
        promise.uring->add_write_request(write_fd, message_size, promise.request_info);
    }
    int write_fd;
};

auto read_socket(char** buffer_pointer){
    return read_awaitable{nullptr, 0, buffer_pointer};
}

auto read_fd(int fd, char** buffer_pointer){
    return read_file_awaitable{nullptr, 0, buffer_pointer, fd};
}

auto write_fd(int fd, char* buffer, size_t message_size){
    return write_file_awaitable{nullptr, message_size, buffer, fd};
}

auto write_socket(char* buffer, size_t message_size){
    return write_awaitable{nullptr, message_size, buffer};
}

#endif
