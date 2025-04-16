#include "server.hpp"
#include <iostream>

int main() {
    try {
        Server server;
        server.start();
    } catch (const std::exception& e) {
        std::cerr << "Ошибка сервера: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}