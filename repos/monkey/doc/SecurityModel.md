# Security Model

## Trust Zones

We have two trust zones: 

1. Apps
2. Concierge and memory nodes

We assume concierge and memory nodes are always reliable. We think apps could be sometimes hijacked.

## Authentication Method

Each connection should be authenticated. 

Concierge itself is always secure, as it is pointed by an static ip address.

Memory nodes should prove themselves by signing a unix timestamp (in seconds) with RC4. The key is shared across all memory nodes and concierge.

Apps prove themselves using the same method too, but each app has its own key.

Concierge and memory nodes shares keys of all apps, called keyring. When client attempts to create a connection, its key should be verified.

We can always believe a verified TCP connection.

## Identifier

Each node (including memory nodes and apps) has a unique UUID. This is used when memory nodes want to manage its memory. Also, it is used when handover between memory nodes happens, it is also used for apps to specify which node should provide services to it.
