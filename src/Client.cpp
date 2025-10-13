#include "Client.hpp"

Client::Client(int fd_)
    : fd(fd_), _authenticated(false), _hasPass(false), _hasNick(false), _hasUser(false),
      _username(""), _nickname(""), _hostname(""), recv_buffer(""), send_buffer("") {}

int Client::getFd() const
{
    return fd;
}

const std::string& Client::getUser() const
{
    return _username;
}

const std::string& Client::getNick() const
{
    return _nickname;
}

bool Client::isAuthenticated() const
{
    return _authenticated;
}

void Client::setUser(const std::string& user)
{
    _username = user;
    _hasUser = true;
}

void Client::setNick(const std::string& nick)
{
    _nickname = nick;
    _hasNick = true;
}

void Client::setAuthenticated(bool auth)
{
    _authenticated = auth;
}

std::string& Client::getBuffer()
{
    return recv_buffer;
}
Client::~Client() {}

void Client::setPass(bool val)
{
    _hasPass = val;
}

void Client::setHasNick(bool val)
{
    _hasNick = val;
}

void Client::setHasUser(bool val)
{
    _hasUser = val;
}

bool Client::hasPass() const
{
    return _hasPass;
}

bool Client::hasNick() const
{
    return _hasNick;
}

bool Client::hasUser() const
{
    return _hasUser;
}
