/*
    Monkey Dock

    gongty [at] tongji [dot] edu [dot] cn
    created on 2025.2.25 at Wujing, Minhang

*/

#pragma once

#include <adl/sys/types.h>
#include <monkey/dock/Client.h>
#include <base/connection.h>
#include <monkey/dock/Session.h>


namespace monkey::dock {


struct Connection : Genode::Connection<Session>, Client
{

    Connection(Genode::Env& env)
    :
    Genode::Connection<Session>(env, Label(), Ram_quota {8 * 1024}, Args()),
    Client(cap(), env)
    {

    }

};


}
