#include <iostream>
#include <string>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <thread>
#include <atomic>

const int PORT = 8080;
std::atomic<bool> running(true);
std::string name;

void receive_messages(int client_socket) {
    char buffer[1024];
    while (running) {
        memset(buffer, 0, sizeof(buffer));
        int bytes_received = recv(client_socket, buffer, sizeof(buffer), 0);
        if (bytes_received <= 0) {
            std::cout << "Сервер отключился." << std::endl;
            running = false;
            break;
        }
        std::cout << buffer << std::endl; // Выводим сообщение на экран
    }
}

void send_messages(int client_socket) {
    char message[1024];
    while (running) {
        std::cin.getline(message, sizeof(message));

        if (strcmp(message, "exit") == 0) {
            running = false; // Завершаем программу
            break;
        }

        // Отправляем на сервер
        send(client_socket, message, strlen(message), 0);
    }
}

int main() {
    int client_socket;
    struct sockaddr_in server_address;

    client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket == -1) {
        perror("Ошибка создания сокета");
        return -1;
    }

    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(PORT);
    inet_pton(AF_INET, "127.0.0.1", &server_address.sin_addr);

    if (connect(client_socket, (struct sockaddr *)&server_address, sizeof(server_address)) == -1) {
        perror("Ошибка подключения");
        return -1;
    }

    char buffer[1024];
    memset(buffer, 0, sizeof(buffer));
    recv(client_socket, buffer, sizeof(buffer), 0);
    std::cout << buffer; // Выводим запрос на экран

    std::getline(std::cin, name); // Ввод имени клиента
    send(client_socket, name.c_str(), name.size(), 0);

    // Запускаем поток для получения сообщений
    std::thread(receive_messages, client_socket).detach();

    // Запускаем поток для отправки сообщений
    send_messages(client_socket);

    close(client_socket);
    return 0;
}