#ifndef SERVER_HPP
#define SERVER_HPP

#include <vector>
#include <string>
#include <mutex>
#include <unordered_map>

class Server {
public:
    Server();
    ~Server();

    void start();

private:
    static const int PORT = 8080;
    static const std::string ADMIN_PASSWORD;

    int server_socket;
    std::vector<int> clients;
    std::vector<std::string> client_names;
    std::mutex clients_mutex;
    std::unordered_map<int, bool> admin_sockets;

    void broadcast(const std::string& message, int sender_socket);
    std::string decryptMessage(const std::string& encrypted, const std::string& key);
    void sendClientList(int admin_socket);
    void handleClient(int client_socket);
};

#endif // SERVER_HPP