# win-pipe
Single-file C++ library for Windows named pipes.

Uses Windows's named pipes for inter-process communication. Senders send data through messages. Receivers automatically and asynchronously read messages and pass the data to a callback function.

## Features
### Supported
* Sending data
* Receiving data asynchronously
* Reuse of pipes
    * If a sender is closed, another sender can use the same pipe/receiver
	* A new pipe name does not have to be used
	* The above also applies to receivers
* STL container support (std::unordered_map, etc.)
	* Default constructor
	* Move constructor
* Any-size data transfer (within Windows API physical limits)

### Unsupported
* Multiple receivers per pipe
	* Receivers automatically pop data from pipes, so having multiple receivers wouldn't make any sense
* Multiple senders per pipe
	* Hopefully will be implemented eventually
	* For now, it's first-come-first-serve
	* Subsequent senders have to wait in line until the currently connected sender is disconnected
	* While waiting, sends are discarded
* Buffering data until a receiver connects
	* Data sent with no receiver on the other end is discarded by design

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
	win_pipe::receiver receiver("example_pipe", NULL, callback);
	std::cin.get();
}

void callback(uint8_t* data, size_t size)
{
	const char* message = reinterpret_cast<const char*>(data);
	std::cout << message << std::endl;
}
```

## TODO
1. Make multiple senders work
1. Fix any bugs that crop up

