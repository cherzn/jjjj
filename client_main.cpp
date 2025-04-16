#include "client.hpp"
#include <iostream>
#include <stdexcept>

int main() {
    try {
        Client client;
        client.connectToServer("127.0.0.1");
        client.run();
    } catch (const std::exception &e) {
        std::cerr << "Ошибка: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}