/*
 * \brief  Vm root interface
 * \author Stefan Kalkowski
 * \date   2012-10-08
 */

/*
 * Copyright (C) 2012-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _CORE__INCLUDE__VM_ROOT_H_
#define _CORE__INCLUDE__VM_ROOT_H_

/* Genode includes */
#include <root/component.h>
#include <base/heap.h>

/* core includes */
#include <vm_session_component.h>

namespace Core { struct Vm_root; }


struct Core::Vm_root : Root_component<Vm_session_component>
{
	Ram_allocator          &_ram_allocator;
	Local_rm               &_local_rm;
	Trace::Source_registry &_trace_sources;

	Create_result _create_session(const char *args) override
	{
		unsigned priority = 0;
		Arg a = Arg_string::find_arg(args, "priority");
		if (a.valid()) {
			priority = (unsigned)a.ulong_value(0);

			/* clamp priority value to valid range */
			priority = min((unsigned)Cpu_session::PRIORITY_LIMIT - 1, priority);
		}

		return _alloc_obj(*ep(),
			              session_resources_from_args(args),
			              session_label_from_args(args),
			              session_diag_from_args(args),
			              _ram_allocator, _local_rm, priority,
			              _trace_sources);
	}

	void _upgrade_session(Vm_session_component &vm, const char *args) override
	{
		vm.upgrade(ram_quota_from_args(args));
		vm.upgrade(cap_quota_from_args(args));
	}

	/**
	 * Constructor
	 *
	 * \param session_ep  entrypoint managing vm_session components
	 * \param md_alloc    meta-data allocator to be used by root component
	 */
	Vm_root(Rpc_entrypoint         &session_ep,
	        Allocator              &md_alloc,
	        Ram_allocator          &ram_alloc,
	        Local_rm               &local_rm,
	        Trace::Source_registry &trace_sources)
	:
		Root_component<Vm_session_component>(&session_ep, &md_alloc),
		_ram_allocator(ram_alloc),
		_local_rm(local_rm),
		_trace_sources(trace_sources)
	{ }
};

#endif /* _CORE__INCLUDE__VM_ROOT_H_ */
