#include "win-pipe.h"

#include <array>
#include <chrono>
#include <iostream>

void run_receiver();
void receiver_callback1(uint8_t* data, size_t size);
void receiver_callback2(uint8_t* data, size_t size);
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
    std::cout << "Type callback to change behavior. Type exit to quit." << std::endl;
    
    int current_callback = 0;
    win_pipe::callback_t callbacks[2] { receiver_callback1, receiver_callback2 };

    std::unordered_map<int, win_pipe::receiver> map;
    map[0] = win_pipe::receiver { "win-pipe_example", callbacks[current_callback] };

    std::string op;
    while (true) {
        std::getline(std::cin, op);
        if (op == "callback") {
            current_callback++;
            map[0].set_callback(callbacks[current_callback % 2]);
        }
        else if (op == "exit")
            break;
    }
}

void receiver_callback1(uint8_t* data, [[maybe_unused]] size_t size)
{
    using namespace std::chrono;

    static int count = 0;
    int oldCount = count;
    count++;

    auto end = high_resolution_clock::now();

    if (oldCount % 2 == 0)
    {
        auto* start = reinterpret_cast<decltype(end)*>(data);
        auto latency = duration_cast<nanoseconds>(end - *start);
        std::cout << "latency: " << latency.count() << "\n";
    }
    else
    {
        auto* message = reinterpret_cast<const char*>(data);
        std::cout << message << std::endl;
    }
}

void receiver_callback2([[maybe_unused]] uint8_t* data, size_t size)
{
    using namespace std::chrono;

    static int count = 0;
    int oldCount = count;
    count++;

    auto end = high_resolution_clock::now();

    if (oldCount % 2 == 0)
    {
        auto* start = reinterpret_cast<decltype(end)*>(data);
        auto latency = duration_cast<nanoseconds>(end - *start);
        std::cout << "latency: " << latency.count() << "\n";
    }
    else
    {
        std::cout << "received a message " << size << " bytes long!\n";
    }
}

void run_sender()
{
    using namespace std::chrono;

    std::cout << "Send messages to the receiver! Type exit to quit." << std::endl;

    auto sender = win_pipe::sender { "win-pipe_example" };

    std::string message;
    while (true) {
        std::getline(std::cin, message);
        auto start = high_resolution_clock::now();

        sender.send(&start, sizeof(decltype(start)));
        sender.send(message.c_str(), (DWORD)message.length() + 1);

        if (message == "exit")
            break;
    }
}
