#ifndef CLIENT_HPP
#define CLIENT_HPP

#include <string>

struct Client
{
    int fd;
    std::string recv_buffer;
    std::string send_buffer;
    Client(int fd_ = -1) : fd(fd_), recv_buffer(), send_buffer() {}
};

#endif