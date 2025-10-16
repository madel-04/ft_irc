#include "Channel.hpp"
#include <algorithm>
#include <sys/socket.h>

Channel::Channel() : _name(""), _topic("") {}

Channel::Channel(const std::string &name) : _name(name), _topic("") {}

const std::string &Channel::getName() const { return _name; }
const std::string &Channel::getTopic() const { return _topic; }
const std::vector<int> &Channel::getMembers() const { return _members; }

bool Channel::hasMember(int client_fd) const
{
    return std::find(_members.begin(), _members.end(), client_fd) != _members.end();
}

void Channel::addMember(int client_fd)
{
    if (!hasMember(client_fd))
        _members.push_back(client_fd);
}

void Channel::removeMember(int client_fd)
{
    _members.erase(std::remove(_members.begin(), _members.end(), client_fd), _members.end());
}

void Channel::broadcast(const std::string &message, int sender_fd)
{
    for (size_t i = 0; i < _members.size(); ++i)
    {
        int member_fd = _members[i];
        if (sender_fd != -1 && member_fd == sender_fd)
            continue; // don't echo back to sender
        send(member_fd, message.c_str(), message.size(), 0);
    }
}
