#include "Channel.hpp"
#include <algorithm>
#include <sys/socket.h>

Channel::Channel() : _name(""), _userLimit(0), _topic("") {}

Channel::Channel(const std::string &name) : _name(name),  _userLimit(0), _topic(""), _inviteOnly(false) {}

const std::string &Channel::getName() const { return _name; }
const std::string &Channel::getTopic() const { return _topic; }	
const std::vector<struct Member> &Channel::getMembers() const { return _members; }

bool Channel::hasMember(int client_fd) const
{
	for (size_t i = 0; i < _members.size(); ++i)
    {
        if (_members[i].fd == client_fd)
            return true;
    }
    return false; // no está en el canal
}

bool Channel::hasMemberNick(std::string name)
{
	for (size_t i = 0; i < _members.size(); ++i)
    {
        if (_members[i].nick == name)
            return true;
    }
    return false; // no está en el canal
}

void Channel::addMember(int client_fd, std::string nick, bool op)
{
	if (!hasMember(client_fd))
    {
		Member newMember = {client_fd, op, nick};
        if (_members.empty())
		{
			this->_whiteList.push_back(client_fd);
            newMember.isOperator = true; // el primer miembro es operador
		}
        _members.push_back(newMember);
    }
}

bool Channel::isOperator(int target_fd)
{
	for (size_t i = 0; i < _members.size(); ++i)
	{
		if (_members[i].fd == target_fd && _members[i].isOperator)
			return true;
	}
	return false;
}

void Channel::removeMember(int target_fd, int requester_fd)
{
	if (!isOperator(requester_fd))
        return;
	for (size_t i = 0; i < _members.size(); ++i)
    {
        if (_members[i].fd == target_fd)
        {
            _members.erase(_members.begin() + i);
            break;
        }
    }
}

void Channel::broadcast(const std::string &message, int sender_fd)
{
    for (size_t i = 0; i < _members.size(); ++i)
    {
        int member_fd = _members[i].fd;
        if (sender_fd != -1 && member_fd == sender_fd)
            continue; // don't echo back to sender
        send(member_fd, message.c_str(), message.size(), 0);
    }
}

void Channel::addToWhiteList(int target_fd)
{
	_whiteList.push_back(target_fd);
}

bool Channel::isInWhiteList(int fd)
{
    for (size_t i = 0; i < _whiteList.size(); ++i) {
        if (_whiteList[i] == fd)
            return true;
    }
    return false;
}

int Channel::getCurrentUsers()
{
	return _members.size();
}

int Channel::getUserLimit()
{
	return _userLimit;
}

int Channel::getFdByNick(std::string nick)
{
	for (size_t i = 0; i < _members.size(); ++i)
    {
        if (_members[i].nick == nick)
        {
            return _members[i].fd;
        }
    }
	return 0;
}
std::string Channel::getKey()
{
	return _key;
}

void Channel::setKey(std::string key)
{
	this->_key = key;
}

bool Channel::getInviteOnly()
{
	return _inviteOnly;
}

bool Channel::isInWhiteList(int client_fd)
{
	for (size_t i; i < _whiteList.size(); i++)
	{
		if (_whiteList[i] == client_fd)
			return true;
	}
	return false;
}