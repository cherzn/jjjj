#ifndef SERVER_H
#define SERVER_H

#include <iostream>
#include <cstring>
#include <vector>
#include <thread>
#include <mutex>
#include <netinet/in.h> // Для sockaddr_in

#define PORT 8080
#define BUFFER_SIZE 1024

class Server {
public:
    Server();
    void start();

private:
    void handle_client(int client_sock);
    void broadcast_message(const char* message, size_t length);

    int server_fd;
    struct sockaddr_in address;
    std::vector<int> clients;
    std::mutex clients_mutex;
};

#endif
