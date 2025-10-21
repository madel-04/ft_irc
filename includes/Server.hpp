#ifndef SERVER_HPP
#define SERVER_HPP

#define RESET       "\033[0m"

// ===== BRIGHT FOREGROUND COLORS =====
#define BRIGHT_RED      "\033[91m"
#define BRIGHT_GREEN    "\033[92m"
#define BRIGHT_YELLOW   "\033[93m"
#define BRIGHT_BLUE     "\033[94m"
#define BRIGHT_MAGENTA  "\033[95m"
#define BRIGHT_CYAN     "\033[96m"
#define BRIGHT_WHITE    "\033[97m"

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
    bool _running;

public:
    Server(int port, const std::string &password);
    ~Server();

    void run();
    void stop();

private:
    void setupListener(int port);
    void acceptNewConnection();
    void handleClientRead(int index);
    void closeClient(int index);
	void sendWelcomeMessage(int client_fd);
	bool authMiddleware(Client &client, const std::string &command, std::istringstream &iss);
	bool handleJoin(Client &client, 	std::istringstream &iss);
	bool handleKick(Client &client, std::istringstream &iss);
	bool handleInvite(Client &client, std::istringstream &iss);
	void handlePriv(Client &client, std::istringstream &iss);
	// Gracefully disconnect a client by file descriptor (remove from poll and maps)
	void disconnectClientFd(int fd);
	// Notify all channels where client is present about nick change
	void notifyNickChange(int client_fd, const std::string &oldNick, const std::string &newNick, const std::string &username);

	bool channelExist(std::string channelName, Client &client);
	bool clientExist(int target_fd, Client &client);
	bool hasPermissions(int client_fd, Client &client, Channel &channel);
	bool channelHasNick(std::string target, std::string channelName, Client &client);

    void handleCommand(Client &client, const std::string &line);
	int getFdByNick(const std::string &nick);

};

#endif