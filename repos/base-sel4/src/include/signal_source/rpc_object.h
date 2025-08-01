/*
 * \brief  Signal-source server interface
 * \author Alexander Boettcher
 * \date   2016-07-09
 */

/*
 * Copyright (C) 2016-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _INCLUDE__SIGNAL_SOURCE__RPC_OBJECT_H_
#define _INCLUDE__SIGNAL_SOURCE__RPC_OBJECT_H_

#include <base/allocator.h>
#include <base/rpc_server.h>
#include <signal_source/sel4_signal_source.h>

namespace Genode { struct Signal_source_rpc_object; }


struct Genode::Signal_source_rpc_object : Rpc_object<SeL4_signal_source,
                                                     Signal_source_rpc_object>
{
	protected:

		Allocator::Alloc_result _notify_phys { };
		Native_capability       _notify      { };

	public:

		Signal_source_rpc_object() {}

		Native_capability _request_notify() { return _notify; }
};

#endif /* _INCLUDE__SIGNAL_SOURCE__RPC_OBJECT_H_ */
