#include "server.h"
#include <iostream>

int main() {
    log("main()\n");
    server server(8000);
    server.start();
    return 0;
}
