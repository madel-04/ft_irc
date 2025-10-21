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
#include <csignal>

extern volatile sig_atomic_t* getShutdownFlag();

// Helper: set non-blocking
/*static bool setNonBlocking(int fd)
{
	// setea el socket como no bloqueante (mirar mejor)
    int flags = fcntl(fd, F_GETFL, 0);  //! ????
    if (flags == -1) return false;
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) return false;
    return true;
}*/

static bool setNonBlocking(int fd)
{
    if (fcntl(fd, F_SETFL, O_NONBLOCK) == -1)
        return false;
    return true;
}

Server::Server(int port, const std::string &password) : _listen_fd(-1), _password(password), _running(false)
{
    setupListener(port);
}

Server::~Server()
{
    // Close all client connections
    for (std::map<int, Client>::iterator it = _clients.begin(); it != _clients.end(); ++it)
    {
        close(it->first);
    }
    _clients.clear();
    
    // Close listening socket
    if (_listen_fd != -1)
    {
        close(_listen_fd);
        _listen_fd = -1;
    }
    
    // Clear channels
    _channels.clear();
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
    _running = true;
    volatile sig_atomic_t* shutdown = getShutdownFlag();
    
    while (_running && !(*shutdown))
    {
        int timeout = 1000; // ms, puedes ajustar
		// poll espera actividad en uno o varios fds
        int poll_ret = poll(&_pfds[0], _pfds.size(), timeout);
		// esto va a añadir información a la estructura de cada fd del vector _pfds, ejemplo: en _pfds[0].enevts nosotros le decimos que evento queremos vigilar
		// _pfds[0].revents indica que eventos REALMENTE ocurrieron
        if (poll_ret < 0)
        {
			// cuando poll falla, pone el numero de error en la variable global "errno"
			// EINTR es una variable que indica que poll se interrumpió por una señal, en este caso no necesariamente queremos se rompa el bucle,
            if (errno == EINTR)
            {
                if (*shutdown) break;
                continue;
            }
            std::cerr << "poll() failed: " << strerror(errno) << std::endl;
            break;
        }
        if (poll_ret == 0)
        {
            // timeout, loop otra vez
            continue;
        }
        // guardo el size de _pfds paor si hay una nueva conexion durante la ejecución del bucle y este tamaño cambia no de error, comprobamos los eventos
        size_t pfds_size = _pfds.size();
        for (size_t i = 0; i < pfds_size; ++i)
        {
			// hacemos una referencia a _pfds[i], si cambiamos esta, cambia el original
            struct pollfd &p = _pfds[i];
            if (p.revents == 0) continue;// consultar REVENTS.txt
            if (p.fd == _listen_fd && (p.revents & POLLIN)) // el socket del servidor escucha nuevas conexines
            {
				//cuando comparamos con & estamos comparando los bits
				// por eemplo si revents es 0x011, es true, porque POLLIN es 0x001 y ese bit coincide
                // nueva conexión entrante creamos nuevo socket
                acceptNewConnection();
            }  
            else // se miran los demas sockets
            {
                if (p.revents & (POLLIN | POLLERR | POLLHUP))
                {
                    handleClientRead(i);
                }
                // para ahora no manejamos POLLOUT (salida) — se añadirá más tarde
            }
        }
    }
    
    std::cout << "\nBye...\n";
}

void Server::sendWelcomeMessage(int client_fd)
{
    std::string welcome =
        ":server NOTICE * :Welcome to ft_irc!\r\n"
        ":server NOTICE * :To register, use the following commands:\r\n"
        ":server NOTICE * :PASS <password>\r\n"
        ":server NOTICE * :NICK <nickname>\r\n"
        ":server NOTICE * :USER <username> <hostname> <servername> <realname>\r\n"
        ":server NOTICE * :After that, you can use HELP to see all the commands\r\n";

    send(client_fd, welcome.c_str(), welcome.size(), 0);
}

