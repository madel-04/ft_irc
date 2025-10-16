#ifndef CHANNEL_HPP
#define CHANNEL_HPP

#include <string>
#include <vector>
#include <map>

class Channel
{
    private:
        std::string _name;
        std::string _topic;
        std::vector<int> _members; // member file descriptors

    public:
        Channel();
        Channel(const std::string &name);

        const std::string &getName() const;
        const std::string &getTopic() const;
    const std::vector<int> &getMembers() const;

    void addMember(int client_fd);
    void removeMember(int client_fd);
    bool hasMember(int client_fd) const;

    void broadcast(const std::string &message, int sender_fd = -1);
};

#endif