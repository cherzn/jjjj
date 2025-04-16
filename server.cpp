#include "server.hpp"
#include <iostream>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <algorithm>
#include <thread>
#include <vector>

const std::string Server::ADMIN_PASSWORD = "admin123";

Server::Server() : server_socket(-1) {}

Server::~Server() {
    if (server_socket != -1) {
        close(server_socket);
    }
}

void Server::broadcast(const std::string& message, int sender_socket) {
    std::lock_guard<std::mutex> lock(clients_mutex);

    // Удаляем все закрытые сокеты перед рассылкой
    for (size_t i = 0; i < clients.size(); ) {
        if (clients[i] == -1) {
            clients.erase(clients.begin() + i);
            client_names.erase(client_names.begin() + i);
        } else {
            i++;
        }
    }

    for (int client : clients) {
        if (client != sender_socket && client != -1) {
            if (send(client, message.c_str(), message.size(), 0) <= 0) {
                // Если отправка не удалась, помечаем сокет для удаления
                auto it = std::find(clients.begin(), clients.end(), client);
                if (it != clients.end()) {
                    size_t index = it - clients.begin();
                    clients[index] = -1;
                }
            }
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
    std::string list = "Список клиентов (" + std::to_string(clients.size()) + "):\n";
    for (size_t i = 0; i < clients.size(); ++i) {
        if (clients[i] != -1) {
            list += "- " + client_names[i] + (admin_sockets.count(clients[i]) ? " (admin)" : "") + "\n";
        }
    }
    send(admin_socket, list.c_str(), list.size(), 0);
}

void Server::removeClient(int client_socket) {
    std::lock_guard<std::mutex> lock(clients_mutex);
    auto it = std::find(clients.begin(), clients.end(), client_socket);
    if (it != clients.end()) {
        size_t index = it - clients.begin();
        std::string name = client_names[index];
        clients.erase(it);
        client_names.erase(client_names.begin() + index);
        admin_sockets.erase(client_socket);

        if (client_socket != -1) {
            close(client_socket);
        }

        std::cout << name << " отключен." << std::endl;
        broadcast(name + " покинул чат.\n", -1);
    }
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
        removeClient(client_socket);
        return;
    }
    name = std::string(buffer, bytes);

    // Проверка на админа
    if (name == "admin") {
        memset(buffer, 0, sizeof(buffer));
        bytes = recv(client_socket, buffer, sizeof(buffer), 0);
        if (bytes <= 0) {
            removeClient(client_socket);
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
            removeClient(client_socket);
            return;
        }
    }

    // Получаем ключ шифрования
    memset(buffer, 0, sizeof(buffer));
    bytes = recv(client_socket, buffer, sizeof(buffer), 0);
    if (bytes <= 0) {
        removeClient(client_socket);
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
        if (bytes <= 0) {
            break;
        }

        std::string message(buffer, bytes);

        // Обработка команд админа
        if (is_admin) {
            if (message.substr(0, 6) == "/kick ") {
                std::string target_name = message.substr(6);
                std::vector<int> targets;

                {
                    std::lock_guard<std::mutex> lock(clients_mutex);
                    for (size_t i = 0; i < client_names.size(); ++i) {
                        if (client_names[i] == target_name && clients[i] != client_socket) {
                            targets.push_back(clients[i]);
                            clients[i] = -1;  // Помечаем для удаления
                        }
                    }
                }

                if (!targets.empty()) {
                    for (int target : targets) {
                        const char* kick_msg = "Вы были отключены админом.\n";
                        send(target, kick_msg, strlen(kick_msg), 0);
                        close(target);
                        std::cout << "Админ " << name << " отключил " << target_name << std::endl;
                    }
                    broadcast(target_name + " был отключен админом.\n", client_socket);
                } else {
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
        broadcast(message, client_socket);
    }

    removeClient(client_socket);
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