void Server::acceptNewConnection()
{
    struct sockaddr_in clientaddr;
    socklen_t addrlen = sizeof(clientaddr);
    int client_fd = accept(_listen_fd, (struct sockaddr *)&clientaddr, &addrlen);
	//accept() toma la conexión pendiente en _listen_fd (el socket del servidor).
	// Devuelve un nuevo descriptor de socket (client_fd) para comunicarte con ese cliente.

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
    _pfds.push_back(pfd); // poll vigila este cliente en el siugiente ciclo

    _clients.insert(std::make_pair(client_fd, Client(client_fd)));
	// con insert evitas sobreescrivir un cliente que ya existiera con esa clave (su fd) si ya existe no hace nada, si quisieramos sobreescribir hariamos lo tipico de _clients[client_fd] = Client(client_fd)
    char ipstr[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &clientaddr.sin_addr, ipstr, sizeof(ipstr));
	// inet_ntop() → convierte la IP del cliente de binario a texto (ej. "192.168.1.5").
	// ntohs() → convierte el puerto de byte order de red a byte order del host.
    std::cout << "Accepted connection from " << ipstr << ":" << ntohs(clientaddr.sin_port)
              << " (fd=" << client_fd << ")\n";
	sendWelcomeMessage(client_fd);

}

void Server::handleClientRead(int index)
{
    int fd = _pfds[index].fd;
    char buf[4096]; 
    ssize_t n = recv(fd, buf, sizeof(buf), 0); // va a leer 4096 bytes del fd
    if (n <= 0) // si devuelve 0 bytes es que cliente cerró conexion, si es menor que 0 es error
    {
        closeClient(index);
        return;
    }
    Client &client = _clients[fd]; //hacemos referincia al cliente que toca
    client.getBuffer().append(buf, n); 
	// funciona como gnl, pegamos n bytes en el buffer
    std::string &buffer = client.getBuffer();
    size_t pos;
    while ((pos = buffer.find("\r\n")) != std::string::npos)
    {
		// buscamos si dentro de lo que hemos pegado hay un salto de linea
        std::string line = buffer.substr(0, pos); // extraemos hasta el salto de linea
        buffer.erase(0, pos + 2); // borramos lo que hemos extraido del buffer para que el resto quede para la siugiente iteracion
        handleCommand(client, line);
		// \r\n es basicamente lo mismo que \n pero en protocolos de red se hace asi no se por que y hay que manejarlo asi por la cara la r es que pones el cursor al principio de la linea
    }
}

bool Server::channelExist(std::string channelName, Client &client)
{
	std::map<std::string, Channel>::iterator it = _channels.find(channelName);
	if (it == _channels.end())
	{
		std::string err = "403 " + client.getNick() + " " + channelName + " :No such channel\r\n";
		send(client.getFd(), err.c_str(), err.size(), 0);
		return false;
	}
	return true;
}

 int Server::getFdByNick(const std::string &nick)
{
    for (std::map<int, Client>::iterator it = _clients.begin(); it != _clients.end(); ++it)
    {
        if (it->second.getNick() == nick)
            return it->first;
    }
    return -1;
}

bool Server::clientExist(int target_fd, Client &client)
{
    if (target_fd < 0)
    {
        std::string err = "401 " + client.getNick() + " :No such user\r\n";
        send(client.getFd(), err.c_str(), err.size(), 0);
        return false;
    }
    std::map<int, Client>::iterator it = _clients.find(target_fd);
    if (it == _clients.end())
    {
        std::string err = "401 " + client.getNick() + " :No such user\r\n";
        send(client.getFd(), err.c_str(), err.size(), 0);
        return false;
    }
    return true;
}

bool Server::hasPermissions(int client_fd, Client &client, Channel &channel)
{
    if (!channel.hasMember(client_fd) || !channel.isOperator(client_fd)) 
    {
        std::string err = "482 " + client.getNick() + " " + channel.getName() + " :You have no permissions\r\n";
        send(client.getFd(), err.c_str(), err.size(), 0);
        return false;
    }
    return true;
}

bool Server::channelHasNick(std::string target, std::string channelName, Client &client)
{
	Channel &channel = _channels[channelName];
	if (!channel.hasMemberNick(target)) 
	{
		std::string err = "441 " + client.getNick() + " " + target + " " + channelName + " :They aren't on that channel\r\n";
		send(client.getFd(), err.c_str(), err.size(), 0);
		return false;
	}
	return true;
}

