#include <cstdlib>
#include <iostream>

#include "gomoku_server.h"

int main(int argc, char* argv[]) {
    int port = 9000;
    if (argc >= 2) {
        port = std::atoi(argv[1]);
        if (port <= 0 || port > 65535) {
            std::cerr << "Invalid port: " << argv[1] << '\n';
            return 1;
        }
    }

    GomokuServer server(port);
    if (!server.start()) {
        return 1;
    }

    server.run();
    return 0;
}
