# win-pipe
Single-file C++ library for Windows named pipes.

Uses Windows's named pipes for inter-process communication. Pipes are inbound, meaning data is sent from client to server.

Currently does not support having multiple senders per pipe/receiver.

### TODO
1. Make multiple senders work if it doesn't already
1. Implement (or hide) move/copy constructors

