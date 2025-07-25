/*
 * \brief  CPU thread RPC object
 * \author Norman Feske
 * \date   2016-05-10
 */

/*
 * Copyright (C) 2016-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _CORE__INCLUDE__CPU_THREAD_COMPONENT_H_
#define _CORE__INCLUDE__CPU_THREAD_COMPONENT_H_

/* Genode includes */
#include <util/list.h>
#include <base/rpc_server.h>
#include <cpu_thread/cpu_thread.h>
#include <base/session_label.h>

/* core includes */
#include <pager.h>
#include <cpu_thread_allocator.h>
#include <pd_session_component.h>
#include <platform_thread.h>
#include <trace/control_area.h>
#include <trace/source_registry.h>

namespace Core {
	class Cpu_session_component;
	class Cpu_thread_component;
}


class Core::Cpu_thread_component : public  Rpc_object<Cpu_thread>,
                                   private List<Cpu_thread_component>::Element,
                                   public  Trace::Source::Info_accessor
{
	public:

		using Thread_name = Trace::Thread_name;

		using Pd_threads = Pd_session_component::Threads;

	private:

		friend class List<Cpu_thread_component>;
		friend class Cpu_session_component;

		Rpc_entrypoint           &_ep;
		Pager_entrypoint         &_pager_ep;
		Cpu_session_component    &_cpu;
		Region_map_component     &_address_space_region_map;
		Cpu_session::Weight const _weight;
		Session_label       const _session_label;
		Thread_name         const _name;
		Pd_threads::Element       _pd_element;
		Platform_thread           _platform_thread;

		/**
		 * Exception handler as defined by 'Cpu_session::exception_sigh'
		 */
		Signal_context_capability _session_sigh { };

		/**
		 * Exception handler as defined by 'Cpu_thread::exception_sigh'
		 */
		Signal_context_capability _thread_sigh { };

		Trace::Control_area::Result _trace_control_slot;

		Constructible<Trace::Source> _trace_source { };

		Trace::Source_registry &_trace_sources;

		struct Managed_thread_cap
		{
			Rpc_entrypoint          &_ep;
			Cpu_thread_component    &_thread;
			Thread_capability const  _cap;

			Managed_thread_cap(Rpc_entrypoint &ep, Cpu_thread_component &thread)
			: _ep(ep), _thread(thread), _cap(_ep.manage(&thread))
			{ }

			~Managed_thread_cap() { _ep.dissolve(&_thread); }

			Thread_capability cap() const { return _cap; }
		} const _managed_thread_cap;

		Rm_client _rm_client;

		/**
		 * Propagate exception handler to platform thread
		 */
		void _update_exception_sigh();

	public:

		/**
		 * Constructor
		 *
		 * \param ep         entrypoint used for managing the thread RPC
		 *                   object
		 * \param pager_ep   pager entrypoint used for handling the page
		 *                   faults of the thread
		 * \param pd         PD session where the thread is executed
		 * \param weight     scheduling weight relative to the other
		 *                   threads of the same CPU session
		 * \param quota      initial quota counter-value of the weight
		 * \param labal      label of the threads session
		 * \param name       name for the thread
		 * \param priority   scheduling priority
		 * \param utcb       user-local UTCB base
		 */
		Cpu_thread_component(Cpu_session_capability     cpu_session_cap,
		                     Cpu_session_component     &cpu,
		                     Rpc_entrypoint            &ep,
		                     Local_rm                  &local_rm,
		                     Pager_entrypoint          &pager_ep,
		                     Pd_session_component      &pd,
		                     Ram_allocator             &cpu_ram,
		                     Platform_pd               &platform_pd,
		                     Pd_threads                &pd_threads,
		                     Trace::Control_area       &trace_control_area,
		                     Trace::Source_registry    &trace_sources,
		                     Cpu_session::Weight        weight,
		                     size_t                     quota,
		                     Affinity::Location         location,
		                     Session_label       const &label,
		                     Thread_name         const &name,
		                     unsigned                   priority,
		                     addr_t                     utcb)
		:
			_ep(ep), _pager_ep(pager_ep), _cpu(cpu),
			_address_space_region_map(pd.address_space_region_map()),
			_weight(weight),
			_session_label(label), _name(name),
			_pd_element(pd_threads, *this),
			_platform_thread(platform_pd, ep, cpu_ram, local_rm, quota,
			                 name.string(), priority, location, utcb),
			_trace_control_slot(trace_control_area.alloc()),
			_trace_sources(trace_sources),
			_managed_thread_cap(_ep, *this),
			_rm_client(cpu_session_cap, _managed_thread_cap.cap(),
			           _address_space_region_map,
			           _platform_thread.pager_object_badge(),
			           _platform_thread.affinity(),
			           pd.label(), name)
		{
			_pager_ep.manage(_rm_client);

			_address_space_region_map.add_client(_rm_client);
			_platform_thread.pager(_rm_client);
			trace_control_area.with_control(_trace_control_slot,
				[&] (Trace::Control &control) {
					_trace_source.construct(*this, control);
					Trace::Source &source = *_trace_source;
					_trace_sources.insert(&source);
				});
		}

		~Cpu_thread_component()
		{
			if (_trace_source.constructed()) {
				Trace::Source &source = *_trace_source;
				_trace_sources.remove(&source);
			}
			_pager_ep.dissolve(_rm_client);

			_address_space_region_map.remove_client(_rm_client);
		}

		using Constructed = Attempt<Ok, Alloc_error>;

		Constructed const constructed = _platform_thread.constructed;

		void destroy(); /* solely called by ~Pd_session_component */


		/********************************************
		 ** Trace::Source::Info_accessor interface **
		 ********************************************/

		Trace::Source::Info trace_source_info() const override
		{
			return { _session_label, _name,
			         _platform_thread.execution_time(),
			         _platform_thread.affinity() };
		}


		/************************
		 ** Accessor functions **
		 ************************/

		/**
		 * Define default exception handler installed for the CPU session
		 */
		void session_exception_sigh(Signal_context_capability);

		void quota(size_t);

		/*
		 * Called by platform-specific 'Native_cpu' operations
		 */
		Platform_thread &platform_thread() { return _platform_thread; }

		size_t weight() const { return _weight.value; }


		/**************************
		 ** CPU thread interface **
		 *************************/

		Dataspace_capability utcb() override;
		void start(addr_t, addr_t) override;
		void pause() override;
		void resume() override;
		void single_step(bool) override;
		Thread_state state() override;
		void state(Thread_state const &) override;
		void exception_sigh(Signal_context_capability) override;
		void affinity(Affinity::Location) override;
		unsigned trace_control_index() override;
		Dataspace_capability trace_buffer() override;
		Dataspace_capability trace_policy() override;
};

#endif /* _CORE__INCLUDE__CPU_THREAD_COMPONENT_H_ */
