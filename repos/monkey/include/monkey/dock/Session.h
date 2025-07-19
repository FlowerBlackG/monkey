/*
    Monkey Dock

    gongty [at] tongji [dot] edu [dot] cn
    created on 2025.2.24 at Wujing, Minhang

*/

#pragma once

#include <base/rpc.h>
#include <base/env.h>
#include <session/session.h>
#include <monkey/net/IP4Addr.h>
#include <monkey/Status.h>
#include <adl/sys/types.h>

/**
 * Dock should be used for single connect, which means it is used for clients.
 *
 * If you want to accept multiple clients as a server, what you want might be a port.
 */
namespace monkey::dock {

struct Session : Genode::Session {
    static const char* service_name() {
        return "MonkeyDock";
    }

    enum { CAP_QUOTA = 8 };

    
    virtual int socket() = 0;
    virtual void close(int socketFd) = 0;
    virtual int connect(int socketFd, net::IP4Addr ip, adl::uint16_t port) = 0;

    virtual monkey::Status makeBuffer(adl::size_t nPages) = 0;
    virtual Genode::Ram_dataspace_capability getBuffer() = 0;

    /**
     * Data should sit in the buffer before calling this.
     */
    virtual adl::int64_t send(int socketFd, adl::size_t len) = 0;
    virtual adl::int64_t recv(int socketFd, adl::size_t len) = 0;


    /*******************
     ** RPC interface **
     *******************/

    GENODE_RPC(Rpc_socket, int, socket);
    GENODE_RPC(Rpc_close, void, close, int);
    GENODE_RPC(Rpc_connect, int, connect, int, net::IP4Addr, adl::uint16_t);
    GENODE_RPC(Rpc_makeBuffer, monkey::Status, makeBuffer, adl::size_t);
    GENODE_RPC(Rpc_getBuffer, Genode::Ram_dataspace_capability, getBuffer);
    GENODE_RPC(Rpc_send, adl::int64_t, send, int, adl::size_t);
    GENODE_RPC(Rpc_recv, adl::int64_t, recv, int, adl::size_t);


    GENODE_RPC_INTERFACE(
        Rpc_socket,
        Rpc_close,
        Rpc_connect,
        Rpc_makeBuffer,
        Rpc_getBuffer,
        Rpc_send,
        Rpc_recv
    );
    
};

}
