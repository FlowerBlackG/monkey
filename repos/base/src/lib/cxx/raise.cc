/*
 * \brief  Reflect error conditions as C++ exceptions
 * \author Norman Feske
 * \date   2025-03-05
 */

/*
 * Copyright (C) 2025 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

/* Genode includes */
#include <base/exception.h>
#include <base/error.h>
#include <base/log.h>
#include <os/backtrace.h>


void Genode::raise(Alloc_error e)
{
	switch (e) {
	case Alloc_error::OUT_OF_RAM:  throw Out_of_ram();
	case Alloc_error::OUT_OF_CAPS: throw Out_of_caps();
	case Alloc_error::DENIED:      break;
	}
	throw Denied();
}


void Genode::raise(Unexpected_error e)
{
	switch (e) {
	case Unexpected_error::INDEX_OUT_OF_BOUNDS:
		error("attempt to use out-of-bounds index");
		break;

	case Unexpected_error::NONEXISTENT_SUB_NODE:
		warning("attempt to access non-existing sub node");
		break;

	case Unexpected_error::ACCESS_UNCONSTRUCTED_OBJ:
		error("attempt to access unconstructed object");
		break;

	case Unexpected_error::IPC_BUFFER_EXCEEDED:
		error("IPC marshalling/unmarshalling error");
		break;
	}

	backtrace();

	switch (e) {
	case Unexpected_error::INDEX_OUT_OF_BOUNDS:      throw Index_out_of_bounds();
	case Unexpected_error::NONEXISTENT_SUB_NODE:     throw Nonexistent_sub_node();
	case Unexpected_error::ACCESS_UNCONSTRUCTED_OBJ: throw Access_unconstructed_obj();
	case Unexpected_error::IPC_BUFFER_EXCEEDED:      break;
	}
	throw Ipc_error();
}
