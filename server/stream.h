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

struct read_socket_awaitable : public stream_base
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

struct write_socket_awaitable : public stream_base
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
        return promise->res;
    }
    char* buffer;
};

auto read_socket(char** buffer_pointer){
    return read_socket_awaitable{nullptr, 0, buffer_pointer};
}

auto write_socket(char* buffer, size_t message_size){
    return write_socket_awaitable{nullptr, message_size, buffer};
}

#endif
