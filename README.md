# win-pipe
Single-file C++ library for Windows named pipes.

Uses Windows's named pipes for inter-process communication. Pipes are inbound, meaning data is sent from client to server.

Not that well tested, especially in cases with multiple clients/servers open shenanigans. This is really meant for 1-to-1 scenarios, so I have no idea how it will work with multiple clients/servers.

May rename client to sender and server to receiver, since those are way better names.
