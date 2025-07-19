/*
    Protocol Connection Dock Support

    Created on 2025.6.7 at Wujing, Minhang

    gongty [at] alumni [dot] tongji [dot] edu [dot] cn

*/

/*
    TODO (For server mode):
        1. bind
        2. listen
        3. socket
*/


#pragma once

#include <monkey/dock/Connection.h>
#include <monkey/net/protocol/ProtocolConnection.h>
#include <base/env.h>


// Place this in public access field.
#define MONKEY_NET_PROTOCOL_USE_DOCK() \
    monkey::dock::Connection* dock = nullptr; \
    inline virtual void close() override { \
        dock->close(socketFd); \
    } \
    inline virtual monkey::Status connect() override { \
        close(); \
        Genode::log("[Socket4 Dock] Connecting to ", ip.toString().c_str(), " : ", port); \
        socketFd = dock->socket(); \
        if (socketFd == -1) \
            return Status::NETWORK_ERROR; \
        if (dock->connect(socketFd, ip, port)) \
            return Status::NETWORK_ERROR; \
        return Status::SUCCESS; \
    } \
    inline virtual adl::int64_t recv(void* buf, adl::size_t len) override { \
        return dock->recv(socketFd, buf, len); \
    } \
    inline virtual adl::int64_t send(const void* buf, adl::size_t len) override { \
        return dock->send(socketFd, buf, len); \
    }



#define MONKEY_NET_IMPL_DOCK_PROTOCOL(baseclass) \
    class baseclass ## Dock : public baseclass { \
    public:    \
        MONKEY_NET_PROTOCOL_USE_DOCK(); \
    };
    