DFIOSDF


bool isValidNick(const std::string &nick) {
    if (nick.empty() || std::isdigit(nick[0]))
        return false;
    for (size_t i = 0; i < nick.size(); ++i) {
        char c = nick[i];
        if (!std::isalnum(c) && std::string("[]\\`_^{|}-").find(c) == std::string::npos)
            return false;
    }
    return true;
}


if (!isValidNick(nick)) {
    msg = ":server 432 * " + nick + " :Erroneous nickname\r\n";
    send(client_fd, msg.c_str(), msg.size(), 0);
    return false;
}
