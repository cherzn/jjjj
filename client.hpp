#ifndef CLIENT_HPP
#define CLIENT_HPP

#include <atomic>
#include <string>

class Client {
    int client_socket;
    std::string name;
    std::string encryption_key;
    std::atomic<bool> running;

    void receiveMessages();
    void sendMessages();

public:
    Client();
    ~Client();

    void connectToServer(const std::string &ip);
    void run();
};

#endif