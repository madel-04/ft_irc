#include "Server.hpp"

#include <iostream>
#include <sstream>
#include <cstring>
#include <cerrno>
#include <vector>
#include <map>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>

// Helper: set non-blocking
static bool setNonBlocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return false;
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) return false;
    return true;
}

Server::Server(int port, const std::string &password) : _listen_fd(-1), _password(password)
{
    setupListener(port);
}

Server::~Server()
{
    if (_listen_fd != -1) close(_listen_fd);
}

void Server::setupListener(int port)
{
    _listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (_listen_fd < 0)
    {
        std::cerr << "socket() failed: " << strerror(errno) << std::endl;
        exit(1);
    }

    // permitir reuseaddr para evitar "address already in use" en reinicios
    int opt = 1;
    if (setsockopt(_listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    {
        std::cerr << "setsockopt() failed: " << strerror(errno) << std::endl;
        close(_listen_fd);
        exit(1);
    }

    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(static_cast<uint16_t>(port));

    if (bind(_listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        std::cerr << "bind() failed: " << strerror(errno) << std::endl;
        close(_listen_fd);
        exit(1);
    }

    if (!setNonBlocking(_listen_fd))
    {
        std::cerr << "failed to set listen socket non-blocking\n";
        close(_listen_fd);
        exit(1);
    }

    if (listen(_listen_fd, SOMAXCONN) < 0)
    {
        std::cerr << "listen() failed: " << strerror(errno) << std::endl;
        close(_listen_fd);
        exit(1);
    }

    // inicializamos pollfd para el socket de escucha
    struct pollfd pfd;
    pfd.fd = _listen_fd;
    pfd.events = POLLIN; // nos interesa aceptar nuevas conexiones
    pfd.revents = 0;
    _pfds.push_back(pfd);

    std::cout << "Listening on port " << port << " (fd " << _listen_fd << ")\n";
}

void Server::run()
{
    while (true)
    {
        int timeout = 1000; // ms, puedes ajustar
        int ret = poll(&_pfds[0], _pfds.size(), timeout);
        if (ret < 0)
        {
            if (errno == EINTR) continue;
            std::cerr << "poll() failed: " << strerror(errno) << std::endl;
            break;
        }
        if (ret == 0)
        {
            // timeout, loop otra vez
            continue;
        }

        // itero sobre una copia del vector de pollfd (por si modificamos _pfds)
        size_t nfds = _pfds.size();
        for (size_t i = 0; i < nfds; ++i)
        {
            struct pollfd &p = _pfds[i];
            if (p.revents == 0) continue;

            if (p.fd == _listen_fd && (p.revents & POLLIN))
            {
                // nueva conexión entrante
                acceptNewConnection();
            }
            else
            {
                if (p.revents & (POLLIN | POLLERR | POLLHUP))
                {
                    handleClientRead(i);
                }
                // para ahora no manejamos POLLOUT (salida) — se añadirá más tarde
            }
        }
    }
}

void Server::acceptNewConnection()
{
    struct sockaddr_in clientaddr;
    socklen_t addrlen = sizeof(clientaddr);
    int client_fd = accept(_listen_fd, (struct sockaddr *)&clientaddr, &addrlen);
    if (client_fd < 0)
    {
        if (errno != EWOULDBLOCK && errno != EAGAIN)
            std::cerr << "accept() failed: " << strerror(errno) << std::endl;
        return;
    }

    if (!setNonBlocking(client_fd))
    {
        std::cerr << "failed to set client non-blocking\n";
        close(client_fd);
        return;
    }

    struct pollfd pfd;
    pfd.fd = client_fd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    _pfds.push_back(pfd);

    _clients.insert(std::make_pair(client_fd, Client(client_fd)));

    char ipstr[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &clientaddr.sin_addr, ipstr, sizeof(ipstr));
    std::cout << "Accepted connection from " << ipstr << ":" << ntohs(clientaddr.sin_port)
              << " (fd=" << client_fd << ")\n";
}

void Server::handleClientRead(int index)
{
    int fd = _pfds[index].fd;
    char buf[4096];
    ssize_t n = recv(fd, buf, sizeof(buf), 0);
    if (n <= 0)
    {
        closeClient(index);
        return;
    }

    Client &client = _clients[fd];
    client.getBuffer().append(buf, n);

    std::string &buffer = client.getBuffer();
    size_t pos;
    while ((pos = buffer.find("\r\n")) != std::string::npos)
    {
        std::string line = buffer.substr(0, pos);
        buffer.erase(0, pos + 2);
        handleCommand(client, line);
    }
}

void Server::handleCommand(Client &client, const std::string &line)
{
    std::istringstream iss(line);
    std::string command;
    iss >> command;

    if (command == "PASS")
    {
        std::string pass;
        iss >> pass;
        if (pass.empty())
        {
            send(client.getFd(), "461 PASS :Not enough parameters\r\n", 34, 0);
            return;
        }
        if (client.isAuthenticated())
        {
            send(client.getFd(), "462 :You may not reregister\r\n", 29, 0);
            return;
        }
        if (pass == _password)
        {
            client.setPass(true);
            send(client.getFd(), ":server NOTICE * :Password accepted\r\n", 38, 0);
        }
        else
        {
            send(client.getFd(), "464 :Password incorrect\r\n", 25, 0);
            close(client.getFd());
        }
    }
    else if (command == "NICK")
    {
        std::string nick;
        iss >> nick;
        if (nick.empty())
        {
            send(client.getFd(), "431 :No nickname given\r\n", 24, 0);
            return;
        }
        client.setNick(nick);
        std::string msg = ":server NOTICE * :Nickname set to " + nick + "\r\n";
        send(client.getFd(), msg.c_str(), msg.size(), 0);
    }
    else if (command == "USER")
    {
        std::string username;
        iss >> username;
        if (username.empty())
        {
            send(client.getFd(), "461 USER :Not enough parameters\r\n", 33, 0);
            return;
        }
        client.setUser(username);
        send(client.getFd(), (":server NOTICE * :Username set to " + username + "\r\n").c_str(), username.size() + 38, 0);
    }
    else if (command == "QUIT")
    {
        send(client.getFd(), "221 :Goodbye\r\n", 14, 0);
        close(client.getFd());
        return;
    }
    else if (command == "JOIN")
    {
        std::string channelName;
        iss >> channelName;

        if (channelName.empty() || channelName[0] != '#')
        {
            send(client.getFd(), "403 :Invalid channel name\r\n", 28, 0);
            return;
        }

        // Crear canal si no existe
        if (_channels.find(channelName) == _channels.end())
        {
            _channels.insert(std::make_pair(channelName, Channel(channelName)));
            std::cout << "Created new channel: " << channelName << "\n";
        }

        Channel &channel = _channels[channelName];
        if (!channel.hasMember(client.getFd()))
        {
            channel.addMember(client.getFd());
            std::string joinMsg = ":" + client.getNick() + " JOIN " + channelName + "\r\n";
            channel.broadcast(joinMsg, client.getFd());
            std::string info = ":server 332 " + client.getNick() + " " + channelName + " :Welcome to " + channelName + "\r\n";
            send(client.getFd(), info.c_str(), info.size(), 0);
            std::cout << client.getNick() << " joined " << channelName << "\n";
        }
    }
    else if (command == "PRIVMSG")
    {
        // Read target (channel or nick)
        std::string target;
        iss >> target;
        if (target.empty())
        {
            send(client.getFd(), "411 :No recipient given (PRIVMSG)\r\n", strlen("411 :No recipient given (PRIVMSG)\r\n"), 0);
            return;
        }

        // The rest of the stream is the message; it may start with a space and then a ':'
        std::string message;
        std::getline(iss, message);
        // trim leading spaces
        size_t start = message.find_first_not_of(' ');
        if (start != std::string::npos)
            message = message.substr(start);
        else
            message.clear();

        if (message.empty() || message[0] != ':')
        {
            send(client.getFd(), "412 :No text to send\r\n", strlen("412 :No text to send\r\n"), 0);
            return;
        }

        // remove leading ':'
        message = message.substr(1);

        // Build the full IRC formatted message
        std::string fullMsg = ":" + client.getNick() + " PRIVMSG " + target + " :" + message + "\r\n";

        if (target[0] == '#')
        {
            // Message to a channel
            std::map<std::string, Channel>::iterator it = _channels.find(target);
            if (it == _channels.end())
            {
                std::string err = "403 " + target + " :No such channel\r\n";
                send(client.getFd(), err.c_str(), err.size(), 0);
                return;
            }
            Channel &chan = it->second;
            if (!chan.hasMember(client.getFd()))
            {
                std::string err = "442 " + target + " :You're not on that channel\r\n";
                send(client.getFd(), err.c_str(), err.size(), 0);
                return;
            }
            chan.broadcast(fullMsg, client.getFd());
        }
        else
        {
            // Direct message to a user
            bool found = false;
            for (std::map<int, Client>::iterator it = _clients.begin(); it != _clients.end(); ++it)
            {
                if (it->second.getNick() == target)
                {
                    send(it->second.getFd(), fullMsg.c_str(), fullMsg.size(), 0);
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                std::string err = "401 " + target + " :No such nick\r\n";
                send(client.getFd(), err.c_str(), err.size(), 0);
            }
        }
    }


    //!
    else
    {
        send(client.getFd(), "421 :Unknown command\r\n", 23, 0);
    }

    // Check if client is fully authenticated
    if (!client.isAuthenticated() && client.hasPass() && client.hasNick() && client.hasUser())
    {
        client.setAuthenticated(true);
        std::string msg = ":server 001 " + client.getNick() + " :Welcome to ft_irc, " +
                          client.getNick() + "!" + client.getUser() + "@localhost\r\n";
        send(client.getFd(), msg.c_str(), msg.size(), 0);
        std::cout << "Client " << client.getFd() << " is now authenticated as "
                  << client.getNick() << "\n";
    }
}


void Server::closeClient(int index)
{
    int fd = _pfds[index].fd;
    close(fd);
    _clients.erase(fd);

    // remover pfd del vector
    _pfds.erase(_pfds.begin() + index);
    
    std::cout << "Client fd=" << fd << " disconnected\n";
}
