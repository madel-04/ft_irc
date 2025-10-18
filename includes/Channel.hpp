#ifndef CHANNEL_HPP
#define CHANNEL_HPP

#include <string>
#include <vector>
#include <map>

struct Member
{
    int fd;
    bool isOperator;
	std::string nick;
};

class Channel
{
    private:
        std::string _name;
		int _userLimit;
        std::string _topic;
		bool _inviteOnly;
		std::string _key;
		bool _topicProtect;
        std::vector<struct Member> _members; // member file descriptors
		std::vector<int> _whiteList;


    public:
        Channel();
        Channel(const std::string &name);

        const std::string &getName() const;
        const std::string &getTopic() const;
    	const std::vector<struct Member> &getMembers() const;

		int	getCurrentUsers();
		int getUserLimit();
		int getFdByNick(std::string nick);
		bool getInviteOnly();
		std::string getKey();
		bool isInWhiteList(int client_fd);
		

    void addMember(int client_fd, std::string nick, bool op);
    void removeMember(int target_fd, int requester_fd);
    bool hasMember(int client_fd) const;
	bool hasMemberNick(std::string name);
	bool isOperator(int client_fd);
	void addToWhiteList(int target_fd);
	void setKey(std::string key);


    void broadcast(const std::string &message, int sender_fd = -1);
};

#endif