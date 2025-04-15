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

std::string encrypt_message(std::string message, const std::string &kode) {
    size_t cols = utf8_length(kode);
    size_t message_length = utf8_length(message);
    size_t total_length = cols * 4; // Общая длина, которую нужно получить в матрице

    // Проверяем, нужно ли добавлять '*' к сообщению
    if (message_length < total_length) {
        size_t stars_to_add = total_length - message_length;
        message.append(stars_to_add, '*'); // Добавляем '*' до достижения нужной длины
    }

    size_t rows = (message.length() / cols) + (message.length() % cols == 0 ? 0 : 1);

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
                    if (index + k < message.length()) {
                        char c = message[index + k];
                        if (c == ' '){matrix[i][j * 4 + k] = '*';}
                        else{matrix[i][j * 4 + k] = c;} // Оставляем все символы неизменными

                    }
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
std::string decrypt_message(const std::string &buffer, const std::string &kode) {
    size_t cols = utf8_length(kode);
    size_t buffer_length = utf8_length(buffer);
    size_t rows = (buffer_length + cols - 1) / cols; // Правильный расчет количества строк

    std::vector<std::string> matrix(rows, std::string(cols * 4, ' ')); // Создаем матрицу

    size_t index2 = 0;

    // Заполняем матрицу по столбцам
    for (size_t j = 0; j < cols; ++j) {
        for (size_t i = 0; i < rows; ++i) {
            if (index2 < buffer.size()) {
                size_t char_length = 1;
                if ((buffer[index2] & 0x80) == 0) {
                    char_length = 1;
                } else if ((buffer[index2] & 0xE0) == 0xC0) {
                    char_length = 2;
                } else if ((buffer[index2] & 0xF0) == 0xE0) {
                    char_length = 3;
                } else if ((buffer[index2] & 0xF8) == 0xF0) {
                    char_length = 4;
                }

                // Заполняем строку
                for (size_t k = 0; k < char_length; ++k) {
                    if (index2 + k < buffer.size()) {
                        matrix[i][j * 4 + k] = buffer[index2 + k];
                    }
                }
                index2 += char_length; // Переход к следующему символу
            }
        }
    }

    std::string decrypted_message;

    // Извлекаем сообщения из матрицы по строкам
    for (size_t i = 0; i < rows; ++i) {
        for (size_t j = 0; j < cols; ++j) {
            for (size_t k = 0; k < 4; ++k) {
                char current_char = matrix[i][j * 4 + k];

                // Включаем* — считываем пробелы в декодированный текст
                if (current_char == '*') {
                    decrypted_message += ' '; // Заменяем '*' на пробел
                } else if (current_char != ' ' && current_char != 0) {
                    decrypted_message += current_char; // Добавляем текущий символ
                }
            }
        }
    }


    return decrypted_message;
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

        std::string received_message(buffer, bytes_received);

        // Проверяем, является ли сообщение именем клиента
        if (received_message == name) {
            // Игнорируем имя, если пришло еще раз
            continue;
        }

        if (received_message.find(delimiter) != std::string::npos) {
            // Извлекаем имя отправителя и зашифрованное сообщение
            std::string sender_name = received_message.substr(0, received_message.find(delimiter));
            std::string enc_message = received_message.substr(received_message.find(delimiter) + 1);
            std::string decrypted_message = decrypt_message(enc_message, encryption_key);

            // Убедитесь, что имя отправителя не совпадает с именем клиента
            if (sender_name != name && !decrypted_message.empty()) {
                // Здесь измените вывод, чтобы не включать имя отправителя дважды.
                std::cout << sender_name << ": " << decrypted_message << std::endl;
            }
        }
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

        // Конструируем сообщение, включая имя отправителя
        std::string full_message = name + delimiter + encrypt_message(message, encryption_key);
        // Отправляем сообщение на сервер
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
    std::cout << buffer; // Выводим запрос на экран

    std::getline(std::cin, name); // Ввод имени клиента
    // Отправляем имя без шифрования
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