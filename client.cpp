#include <iostream>
#include <string>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <thread>
#include <atomic>
#include <vector>
#include <sstream>

const int PORT = 8080;
std::atomic<bool> running(true);
std::string name;
std::string encryption_key; // Кодировочное слово
const std::string delimiter = "|"; // Используем разделитель


// Предполагается, что utf8_length корректно считает количество символов UTF-8
size_t utf8_length(const std::string &str) {
    size_t length = 0;
    for (size_t i = 0; i < str.size(); ) {
        unsigned char c = str[i];
        if (c <= 0x7F) { i += 1; }
        else if ((c & 0xE0) == 0xC0) { i += 2; }
        else if ((c & 0xF0) == 0xE0) { i += 3; }
        else if ((c & 0xF8) == 0xF0) { i += 4; }
        else { i += 1; } // Некорректный UTF-8, пропускаем 1 байт
        length++;
    }
    return length;
}

std::string encrypt_message(std::string message, const std::string &kode) {
    size_t cols = utf8_length(kode);
    if (cols == 0) return ""; // На случай пустого кода

    size_t message_length = utf8_length(message);
    size_t rows = (message_length + cols - 1) / cols; // Округление вверх

    // Вместо матрицы char[100][401] используем vector<vector<string>> для хранения символов
    std::vector<std::vector<std::string> > matrix(rows, std::vector<std::string>(cols, "*"));

    // Заполнение матрицы символами (каждый символ UTF-8 хранится как строка)
    size_t current_pos = 0;
    for (size_t i = 0; i < rows; ++i) {
        for (size_t j = 0; j < cols; ++j) {
            if (current_pos >= message.size()) break;

            // Определяем длину текущего символа UTF-8
            unsigned char c = message[current_pos];
            size_t char_len = 1;
            if ((c & 0xE0) == 0xC0) { char_len = 2; }
            else if ((c & 0xF0) == 0xE0) { char_len = 3; }
            else if ((c & 0xF8) == 0xF0) { char_len = 4; }

            // Извлекаем символ
            std::string symbol = message.substr(current_pos, char_len);
            if (symbol == " ") {
                matrix[i][j] = "*";
            } else {
                matrix[i][j] = symbol;
            }
            current_pos += char_len;
        }
    }

    // Чтение матрицы по столбцам
    std::string encrypted_message;
    for (size_t j = 0; j < cols; ++j) {
        for (size_t i = 0; i < rows; ++i) {
            encrypted_message += matrix[i][j];
        }
    }

    return encrypted_message;
}
std::string decrypt_message(const std::string &buffer, const std::string &kode) {
    size_t cols = utf8_length(kode);
    if (cols == 0) return buffer;

    size_t total_chars = utf8_length(buffer);
    size_t rows = (total_chars) / cols;

    // Создаем матрицу для хранения символов
    std::vector<std::vector<std::string> > matrix(rows, std::vector<std::string>(cols));

    // Заполняем матрицу по колонкам (как при шифровании)
    size_t buf_pos = 0;
    for (size_t j = 0; j < cols; ++j) {
        for (size_t i = 0; i < rows; ++i) {

            unsigned char c = buffer[buf_pos];
            size_t char_len = 1;
            if ((c & 0xE0) == 0xC0) char_len = 2;
            else if ((c & 0xF0) == 0xE0) char_len = 3;
            else if ((c & 0xF8) == 0xF0) char_len = 4;

            // Извлекаем символ
            matrix[i][j] = buffer.substr(buf_pos, char_len);
            if (matrix[i][j] == "*") matrix[i][j] = " "; // Заменяем * на пробелы
            buf_pos += char_len;
        }
    }

    // Читаем матрицу по строкам
    std::string result;
    for (size_t i = 0; i < rows; ++i) {
        for (size_t j = 0; j < cols; ++j) {
            result += matrix[i][j];
        }
    }

    // Удаляем лишние пробелы в конце (которые были *)
    size_t end = result.find_last_not_of(" ");
    if (end != std::string::npos) {
        result = result.substr(0, end + 1);
    }

    return result;
}
void receive_messages(int client_socket) {
    char buffer[4096];
    while (running) {
        memset(buffer, 0, sizeof(buffer));
        int bytes_received = recv(client_socket, buffer, sizeof(buffer), 0);
        if (bytes_received <= 0) {
            std::cout << "Сервер отключился." << std::endl;
            running = false;
            break;
        }

        std::string received_message(buffer, bytes_received);


        if (received_message.find("подключился") != std::string::npos ||
            received_message.find("покинул") != std::string::npos ||
            received_message.find("отключен") != std::string::npos) {
            std::cout << received_message;
            continue;
        }

        if (received_message == name) {
            continue;
        }

        if (received_message.find(delimiter) != std::string::npos) {
            std::string sender_name = received_message.substr(0, received_message.find(delimiter));
            std::string enc_message = received_message.substr(received_message.find(delimiter) + 1);
            std::string decrypted_message = decrypt_message(enc_message, encryption_key);

            if (sender_name != name && !decrypted_message.empty()) {
                std::cout << sender_name << ": "<< decrypted_message << std::endl;
            }
        } else {
            std::cout << received_message;
        }
    }
}

void send_messages(int client_socket) {
    std::string message;
    while (running) {
        std::getline(std::cin, message);

        if (message == "exit") {
            running = false;
            break;
        }

        std::string full_message;
        if (name == "admin" && (message.substr(0, 6) == "/kick " || message == "/list")) {
            full_message = message;
        } else {
            full_message = name + delimiter + encrypt_message(message, encryption_key);
        }

        send(client_socket, full_message.c_str(), full_message.size(), 0);
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
    std::cout << buffer;

    std::getline(std::cin, name);
    send(client_socket, name.c_str(), name.size(), 0);

    if (name == "admin") {
        std::string password;

        std::getline(std::cin, password);
        send(client_socket, password.c_str(), password.size(), 0);

        memset(buffer, 0, sizeof(buffer));
        recv(client_socket, buffer, sizeof(buffer), 0);
        std::cout << buffer;

        if (strstr(buffer, "Неверный") != nullptr) {
            close(client_socket);
            return 0;
        }
    }

    std::cout << "Введите код шифрования (русские буквы): ";
    std::getline(std::cin, encryption_key);

    std::thread(receive_messages, client_socket).detach();
    send_messages(client_socket);

    close(client_socket);
    return 0;
}