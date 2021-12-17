#ifndef TASK_H
#define TASK_H

// infiniteDataStream.cpp
#include <coroutine>
#include <memory>
#include <iostream>

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

#endif 