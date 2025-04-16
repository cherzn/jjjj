#include "client.hpp"
#include <iostream>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <thread>

const int PORT = 8080;
const std::string DELIMITER = "|";

// Статические функции для шифрования/дешифрования
namespace {
    size_t utf8_length(const std::string &str) {
        size_t length = 0;
        for (size_t i = 0; i < str.size(); ) {
            unsigned char c = str[i];
            if (c <= 0x7F) { i += 1; }
            else if ((c & 0xE0) == 0xC0) { i += 2; }
            else if ((c & 0xF0) == 0xE0) { i += 3; }
            else if ((c & 0xF8) == 0xF0) { i += 4; }
            else { i += 1; }
            length++;
        }
        return length;
    }

    std::string encrypt_message(const std::string &message, const std::string &key) {
        size_t cols = utf8_length(key);
        if (cols == 0) return "";

        size_t message_length = utf8_length(message);
        size_t rows = (message_length + cols - 1) / cols;

        std::vector<std::vector<std::string> > matrix(rows, std::vector<std::string>(cols, "*"));

        size_t current_pos = 0;
        for (size_t i = 0; i < rows; ++i) {
            for (size_t j = 0; j < cols; ++j) {
                if (current_pos >= message.size()) break;

                unsigned char c = message[current_pos];
                size_t char_len = 1;
                if ((c & 0xE0) == 0xC0) char_len = 2;
                else if ((c & 0xF0) == 0xE0) char_len = 3;
                else if ((c & 0xF8) == 0xF0) char_len = 4;

                std::string symbol = message.substr(current_pos, char_len);
                matrix[i][j] = (symbol == " ") ? "*" : symbol;
                current_pos += char_len;
            }
        }

        std::string encrypted;
        for (size_t j = 0; j < cols; ++j) {
            for (size_t i = 0; i < rows; ++i) {
                encrypted += matrix[i][j];
            }
        }

        return encrypted;
    }

    std::string decrypt_message(const std::string &buffer, const std::string &key) {
        size_t cols = utf8_length(key);
        if (cols == 0) return buffer;

        size_t total_chars = utf8_length(buffer);
        size_t rows = (total_chars + cols - 1) / cols;

        std::vector<std::vector<std::string> > matrix(rows, std::vector<std::string>(cols));

        size_t buf_pos = 0;
        for (size_t j = 0; j < cols; ++j) {
            for (size_t i = 0; i < rows; ++i) {
                if (buf_pos >= buffer.size()) {
                    matrix[i][j] = " ";
                    continue;
                }

                unsigned char c = buffer[buf_pos];
                size_t char_len = 1;
                if ((c & 0xE0) == 0xC0) char_len = 2;
                else if ((c & 0xF0) == 0xE0) char_len = 3;
                else if ((c & 0xF8) == 0xF0) char_len = 4;

                if (buf_pos + char_len > buffer.size()) char_len = 1;

                matrix[i][j] = buffer.substr(buf_pos, char_len);
                if (matrix[i][j] == "*") matrix[i][j] = " ";
                buf_pos += char_len;
            }
        }

        std::string result;
        for (size_t i = 0; i < rows; ++i) {
            for (size_t j = 0; j < cols; ++j) {
                result += matrix[i][j];
            }
        }

        size_t end = result.find_last_not_of(" ");
        if (end != std::string::npos) {
            result = result.substr(0, end + 1);
        }

        return result;
    }
}

Client::Client() : client_socket(-1), running(true) {}

Client::~Client() {
    if (client_socket != -1) close(client_socket);
}

void Client::connectToServer(const std::string &ip) {
    client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket == -1) {
        throw std::runtime_error("Ошибка создания сокета");
    }

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    inet_pton(AF_INET, ip.c_str(), &server_addr.sin_addr);

    if (connect(client_socket, (sockaddr*)&server_addr, sizeof(server_addr))) {
        throw std::runtime_error("Ошибка подключения к серверу");
    }

    char buffer[1024];
    memset(buffer, 0, sizeof(buffer));
    recv(client_socket, buffer, sizeof(buffer), 0);
    std::cout << buffer;

    std::getline(std::cin, name);
    send(client_socket, name.c_str(), name.size(), 0);

    if (name == "admin") {
        std::cout << "Введите пароль админа: ";
        std::string password;
        std::getline(std::cin, password);
        send(client_socket, password.c_str(), password.size(), 0);

        memset(buffer, 0, sizeof(buffer));
        recv(client_socket, buffer, sizeof(buffer), 0);
        std::cout << buffer;

        if (strstr(buffer, "Неверный")) {
            throw std::runtime_error("Неверный пароль админа");
        }
    }

    std::cout << "Введите код шифрования (русские буквы): ";
    std::getline(std::cin, encryption_key);
    send(client_socket, encryption_key.c_str(), encryption_key.size(), 0);
}

void Client::receiveMessages() {
    char buffer[4096];
    while (running) {
        memset(buffer, 0, sizeof(buffer));
        int bytes = recv(client_socket, buffer, sizeof(buffer), 0);
        if (bytes <= 0) {
            std::cout << "Сервер отключился." << std::endl;
            running = false;
            break;
        }

        std::string msg(buffer, bytes);
        if (msg.find(DELIMITER) != std::string::npos) {
            size_t pos = msg.find(DELIMITER);
            std::string sender = msg.substr(0, pos);
            std::string content = msg.substr(pos + 1);
            std::string decrypted = decrypt_message(content, encryption_key);
            std::cout << sender << ": " << decrypted << std::endl;
        } else {
            std::cout << msg;
        }
    }
}

void Client::sendMessages() {
    std::string msg;
    while (running) {
        std::getline(std::cin, msg);
        if (msg == "exit") {
            running = false;
            break;
        }

        if (name == "admin" && (msg.substr(0, 6) == "/kick " || msg == "/list")) {
            send(client_socket, msg.c_str(), msg.size(), 0);
        } else {
            std::string encrypted = name + DELIMITER + encrypt_message(msg, encryption_key);
            send(client_socket, encrypted.c_str(), encrypted.size(), 0);
        }
    }
}

void Client::run() {
    std::thread receiver(&Client::receiveMessages, this);
    std::thread sender(&Client::sendMessages, this);

    receiver.join();
    sender.join();
}