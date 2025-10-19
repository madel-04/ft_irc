#include "Server.hpp"
#include "Client.hpp"
#include <iostream>
#include <string>
#include <sstream>
#include <sys/socket.h>

bool isValidNick(const std::string &nick) {
    if (std::isdigit(nick[0]))
        return false;
    for (size_t i = 0; i < nick.size(); ++i) {
        char c = nick[i];
        if (!std::isalnum(c) && std::string("[]\\`_^{|}-").find(c) == std::string::npos)
            return false;
    }
    return true;
}

bool Server::authMiddleware(Client &client, const std::string &command, std::istringstream &iss)
{
	int	client_fd = client.getFd();
	std::string msg;

	if (!client.hasPass() && command != "PASS")
	{
		msg = "461 PASS :You need to be authenticated\r\n";
		send(client_fd, msg.c_str(), msg.size(), 0);
		return false;
	}
	if (command == "PASS")
	{
		std::string pass;
		iss >> pass;

		if (pass.empty())
		{
			msg = "461 PASS :Not enough parameters\r\n";
			send(client_fd, msg.c_str(), msg.size(), 0);
			return false;
		}
		if (client.hasPass())
		{
			msg = "462 PASS :You may not reregister\r\n";
			send(client_fd, msg.c_str(), msg.size(), 0);
			return false;
		}
		if (pass == _password)
		{
			client.setPass(true);
			msg = ":server NOTICE * :Password accepted\r\n";
			send(client_fd, msg.c_str(), msg.size(), 0);
		}
		else
		{
			msg = "464 PASS :Password incorrect\r\n";
			send(client_fd, msg.c_str(), msg.size(), 0);
		}
		return false;
	}
	if (command == "NICK")
	{
		std::string nick;
		iss >> nick;
		if (nick.empty())
		{
			msg = "431 NICK :No nickname given\r\n";
			send(client_fd, msg.c_str(), msg.size(), 0);
			return false;
		}
		if (this->getFdByNick(nick) != -1)
		{
			std::string msg = ":server 433 * " + nick + " :Nickname is already in use\r\n";
			send(client.getFd(), msg.c_str(), msg.size(), 0);
			return false;
		}
		if (!isValidNick(nick))
		{
			msg = ":server 432 * " + nick + " :Erroneous nickname\r\n";
			send(client_fd, msg.c_str(), msg.size(), 0);
			return false;
		}
		client.setNick(nick);
		msg = ":server NOTICE * :Nickname set to " + nick + "\r\n";
		send(client_fd, msg.c_str(), msg.size(), 0);
		if (!client.isAuthenticated() && client.hasPass() && client.hasNick() && client.hasUser())
		{
			client.setAuthenticated(true);
			std::string welcome = ":server NOTICE " + client.getNick() +
				" :Welcome to ft_irc, " + client.getNick() + "!" + client.getUser() + "@localhost\r\n";
			send(client_fd, welcome.c_str(), welcome.size(), 0);
			std::cout << "Client " << client_fd << " is now authenticated as "
					  << client.getNick() << "\n";
		}
		return false;
	}
	if (command == "USER")
	{
		std::string username, hostname, servername, realname;
		iss >> username >> hostname >> servername;
		std::getline(iss, realname);

		if (!realname.empty() && realname[0] == ' ')
			realname = realname.erase(0, 1); // remove leading space
		if (!realname.empty() && realname[0] == ':')
			realname = realname.erase(0, 1); // remove leading ':'

		if (client.hasUser())
		{
			msg = ":server 462 USER :You may not reregister\r\n";
			send(client_fd, msg.c_str(), msg.size(), 0);
			return false;
		}

		if (client.isAuthenticated())
		{
			msg = ":server 462 USER :You may not reregister\r\n";
			send(client_fd, msg.c_str(), msg.size(), 0);
			return false;
		}

		if (username.empty() || hostname.empty() || servername.empty() || realname.empty())
		{
			msg = ":server 461 USER :Not enough parameters\r\n";
			send(client_fd, msg.c_str(), msg.size(), 0);
			return false;
		}
		client.setUser(username); //!
		client.setHostname(hostname);
		client.setServername(servername);
		client.setRealname(realname);
		//client.setUserSet(true); //Flag para saber que todo se ha seteado correctamente

		msg = ":server NOTICE * :Username set to " + username + "\r\n";
		send(client_fd, msg.c_str(), msg.size(), 0);
		if (!client.isAuthenticated() && client.hasPass() && client.hasNick() && client.hasUser())
		{
			client.setAuthenticated(true);
			std::string welcome = ":server NOTICE " + client.getNick() +
				" :Welcome to ft_irc, " + client.getNick() + "!" + client.getUser() + "@localhost\r\n";
			send(client_fd, welcome.c_str(), welcome.size(), 0);
			std::cout << "Client " << client_fd << " is now authenticated as "
					  << client.getNick() << "\n";
		}
		return false;
	}
	if (!client.isAuthenticated())
	{
		msg = "461 " + command + " :You are not fully authenticated, did you set your USER and NICK?\r\n";
		send(client_fd, msg.c_str(), msg.size(), 0);
		return false;
	}
	return true;
}
