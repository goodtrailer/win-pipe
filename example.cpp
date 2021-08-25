#include "win-pipe.h"

#include <iostream>

void run_server();
void server_callback(uint8_t* data, size_t size);
void run_client();

int main(int argc, void** argv)
{
    if (argc < 2) {
        std::cout << "Specify server/client." << std::endl;
        return EXIT_FAILURE;
    }

    const char* arg1 = reinterpret_cast<const char*>(argv[1]);

    if (strcmp(arg1, "server") == 0)
        run_server();

    else if (strcmp(arg1, "client") == 0)
        run_client();

    else {
        std::cout << "Unrecognized arg, must be server/client." << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

void run_server()
{
    win_pipe::server server("win-pipe_test", NULL, server_callback);

    std::string op;
    while (true) {
        std::getline(std::cin, op);
        if (op == "exit")
            break;
    }
}

void server_callback(uint8_t* data, size_t size)
{
    auto* message = reinterpret_cast<const char*>(data);
    std::cout << message << std::endl;
}

void run_client()
{
    win_pipe::client client("win-pipe_test", NULL);

    std::string op;
    while (true) {
        std::getline(std::cin, op);
        if (op == "exit")
            break;

        client.write("hello world", sizeof("hello world"));
    }
}
