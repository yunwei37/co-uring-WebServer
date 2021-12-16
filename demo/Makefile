CXXFLAGS ?= -Wall -O2 -D_GNU_SOURCE -luring -std=c++2a -fcoroutines
all_targets = io_uring_echo_server

.PHONY: liburing io_uring_echo_server

all: $(all_targets)

clean:
	rm -f $(all_targets)

liburing:
	+$(MAKE) -C ./liburing

io_uring_echo_server:
	$(CXX) io_uring_echo_server.cpp -o ./io_uring_echo_server  ${CXXFLAGS}
