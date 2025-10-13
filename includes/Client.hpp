#ifndef CLIENT_HPP
#define CLIENT_HPP

#include <string>
 #include <stdlib.h>

struct Client
{
    private:
    int fd;
    bool _authenticated;
    bool _hasPass;
    bool _hasNick;
    bool _hasUser;
    std::string _username;
    std::string _nickname;
    std::string _hostname;

    std::string recv_buffer;
    std::string send_buffer;

    public:
    Client(int fd_ = -1);
    ~Client();
    int getFd() const;
    const std::string& getUser() const;
    const std::string& getNick() const;
    bool isAuthenticated() const;

    void setUser(const std::string& user);
    void setNick(const std::string& nick);
    void setAuthenticated(bool auth);

    bool hasPass() const;
    bool hasNick() const;
    bool hasUser() const;

    void setPass(bool val);
    void setHasNick(bool val);
    void setHasUser(bool val);

    std::string& getBuffer();
};

#endif