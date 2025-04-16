#include "server.hpp"
#include <iostream>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <algorithm>
#include <thread>

const std::string Server::ADMIN_PASSWORD = "admin123";

Server::Server() : server_socket(-1) {}

Server::~Server() {
    if (server_socket != -1) close(server_socket);
}

void Server::broadcast(const std::string& message, int sender_socket) {
    std::lock_guard<std::mutex> lock(clients_mutex);
    for (int client : clients) {
        if (client != sender_socket && client != -1) {
            send(client, message.c_str(), message.size(), 0);
        }
    }
}

std::string Server::decryptMessage(const std::string& encrypted, const std::string& key) {
    size_t key_len = key.length();
    std::string decrypted;
    for (size_t i = 0; i < encrypted.length(); ++i) {
        decrypted += encrypted[i] ^ key[i % key_len];
    }
    return decrypted;
}

void Server::sendClientList(int admin_socket) {
    std::lock_guard<std::mutex> lock(clients_mutex);
    std::string list = "Список клиентов (" + std::to_string(client_names.size()) + "):\n";
    for (size_t i = 0; i < client_names.size(); ++i) {
        list += "- " + client_names[i] + (admin_sockets.count(clients[i]) ? " (admin)" : "") + "\n";
    }
    send(admin_socket, list.c_str(), list.size(), 0);
}

void Server::handleClient(int client_socket) {
    char buffer[4096];
    std::string name;
    std::string encryption_key;
    bool is_admin = false;

    // Получаем имя клиента
    memset(buffer, 0, sizeof(buffer));
    int bytes = recv(client_socket, buffer, sizeof(buffer), 0);
    if (bytes <= 0) {
        close(client_socket);
        return;
    }
    name = std::string(buffer, bytes);

    // Проверка на админа
    if (name == "admin") {
        memset(buffer, 0, sizeof(buffer));
        bytes = recv(client_socket, buffer, sizeof(buffer), 0);
        if (bytes <= 0) {
            close(client_socket);
            return;
        }
        std::string password(buffer, bytes);

        if (password == ADMIN_PASSWORD) {
            is_admin = true;
            admin_sockets[client_socket] = true;
            const char* msg = "Вы успешно вошли как админ.\nДоступные команды:\n/kick [имя] - отключить пользователя\n/list - список клиентов\n";
            send(client_socket, msg, strlen(msg), 0);
        } else {
            const char* msg = "Неверный пароль админа. Отключение.\n";
            send(client_socket, msg, strlen(msg), 0);
            close(client_socket);
            return;
        }
    }

    // Получаем ключ шифрования
    memset(buffer, 0, sizeof(buffer));
    bytes = recv(client_socket, buffer, sizeof(buffer), 0);
    if (bytes <= 0) {
        close(client_socket);
        return;
    }
    encryption_key = std::string(buffer, bytes);

    // Добавляем клиента
    {
        std::lock_guard<std::mutex> lock(clients_mutex);
        clients.push_back(client_socket);
        client_names.push_back(name);
    }

    std::cout << name << " подключился." << (is_admin ? " (Админ)" : "") << std::endl;
    broadcast(name + " подключился к чату.\n", client_socket);

    while (true) {
        memset(buffer, 0, sizeof(buffer));
        bytes = recv(client_socket, buffer, sizeof(buffer), 0);
        if (bytes <= 0) break;

        std::string message(buffer, bytes);

        // Обработка команд админа
        if (is_admin) {
            if (message.substr(0, 6) == "/kick ") {
                std::string target_name = message.substr(6);
                std::lock_guard<std::mutex> lock(clients_mutex);

                bool found = false;
                for (size_t i = 0; i < client_names.size(); i++) {
                    if (client_names[i] == target_name) {
                        int target_socket = clients[i];

                        // Не даем админу кикнуть самого себя
                        if (target_socket == client_socket) {
                            send(client_socket, "Вы не можете отключить себя.\n", 28, 0);
                            found = true;
                            break;
                        }

                        const char* kick_msg = "Вы были отключены админом.\n";
                        send(target_socket, kick_msg, strlen(kick_msg), 0);
                        close(target_socket);

                        // Помечаем сокет как закрытый
                        clients[i] = -1;

                        std::cout << "Админ " << name << " отключил " << target_name << std::endl;
                        broadcast(target_name + " был отключен админом.\n", client_socket);
                        found = true;
                        break;
                    }
                }

                if (!found) {
                    std::string error_msg = "Пользователь '" + target_name + "' не найден.\n";
                    send(client_socket, error_msg.c_str(), error_msg.size(), 0);
                }
                continue;
            }
            else if (message == "/list") {
                sendClientList(client_socket);
                continue;
            }
        }

        // Обычное сообщение
        std::string decrypted = decryptMessage(message, encryption_key);
        std::cout << "Получено от " << name << ": " << decrypted << std::endl;
        broadcast(name + ": " + message, client_socket);
    }

    // Удаляем клиента при отключении
    close(client_socket);
    admin_sockets.erase(client_socket);

    {
        std::lock_guard<std::mutex> lock(clients_mutex);
        auto it = std::find(clients.begin(), clients.end(), client_socket);
        if (it != clients.end()) {
            size_t index = it - clients.begin();
            std::string disconnected_name = client_names[index];
            clients.erase(it);
            client_names.erase(client_names.begin() + index);
            broadcast(disconnected_name + " покинул чат.\n", -1);
            std::cout << disconnected_name << " отключился." << std::endl;
        }
    }
}

void Server::start() {
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1) {
        throw std::runtime_error("Ошибка создания сокета");
    }

    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(server_socket, (sockaddr*)&server_addr, sizeof(server_addr))) {
        throw std::runtime_error("Ошибка привязки сокета");
    }

    if (listen(server_socket, 5)) {
        throw std::runtime_error("Ошибка прослушивания");
    }

    std::cout << "Сервер запущен на порту " << PORT << std::endl;

    while (true) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        int client_socket = accept(server_socket, (sockaddr*)&client_addr, &client_len);

        if (client_socket < 0) {
            std::cerr << "Ошибка подключения клиента" << std::endl;
            continue;
        }

        const char* msg = "Введите ваше имя: ";
        send(client_socket, msg, strlen(msg), 0);

        std::thread([this, client_socket]() {
            this->handleClient(client_socket);
        }).detach();
    }
}