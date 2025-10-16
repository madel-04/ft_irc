#ifndef SERVER_HPP
#define SERVER_HPP

#include <string>
#include <vector>
#include <map>
#include "Client.hpp"
#include <stdlib.h>
#include "Channel.hpp"

class Server
{
private:
    int _listen_fd;
    std::string _password;
    std::vector<int> _ports;
    std::vector<struct pollfd> _pfds;
    std::map<int, Client> _clients;  // <fd, Client>
    std::map<std::string, Channel> _channels; // <channel_name, Channel>

public:
    Server(int port, const std::string &password);
    ~Server();

    void run();

private:
    void setupListener(int port);
    void acceptNewConnection();
    void handleClientRead(int index);
    void closeClient(int index);

    void handleCommand(Client &client, const std::string &line);
};

#endif