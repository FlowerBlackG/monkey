/*
    Sub-app of Monkey Lab.

    Created on 2025.6.22 at Minhang
    By gongty
*/

#pragma once


#include <adl/sys/types.h>

#include <libc/component.h>
#include <base/heap.h>

#include <adl/collections/HashMap.hpp>
#include <adl/TString.h>

#include <monkey/genodeutils/memory.h>
#include <monkey/tycoon/Tycoon.h>

#include "./config.h"


class LabApp {
protected:
    Genode::Env& env;
    Genode::Heap& heap;
    monkey::Tycoon& tycoon;

public:
    LabApp(
        Genode::Env& env,
        Genode::Heap& heap,
        monkey::Tycoon& tycoon
    ) : env(env), heap(heap), tycoon(tycoon) { }

    virtual ~LabApp() { }

    virtual monkey::Status run() { return monkey::Status::NOT_FOUND; }
};
