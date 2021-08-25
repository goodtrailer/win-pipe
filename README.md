# win-pipe
Single-file C++ library for Windows named pipes.

Uses Windows's named pipes for inter-process communication. Pipes are inbound, meaning data is sent from client to server.

Not that well tested, especially in cases with multiple clients/servers open shenanigans. This is really meant for 1-to-1 scenarios, so I have no idea how it will work with multiple clients/servers.

### TODO
1. Rename `server`/`client` to `receiver`/`sender`
1. Handle cases where you send more data than you read (currently just disconnects the sender)
1. Handle cases with multiple senders/receivers on one pipe
    * Throw if multiple receivers, should only have one receiver per pipe (since reading pops the data)
    * Make multiple senders work if it doens't already
