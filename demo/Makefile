CXXFLAGS ?= -Wall -O2 -D_GNU_SOURCE -luring -std=c++2a -fcoroutines
all_targets = io_uring_echo_server io_uring_coroutine_echo_server
build_dir = ../build/demo/

.PHONY: liburing io_uring_echo_server

all: $(all_targets)

clean:
	rm -rf $(build_dir)

liburing:
	+$(MAKE) -C ./liburing

make_build:
	mkdir $(build_dir)

io_uring_echo_server: make_build
	$(CXX) io_uring_echo_server.cpp -o $(build_dir)io_uring_echo_server  ${CXXFLAGS}

io_uring_coroutine_echo_server: make_build
	$(CXX) io_uring_coroutine_echo_server.cpp -o $(build_dir)io_uring_coroutine_echo_server  ${CXXFLAGS}

benchmark: io_uring_coroutine_echo_server
	./io_uring_coroutine_echo_server 8888
	./test_echo_server 8888

