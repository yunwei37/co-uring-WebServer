#ifndef TASK_H
#define TASK_H

// infiniteDataStream.cpp
#include <coroutine>
#include <memory>
#include <iostream>

enum task_option {
    ACCEPT,
    READ,
    WRITE,
    OPEN,
    PROV_BUF,
};

union request {
    struct{
            short event_type;
            short bid;
            int client_socket;
    };
    unsigned long long uring_data;
};

static_assert(sizeof(request) == 8);

struct task {
    struct promise_type
    {
        using Handle = std::coroutine_handle<promise_type>;
        task get_return_object()
        {
            return task{Handle::from_promise(*this)};
        }
        std::suspend_always initial_suspend() noexcept { 
            return {}; 
        }
        std::suspend_never final_suspend() noexcept { return {}; }
        void return_void() noexcept {}
        void unhandled_exception() noexcept {}
        
        request request_info;  
        size_t res;
    };
    explicit task(promise_type::Handle handler) : handler(handler) {}
    void destroy() { handler.destroy(); }
    task(const task &) = delete;
    task &operator=(const task &) = delete;
    task(task &&t) noexcept : handler(t.handler) { t.handler = {}; }
    task &operator=(task &&t) noexcept
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