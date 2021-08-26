#include "win-pipe.h"

#include <iostream>

void run_receiver();
void receiver_callback(uint8_t* data, size_t size);
void run_sender();

int main(int argc, void** argv)
{
    if (argc < 2) {
        std::cout << "Specify sender/receiver." << std::endl;
        return EXIT_FAILURE;
    }

    const char* arg1 = reinterpret_cast<const char*>(argv[1]);

    if (strcmp(arg1, "receiver") == 0)
        run_receiver();

    else if (strcmp(arg1, "sender") == 0)
        run_sender();

    else {
        std::cout << "Unrecognized arg, must be sender/receiver." << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

void run_receiver()
{
    std::cout << "Type exit to quit." << std::endl;

    win_pipe::receiver receiver("win-pipe_test", NULL, receiver_callback);

    std::string op;
    while (true) {
        std::getline(std::cin, op);
        if (op == "exit")
            break;
    }
}

void receiver_callback(uint8_t* data, size_t size)
{
    auto* message = reinterpret_cast<const char*>(data);
    std::cout << message << " : " << size << std::endl;
}

void run_sender()
{
    std::cout << "Send messages to the receiver! Type exit to quit." << std::endl;
    
    win_pipe::sender sender("win-pipe_test");

    std::string message;
    while (true) {
        std::getline(std::cin, message);
        if (message == "exit")
            break;

        sender.write(message.c_str(), message.length() + 1);
    }
}