void helpCommand(Client &client)
{
	std::string help =
	":server NOTICE * :PASS - set the password\r\n"
	":server NOTICE * :USER <username> <hostname> <servername> <realname>- set the user\r\n"
	":server NOTICE * :NICK - set the nickname\r\n"
	":server NOTICE * :QUIT - stop execution\r\n"
	":server NOTICE * :PRIVMSG <target nickname> <message> — Send a private message to a user\r\n"
	":server NOTICE * :JOIN <channel name> — Join a channel, If the channel does not exist, it will be created automatically\r\n"
	":server NOTICE * :KICK <channel> <nickname> - Remove user from channel\r\n"
	":server NOTICE * :INVITE <nickname> <channel> - Invite user from channel'\' add to whitelist\r\n"
	":server NOTICE * :PRIVMSG <nickname> <message> - Send a private message to another user\r\n";
	send(client.getFd(), help.c_str(), help.size(), 0);
}

bool Server::handleJoin(Client &client, std::istringstream &iss)
{
    std::string channelListStr, passwordListStr;

    iss >> channelListStr;
	iss >> passwordListStr;

   if (channelListStr.empty())
	{
		std::string err = ":server 461 " + client.getNick() + " JOIN :Not enough parameters\r\n";
		send(client.getFd(), err.c_str(), err.size(), 0);
		return false;
	}
	std::vector<std::string> channels;
	std::stringstream ssChannels(channelListStr);
	std::string temp;
	while (std::getline(ssChannels, temp, ','))
	{
		channels.push_back(temp);
	}
	std::vector<std::string> passwords;
	if (!passwordListStr.empty())
	{
		std::stringstream ssPasswords(passwordListStr);
		while (std::getline(ssPasswords, temp, ','))
		{
			passwords.push_back(temp);
		}
	}
	for (size_t i = 0; i < channels.size(); ++i)
	{
		const std::string channelName = channels[i];
		const std::string key = (i < passwords.size()) ? passwords[i] : "";

		if (channelName.empty() || channelName[0] != '#' )
		{
			std::string err = ":server 403 " + client.getNick() + " " + channelName + " :Invalid channel name\r\n";
			send(client.getFd(), err.c_str(), err.size(), 0);
			continue;
		}
		bool newlyCreated = false;
		if (_channels.find(channelName) == _channels.end())
		{
			_channels.insert(std::make_pair(channelName, Channel(channelName)));
			newlyCreated = true;
			std::cout << "Created new channel: " << channelName << "\n";
		}
		Channel &channel = _channels[channelName];
		if (newlyCreated && !key.empty())
		{
			channel.setKey(key);
		}
		if (channel.hasMember(client.getFd()))
		{
			std::string msg = ":server 443 " + client.getNick() + " " + channelName + " :is already on channel\r\n";
			send(client.getFd(), msg.c_str(), msg.size(), 0);
			continue;
		}
		if (!channel.getKey().empty() && channel.getKey() != key)
		{
			std::string err = ":server 475 " + client.getNick() + " " + channelName + " :Cannot join channel (+k) - wrong key\r\n";
			send(client.getFd(), err.c_str(), err.size(), 0);
			continue;
		}
		if (channel.getUserLimit() != 0 && channel.getCurrentUsers() >= channel.getUserLimit())
		{
			std::string err = ":server 471 " + client.getNick() + " " + channelName + " :Cannot join channel (+l) - channel is full\r\n";
			send(client.getFd(), err.c_str(), err.size(), 0);
			continue;
		}

		if (channel.getInviteOnly() && !channel.isInWhiteList(client.getFd()))
		{
			std::string err = ":server 473 " + client.getNick() + " " + channel.getName() + " :Cannot join channel (+i) - you must be invited\r\n";
			send(client.getFd(), err.c_str(), err.size(), 0);
			continue;
		}
		channel.addMember(client.getFd(), client.getNick(), false);
		std::string joinMsg = ":" + client.getNick() + " JOIN " + channelName + "\r\n";
		send(client.getFd(), joinMsg.c_str(), joinMsg.size(), 0);
		channel.broadcast(joinMsg, client.getFd());
		std::string welcome = ":server NOTICE " + client.getNick() + " :Welcome to " + channel.getName() + "\r\n";
		send(client.getFd(), welcome.c_str(), welcome.size(), 0);

		std::string topicMsg;
		if (channel.getTopic().empty())
			topicMsg = ":server 331 " + client.getNick() + " " + channel.getName() + " :No topic is set\r\n";
		else
			topicMsg = ":server 332 " + client.getNick() + " " + channel.getName() + " :" + channel.getTopic() + "\r\n";
		send(client.getFd(), topicMsg.c_str(), topicMsg.size(), 0);

		size_t currentUsers = channel.getCurrentUsers();
		std::string limitStr;
		if (channel.getUserLimit() == 0)
			limitStr = "unlimited";
		else {
			std::stringstream ss;
			ss << channel.getUserLimit();
			limitStr = ss.str();
		}
		std::stringstream ss2;
		ss2 << ":server NOTICE " << client.getNick() << " :Users: " << currentUsers << "/" << limitStr << "\r\n";
		std::string userInfo = ss2.str();
		send(client.getFd(), userInfo.c_str(), userInfo.size(), 0);

		if (channel.isOperator(client.getFd()))
		{
			std::string opMsg = ":server NOTICE " + client.getNick() + " :You have channel operator privileges\r\n";
			send(client.getFd(), opMsg.c_str(), opMsg.size(), 0);
		}
		std::cout << client.getNick() << " joined " << channelName << "\n";
	}
	return true;
}


