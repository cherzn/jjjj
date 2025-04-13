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
std::string encryption_key; // Кодировочное слово

size_t utf8_length(const std::string &str) {
    size_t length = 0;
    for (size_t i = 0; i < str.length(); ++i) {
        if ((str[i] & 0x80) == 0) {
            length++;
        } else if ((str[i] & 0xE0) == 0xC0) {
            length++;
            i++;
        } else if ((str[i] & 0xF0) == 0xE0) {
            length++;
            i += 2;
        } else if ((str[i] & 0xF8) == 0xF0) {
            length++;
            i += 3;
        }
    }
    return length;
}

std::string encrypt_message(const std::string &message, const std::string &kode) {
    size_t cols = utf8_length(kode);
    size_t message_length = utf8_length(message);
    size_t rows = (message_length / cols) + (message_length % cols == 0 ? 0 : 1);

    char matrix[100][401] = {};
    size_t index = 0;

    // Заполнение матрицы
    for (size_t i = 0; i < rows; ++i) {
        for (size_t j = 0; j < cols; ++j) {
            if (index < message.length()) {
                size_t char_length = 1;
                if ((message[index] & 0x80) == 0) {
                    char_length = 1;
                } else if ((message[index] & 0xE0) == 0xC0) {
                    char_length = 2;
                } else if ((message[index] & 0xF0) == 0xE0) {
                    char_length = 3;
                } else if ((message[index] & 0xF8) == 0xF0) {
                    char_length = 4;
                }

                for (size_t k = 0; k < char_length; ++k) {
                    matrix[i][j * 4 + k] = (index + k < message.length()) ? message[index + k] : ' ';
                }
                index += char_length;
            }
        }
    }

    std::string encrypted_message;

    // Формирование зашифрованного сообщения
    for (size_t j = 0; j < cols; ++j) {
        for (size_t i = 0; i < rows; ++i) {
            for (size_t k = 0; k < 4; ++k) {
                if (matrix[i][j * 4 + k] != 0) {
                    encrypted_message += matrix[i][j * 4 + k];
                }
            }
        }
    }

    return encrypted_message;
}

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

        // Шифрование сообщения
        std::string encrypted_message = encrypt_message(message, encryption_key);

        // Отправляем зашифрованное сообщение на сервер
        send(client_socket, encrypted_message.c_str(), encrypted_message.size(), 0);
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

    std::cout << "Введите код шифрования (русские буквы): ";
    std::getline(std::cin, encryption_key); // Ввод кодировочного слова

    // Запускаем поток для получения сообщений
    std::thread(receive_messages, client_socket).detach();

    // Запускаем поток для отправки сообщений
    send_messages(client_socket);

    close(client_socket);
    return 0;
}