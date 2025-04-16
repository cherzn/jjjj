#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <algorithm>
#include <map>

const int PORT = 8080;
const std::string ADMIN_NAME = "admin";
const std::string ADMIN_PASSWORD = "secret123"; // Пароль админа

std::vector<int> clients;
std::vector<std::string> client_names;
std::mutex clients_mutex;
std::map<int, bool> admin_clients; // Сокет -> является ли админом

void broadcast_message(const std::string& message, int sender_socket) {
    std::lock_guard<std::mutex> lock(clients_mutex);
    for (size_t i = 0; i < clients.size(); ++i) {
        if (clients[i] != sender_socket) {
            send(clients[i], message.c_str(), message.size(), 0);
        }
    }
}

void send_client_list(int admin_socket) {
    std::lock_guard<std::mutex> lock(clients_mutex);
    std::string list = "Список клиентов:\n";
    for (size_t i = 0; i < client_names.size(); ++i) {
        list += "- " + client_names[i] + "\n";
    }
    send(admin_socket, list.c_str(), list.size(), 0);
}

void handle_client(int client_socket) {
    char buffer[4096]; // Увеличим буфер для больших сообщений
    std::string name;
    bool is_admin = false;

    // Получаем имя клиента
    memset(buffer, 0, sizeof(buffer));
    int bytes_received = recv(client_socket, buffer, sizeof(buffer), 0);
    name = std::string(buffer, bytes_received);

    // Проверка на админа
    if (name == ADMIN_NAME) {

        memset(buffer, 0, sizeof(buffer));
        bytes_received = recv(client_socket, buffer, sizeof(buffer), 0);
        std::string password(buffer, bytes_received);

        if (password == ADMIN_PASSWORD) {
            is_admin = true;
            admin_clients[client_socket] = true;
            const char* success_msg = "Вы успешно вошли как админ.\nДоступные команды:\n/kick [имя] - отключить пользователя\n/list - список клиентов\n";
            send(client_socket, success_msg, strlen(success_msg), 0);
        } else {
            const char* fail_msg = "Неверный пароль админа. Отключение.\n";
            send(client_socket, fail_msg, strlen(fail_msg), 0);
            close(client_socket);
            return;
        }
    }

    // Добавляем клиента в списки
    {
        std::lock_guard<std::mutex> lock(clients_mutex);
        clients.push_back(client_socket);
        client_names.push_back(name);
    }

    std::cout << name << " подключился." << (is_admin ? " (Админ)" : "") << std::endl;
    broadcast_message(name + " подключился к чату.\n", client_socket);

    while (true) {
        memset(buffer, 0, sizeof(buffer));
        bytes_received = recv(client_socket, buffer, sizeof(buffer), 0);
        if (bytes_received <= 0) {
            std::cout << name << " отключился." << std::endl;
            break;
        }

        std::string message = std::string(buffer, bytes_received);

        // Обработка команд админа
        if (is_admin) {
            if (message.substr(0, 6) == "/kick ") {
                std::string target_name = message.substr(6);
                std::lock_guard<std::mutex> lock(clients_mutex);

                for (size_t i = 0; i < client_names.size(); i++) {
                    if (client_names[i] == target_name) {
                        int target_socket = clients[i];
                        const char* kick_msg = "Вы были отключены админом.\n";
                        send(target_socket, kick_msg, strlen(kick_msg), 0);
                        close(target_socket);

                        clients.erase(clients.begin() + i);
                        client_names.erase(client_names.begin() + i);

                        std::cout << "Админ " << name << " отключил " << target_name << std::endl;
                        broadcast_message(target_name + " был отключен админом.\n", client_socket);
                        break;
                    }
                }
                continue;
            }
            else if (message == "/list") {
                send_client_list(client_socket);
                continue;
            }
        }

        // Здесь также необходимо удостовериться в правильности шифрования
        std::cout << message << std::endl; // Вывод сообщения на сервере
        broadcast_message(message + "\n", client_socket);
    }

    close(client_socket);
    admin_clients.erase(client_socket);

    {
        std::lock_guard<std::mutex> lock(clients_mutex);
        auto it = std::find(clients.begin(), clients.end(), client_socket);
        if (it != clients.end()) {
            int index = std::distance(clients.begin(), it);
            clients.erase(it);
            client_names.erase(client_names.begin() + index);
            broadcast_message(name + " покинул чат.\n", -1);
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
    std::cout << "Ожидание подключений..." << std::endl;

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
