# include <iostream>
# include <vector>
# include <string>
# include <algorithm>
# include <cctype>
# include <csignal>

#include "Server.hpp"

static volatile sig_atomic_t g_shutdown = 0;

extern "C" void signalHandler(int signum)
{
	(void)signum;
	g_shutdown = 1;
}

extern volatile sig_atomic_t* getShutdownFlag()
{
	return &g_shutdown;
}

int main(int argc, char* argv[]) 
{
	std::signal(SIGINT, signalHandler);   // Ctrl+C

    if (argc != 3)
    {
        std::cerr << "Usage: " << argv[0] << "  ./ircserv <port> <password>\n";
        return 1;
    }

    int port = std::atoi(argv[1]);
    if (port < 1024 || port > 65535)
    {
        std::cerr << "Error: Port must be between 1024 and 65535.\n";
        return 1;
    }
    std::string password = argv[2];
    Server server(port, password);
    server.run();
    return 0;
}

/*

Aunque el subject no pone un rango explícito, hay reglas estándar de red que aplican:

Rango               Nombre	                            ¿Usar en ft_irc?	Explicación
0–1023	            Puertos reservados (privilegiados)	❌ No	           Requieren permisos de root (ej: 80, 22, 443)
1024–49151	        Puertos registrados (uso normal)	✅ Sí	           Perfecto para tu servidor IRC
49152–65535	        Puertos efímeros (temporales)	    ⚠️ Mejor no	        Los usa el sistema para conexiones salientes


*/