#ifndef STREAM_H
#define STREAM_H

#include <coroutine>
#include <memory>
#include "task.h"

class stream_base
{
    int fd;
};

class read_awaitable: stream_base
{
    bool await_ready() { return false; }
    void await_suspend(std::coroutine_handle<conn_task::promise_type> h) {
        auto &p = h.promise();
        
        this->p = &p;
    }
    size_t await_resume() {
        return p->res;
    }
    size_t message_size;
    unsigned flags;
    conn_task::promise_type* p = NULL;
};

class write_awaitable: stream_base
{

};

#endif
