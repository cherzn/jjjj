#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <algorithm>

const int PORT = 8080;
std::vector<int> clients;
std::vector<std::string> client_names;
std::mutex clients_mutex;

void broadcast_message(const std::string& message, int sender_socket) {
    std::lock_guard<std::mutex> lock(clients_mutex);
    for (size_t i = 0; i < clients.size(); ++i) {
        if (clients[i] != sender_socket) {
            send(clients[i], message.c_str(), message.size(), 0);
        }
    }
}

void handle_client(int client_socket) {
    char buffer[1024];
    std::string name;
    memset(buffer, 0, sizeof(buffer));
    int bytes_received = recv(client_socket, buffer, sizeof(buffer), 0);
    name = std::string(buffer, bytes_received); // Сохраняем имя клиента

    {
        std::lock_guard<std::mutex> lock(clients_mutex);
        clients.push_back(client_socket);
        client_names.push_back(name);
    }

    std::cout << name << " подключился." << std::endl;

    while (true) {
        memset(buffer, 0, sizeof(buffer));
        bytes_received = recv(client_socket, buffer, sizeof(buffer), 0);
        if (bytes_received <= 0) {
            std::cout << name << " отключился." << std::endl;
            break;
        }

        std::string message = std::string(buffer, bytes_received);
        std::cout << name << ": " << message << std::endl; // Выводим сообщение на сервере
        broadcast_message( message, client_socket);
    }

    close(client_socket);

    {
        std::lock_guard<std::mutex> lock(clients_mutex);
        auto it = std::find(clients.begin(), clients.end(), client_socket);
        if (it != clients.end()) {
            int index = std::distance(clients.begin(), it);
            clients.erase(it);
            client_names.erase(client_names.begin() + index);
        }
    }
}

int main() {
    int server_socket;
    struct sockaddr_in server_address;

    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1) {
        perror("Ошибка создания сокета");
        return -1;
    }

    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = INADDR_ANY;
    server_address.sin_port = htons(PORT);

    if (bind(server_socket, (struct sockaddr *)&server_address, sizeof(server_address)) < 0) {
        perror("Ошибка привязки");
        return -1;
    }

    if (listen(server_socket, 3) < 0) {
        perror("Ошибка прослушивания");
        return -1;
    }

    std::cout << "Сервер запущен на порту " << PORT << std::endl;

    while (true) {
        int client_socket = accept(server_socket, nullptr, nullptr);
        if (client_socket < 0) {
            perror("Ошибка подключения");
            continue;
        }

        const char* request_name = "Введите ваше имя: ";
        send(client_socket, request_name, strlen(request_name), 0);
        std::thread(handle_client, client_socket).detach();
    }

    close(server_socket);
    return 0;
}