#ifndef CLIENT_H
#define CLIENT_H

#include <iostream>
#include <cstring>
#include <cstdlib>
#include <thread>
#include <arpa/inet.h> // Для inet_pton
#include <unistd.h> // Для close()

#define BUFFER_SIZE 1024

class Client {
public:
    Client(const std::string& server_ip, int port);
    void start();

private:
    void sendMessage();
    void receiveMessage();

    int client_fd;
    struct sockaddr_in address;
};

size_t utf8_length(const std::string &str);

#endif
