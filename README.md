# win-pipe
Single-file C++ library for Windows named pipes.

Uses Windows's named pipes for inter-process communication (IPC). Latency measured roughly ~50–150µs using `example.cpp`. Senders send data through messages. Receivers automatically and asynchronously read messages and pass the data to a callback function.

## Features
### Supported
* RAII
* Variable-size data transfer

### Unsupported
* Multiple receivers per pipe
	* Receivers automatically pop data from pipes, so having multiple receivers wouldn't work
        * Pub/sub would be possible with shared memory, but this just uses Win32 API named pipes
* Multiple senders per pipe
	* Hopefully will be implemented eventually
	* For now, it's first-come-first-serve
	* Subsequent senders have to wait in line until the currently connected sender is disconnected
	* While waiting, sends are discarded
* Buffering data until a receiver connects
	* This is by design
	* Data sent with no receiver is discarded

## Examples
A slightly more complex example can be found at [example.cpp](example.cpp).

### Sender
```c++
#include "win-pipe.h"

#include <iostream>
#include <string>

int main()
{
	win_pipe::sender sender("example_pipe");
	std::string message;
	std::getline(std::cin, message);
	sender.send(message.c_str(), (DWORD)message.length() + 1);
}
```

### Receiver
```c++
#include "win-pipe.h"

#include <iostream>

void callback(uint8_t* data, size_t size);

int main()
{
	win_pipe::receiver receiver("example_pipe", callback);
	std::cin.get();
}

void callback(uint8_t* data, size_t size)
{
	const char* message = reinterpret_cast<const char*>(data);
	std::cout << message << std::endl;
}
```

## To Do
1. Make multiple senders work
1. Fix any bugs that crop up