bool Server::handleKick(Client &client, std::istringstream &iss)
{
    std::string channelName, target, reason;
    iss >> channelName >> target;

    if (channelName.empty() || target.empty())
    {
        std::string err = "461 KICK :Not enough parameters\r\n";
        send(client.getFd(), err.c_str(), err.size(), 0);
        return false;
    }
    if (!channelExist(channelName, client)) return false;
    Channel &channel = _channels[channelName];
    if (!channelHasNick(target, channelName, client)) return false;
    int target_fd = channel.getFdByNick(target);
    if (!clientExist(target_fd, client)) return false;
    if (!hasPermissions(client.getFd(), client, channel)) return false;
    channel.removeMember(target_fd, client.getFd());
    std::string fullMsg = ":" + client.getNick() + "!" + client.getUser() + "@localhost "
                        "KICK " + channelName + " " + target + "\r\n";
    channel.broadcast(fullMsg, 0);
    send(target_fd, fullMsg.c_str(), fullMsg.size(), 0);

    return true;
}

bool Server::handleInvite(Client &client, std::istringstream &iss)
{
	std::string target, channelName;
		iss  >> target >> channelName;
		if (target.empty() || channelName.empty())
		{
			std::string err = "461 INVITE :Not enough parameters\r\n";
			send(client.getFd(), err.c_str(), err.size(), 0);
			return false;
		}
		if (!channelExist(channelName, client)) return false;
		Channel &channel = _channels[channelName];
		if (!hasPermissions(client.getFd(), client, channel)) return false;
		int target_fd = getFdByNick(target);
		if (!clientExist(target_fd, client)) return false;
		if (channel.hasMember(target_fd))
		{
			std::string errMsg = ":server 443 " + client.getNick() + " " + target + " " + channelName + " :is already on channel\r\n";
			send(client.getFd(), errMsg.c_str(), errMsg.size(), 0);
			return false;
		}
		if (!channel.isInWhiteList(target_fd))
		{
			channel.addToWhiteList(target_fd);
			std::string inviteMsg = ":" + client.getNick() + "!" + client.getUser() + "@localhost "
							"INVITE " + target + " :" + channelName + "\r\n";
			send(target_fd, inviteMsg.c_str(), inviteMsg.size(), 0);
		}
		else
		{
			std::string errMsg = ":server 443 " + client.getNick() + " " + target + " " + channelName + " :is already invited\r\n";
			send(client.getFd(), errMsg.c_str(), errMsg.size(), 0);
		}
		return true;
}

