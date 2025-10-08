#ifndef SERVER_HPP
#define SERVER_HPP

#include <string>
#include <vector>
#include <map>

class Server
{
private:
    std::string password;
    int port; // std::vector<int> ports; // support for multiple ports
    int server_fd;

public:
    Server();
    ~Server();
    Server(const Server& other);
    Server& operator=(const Server& other);
    
};

#endif