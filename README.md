# co-uring-webserver

Co-uring-webserver is a simple high performance Web Server written in cpp20 for learning:

- io_uring support for non-blocking IO
- write concurrent code using C++20 coroutine

This repo also contains a series of learning materials for c++20 and io_uring, along with demos.

本项目为 C++20 编写的 Web 服务器，可处理静态资源, 同时也包含了一些学习 c++20 与 io_uring 的相关资料:

- 使用带有 IORING_OP_PROVIDE_BUFFERS 和 IORING_FEAT_FAST_POLL 的 io_uring 技术进行非阻塞 IO 请求，避免系统调用开销以及用户态-内核态内存复制；
- 采用 c++20 std::coroutine 处理异步回调；
- TODO: 线程池
- TODO

## demos to learn C++20, io_uring and test benchmark

- [io_uring_coroutine_echo_server.cpp](demo\io_uring_coroutine_echo_server.cpp) an echo server with liburing and cpp coroutine
- [io_uring_echo_server.c](demo\io_uring_echo_server.c) an echo server with liburing in c
- [io_uring_echo_server.cpp](demo\io_uring_echo_server.cpp) an echo server with liburing in cpp
- [uring_server.c](demo\uring_server.c) a basic static server with liburing
- [liburing_cat.cpp](demo\liburing_cat.c) a cat program with liburing
- [uring_cat.cpp](demo\uring_cat.c) a cat program using io_uring syscall interface

## requirements

- Linux 5.7 or higher with IORING_FEAT_FAST_POLL and IORING_OP_PROVIDE_BUFFERS required
- gcc 10.0 or higher

## build

## Document in Chinese

这里也包含了一些在学习 c++20 与 io_uring 相关知识的时候，翻译和撰写的中文文档，以及对应的原型实现：

- io_uring 从原理到动手实践: 关于 iouring 的基本使用细节
  - [io_uring 从原理到动手实践 part1: 使用系统调用接口实现 cat 程序](document\io_uring-by-example\io_uring-by-example1.md)
  - [io_uring 从原理到动手实践 part2: liburing](document\io_uring-by-example\io_uring-by-example2.md)
  - [io_uring 从原理到动手实践 part3: 使用 liburing 实现的一个网络服务器](document\io_uring-by-example\io_uring-by-example3.md)
- c++20 协程：
  - TODO
- [使用 c++20 协程与 io_uring 实现高性能web服务器 part1：从 echo server 开始](document\part1.md)

## benchmark

- for echo server： see [document/part1.md](document/part1.md)
- TODO

## reference

repos:

- the liburing library [github.com/axboe/liburing](https://github.com/axboe/liburing)
- an echo server with liburing in c [github.com/frevib/io_uring-echo-server](https://github.com/frevib/io_uring-echo-server)
- a simple rust echo benchmark tester [github.com/haraldh/rust_echo_bench](https://github.com/haraldh/rust_echo_bench)
- [github.com/lewissbaker/cppcoro](https://github.com/lewissbaker/cppcoro)
- [github.com/facebookexperimental/libunifex](https://github.com/facebookexperimental/libunifex)
- [github.com/netcan/asyncio](https://github.com/netcan/asyncio)

articles:

- cpp reference [en.cppreference.com/w/cpp/language/coroutines](https://en.cppreference.com/w/cpp/language/coroutines)
- modernescpp [www.modernescpp.com/index.php/tag/coroutines](https://www.modernescpp.com/index.php/tag/coroutines)
- 