void Server::handlePriv(Client &client, std::istringstream &iss)
{
	std::string target;
	iss >> target;
	if (target.empty())
	{
		send(client.getFd(), "411 :No recipient given (PRIVMSG)\r\n", strlen("411 :No recipient given (PRIVMSG)\r\n"), 0);
		return;
	}

	std::string message;
	std::getline(iss, message);
	size_t start = message.find_first_not_of(' ');
	if (start != std::string::npos)
		message = message.substr(start);
	else
		message.clear();

	if (message.empty())
	{
		send(client.getFd(), "412 :No text to send\r\n", strlen("412 :No text to send\r\n"), 0);
		return;
	}
	if (message[0] != ':')
	{
		std::string err = ":server 461 " + client.getNick() +
						" PRIVMSG :Invalid syntax, use ':' before message\r\n";
		send(client.getFd(), err.c_str(), err.size(), 0);
		return;
	}
	message = message.substr(1);
	std::string fullMsg = ":" + client.getNick() + " PRIVMSG " + target + ":" + message + "\r\n";
	if (target[0] == '#')
	{
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



void Server::handleCommand(Client &client, const std::string &line)
{
    std::istringstream iss(line);
    std::string command;
    int client_fd = client.getFd();
    iss >> command;

    if (command == "QUIT")
    {
		send(client_fd, "221 :Goodbye\r\n", 14, 0);
		// Ensure we properly remove from poll list and clients map
		disconnectClientFd(client_fd);
		return;
    }
    if (command == "HELP")
    {
        helpCommand(client);
        return;
    }
    if (!authMiddleware(client, command, iss))
        return;
    if (command == "JOIN")
    {
        handleJoin(client, iss);
        return;
    }
    else if (command == "KICK")
    {
        handleKick(client, iss);
        return;
    }
	else if (command == "INVITE")
	{
		handleInvite(client, iss);
		return;
	}
    else if (command == "PRIVMSG")
    {
		handlePriv(client, iss);
		return;
	}
    else
        send(client.getFd(), "421 :Unknown command\r\n", 23, 0);
}


void Server::closeClient(int index)
{
    int fd = _pfds[index].fd;
	// If we still have the client object, use its nick to broadcast part messages
	std::string nick;
	std::string user;
	std::map<int, Client>::iterator itc = _clients.find(fd);
	if (itc != _clients.end())
	{
		nick = itc->second.getNick();
		user = itc->second.getUser();
	}

	// Remove from all channels and notify
	for (std::map<std::string, Channel>::iterator it = _channels.begin(); it != _channels.end(); ++it)
	{
		Channel &ch = it->second;
		if (ch.hasMember(fd))
		{
			std::string partMsg;
			if (!nick.empty())
				partMsg = ":" + nick + "!" + user + "@localhost PART " + ch.getName() + "\r\n";
			else
				partMsg = ":server NOTICE * :Client left channel " + ch.getName() + "\r\n";
			ch.broadcast(partMsg, fd);
			ch.removeMemberByFd(fd);
		}
	}

    close(fd);
    _clients.erase(fd);

    // remover pfd del vector
    _pfds.erase(_pfds.begin() + index);
    
    std::cout << "Client fd=" << fd << " disconnected\n";
}

void Server::stop()
{
    _running = false;
}

// Remove a client given its fd: close socket, erase from pollfds and clients map
void Server::disconnectClientFd(int fd)
{
	// Find the pollfd index for this fd
	for (size_t i = 0; i < _pfds.size(); ++i)
	{
		if (_pfds[i].fd == fd)
		{
			closeClient(static_cast<int>(i));
			return;
		}
	}
	// If not in poll list (edge case), just close and erase
	close(fd);
	_clients.erase(fd);
}

/*
Antes solo se hacia el close client.
Ahora cierra el socket, elimina el cliente del mapa y quita el pollfd del vector
Antes se quedaban ek pollfd del cliente como zombie y la entrada dek cliente seguia en _clients
Lo que pasaba es que tenias entradas duplicadas y estado obsoleto al volver a intentar reconectar

! SE ELIMINAN TAMBIÉN AL USUARIO DE TODOS LOS CANALES??
*/

// Notify all channels where client is present about nick change
void Server::notifyNickChange(int client_fd, const std::string &oldNick, const std::string &newNick, const std::string &username)
{
	// Standard IRC NICK change format: :oldnick!user@host NICK :newnick
	std::string nickMsg = ":" + oldNick + "!" + username + "@localhost NICK :" + newNick + "\r\n";
	
	// Broadcast to all channels where this user is a member
	for (std::map<std::string, Channel>::iterator it = _channels.begin(); it != _channels.end(); ++it)
	{
		Channel &ch = it->second;
		if (ch.hasMember(client_fd))
		{
			// Broadcast to everyone in the channel (including the user who changed nick)
			ch.broadcast(nickMsg, -1);
			// Update the nickname in the channel's member list
			ch.updateMemberNick(client_fd, newNick);
		}
	}
}