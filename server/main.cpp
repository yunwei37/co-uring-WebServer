#include "server.h"

int main() {
    server server(8000);
    server.start();
    return 0;
}
