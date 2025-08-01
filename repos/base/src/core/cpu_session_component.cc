/*
 * \brief  Core implementation of the CPU session/thread interfaces
 * \author Christian Helmuth
 * \date   2006-07-17
 *
 * FIXME arg_string and quota missing
 */

/*
 * Copyright (C) 2006-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

/* Genode includes */
#include <util/arg_string.h>

/* core includes */
#include <cpu_session_component.h>
#include <rm_session_component.h>
#include <pd_session_component.h>
#include <platform_generic.h>

using namespace Core;


Cpu_session::Create_thread_result
Cpu_session_component::create_thread(Capability<Pd_session> pd_cap,
                                     Name const &name, Affinity::Location affinity,
                                     Weight weight, addr_t utcb)
{
	if (!try_withdraw(Ram_quota{_utcb_quota_size()}))
		return Create_thread_error::OUT_OF_RAM;

	if (weight.value == 0) {
		warning("Thread ", name, ": Bad weight 0, using default weight instead.");
		weight = Weight();
	}
	if (weight.value > QUOTA_LIMIT) {
		warning("Thread ", name, ": Oversized weight ", weight.value, ", using limit instead.");
		weight = Weight(QUOTA_LIMIT);
	}

	Mutex::Guard thread_list_lock_guard(_thread_list_lock);

	Create_thread_result result = Create_thread_error::DENIED;

	_incr_weight(weight.value);

	_thread_ep.apply(pd_cap, [&] (Pd_session_component *pd) {

		if (!pd) {
			error("create_thread: invalid PD argument");
			return;
		}

		Mutex::Guard slab_lock_guard(_thread_alloc_lock);

		pd->with_threads([&] (Pd_session_component::Threads &pd_threads) {
			pd->with_platform_pd([&] (Platform_pd &platform_pd) {

				_thread_alloc.create(cap(), *this, _thread_ep, _local_rm,
				                     _pager_ep, *pd, _ram_alloc, platform_pd,
				                     pd_threads, _trace_control_area,
				                     _trace_sources, weight,
				                     _weight_to_quota(weight.value),
				                     _thread_affinity(affinity), _label, name,
				                     _priority, utcb).with_result(

					[&] (Thread_alloc::Allocation &thread) {
						thread.obj.constructed.with_result(
							[&] (Ok) {
								thread.obj.session_exception_sigh(_exception_sigh);
								_thread_list.insert(&thread.obj);
								thread.deallocate = false;
								result = thread.obj.cap();
							},
							[&] (Alloc_error e) { result = e; });
					},
					[&] (Alloc_error e) { result = e; });
			});
		});
	});

	if (result.failed()) {
		_decr_weight(weight.value);
		replenish(Ram_quota{_utcb_quota_size()});
	}
	return result;
}


Affinity::Location Cpu_session_component::_thread_affinity(Affinity::Location location) const
{
	/* convert session-local location to physical location */
	int const x1 = location.xpos() + _location.xpos(),
	          y1 = location.ypos() + _location.ypos(),
	          x2 = location.xpos() +  location.width(),
	          y2 = location.ypos() +  location.height();

	int const clipped_x1 = max(_location.xpos(), x1),
	          clipped_y1 = max(_location.ypos(), y1),
	          clipped_x2 = max(_location.xpos() + (int)_location.width()  - 1, x2),
	          clipped_y2 = max(_location.ypos() + (int)_location.height() - 1, y2);

	return Affinity::Location(clipped_x1, clipped_y1,
	                          clipped_x2 - clipped_x1 + 1,
	                          clipped_y2 - clipped_y1 + 1);
}


void Cpu_session_component::_unsynchronized_kill_thread(Thread_capability thread_cap)
{
	Cpu_thread_component *thread_ptr = nullptr;
	_thread_ep.apply(thread_cap, [&] (Cpu_thread_component *t) { thread_ptr = t; });

	if (!thread_ptr) return;

	_thread_list.remove(thread_ptr);

	_decr_weight(thread_ptr->weight());

	{
		Mutex::Guard lock_guard(_thread_alloc_lock);
		_thread_alloc.destroy(*thread_ptr);
	}

	replenish(Ram_quota{_utcb_quota_size()});
}


void Cpu_session_component::kill_thread(Thread_capability thread_cap)
{
	if (!thread_cap.valid())
		return;

	Mutex::Guard lock_guard(_thread_list_lock);

	/* check that cap belongs to this session */
	for (Cpu_thread_component *t = _thread_list.first(); t; t = t->next()) {
		if (t->cap() == thread_cap) {
			_unsynchronized_kill_thread(thread_cap);
			break;
		}
	}
}


void Cpu_session_component::exception_sigh(Signal_context_capability sigh)
{
	_exception_sigh = sigh;

	Mutex::Guard lock_guard(_thread_list_lock);

	for (Cpu_thread_component *t = _thread_list.first(); t; t = t->next())
		t->session_exception_sigh(_exception_sigh);
}


Affinity::Space Cpu_session_component::affinity_space() const
{
	/*
	 * Return affinity subspace as constrained by the CPU session
	 * affinity.
	 */
	return Affinity::Space(_location.width(), _location.height());
}


Dataspace_capability Cpu_session_component::trace_control()
{
	return _trace_control_area.dataspace();
}


void Cpu_session_component::_transfer_quota(Cpu_session_component &dst,
                                            size_t const quota)
{
	if (!quota) { return; }
	_decr_quota(quota);
	dst._incr_quota(quota);
}


int Cpu_session_component::transfer_quota(Cpu_session_capability dst_cap,
                                          size_t amount)
{
	/* lookup targeted CPU session */
	auto lambda = [&] (Cpu_session_component *dst) {
		if (!dst) {
			warning("Transfer CPU quota, ", _label, ", targeted session not found");
			return -1;
		}
		/* check reference relationship */
		if (dst->_ref != this && dst != _ref) {
			warning("Transfer CPU quota, ", _label, " -> ", dst->_label, ", "
			        "no reference relation");
			return -2;
		}
		/* check quota availability */
		size_t const quota = quota_lim_downscale(_quota, amount);
		if (quota > _quota) {
			warning("Transfer CPU quota, ", _label, " -> ", dst->_label, ", "
			        "insufficient quota ", _quota, ", need ", quota);
			return -3;
		}
		/* transfer quota */
		_transfer_quota(*dst, quota);
		return 0;
	};
	return _session_ep.apply(dst_cap, lambda);
}


int Cpu_session_component::ref_account(Cpu_session_capability ref_cap)
{
	/*
	 * Ensure that the ref account is set only once
	 *
	 * FIXME Add check for cycles along the tree of reference accounts
	 */
	if (_ref) {
		warning("set ref account, ", _label, ", set already");
		return -2; }

	/* lookup and check targeted CPU-session */
	auto lambda = [&] (Cpu_session_component *ref) {
		if (!ref) {
			warning("set ref account, ", _label, ", targeted session not found");
			return -1;
		}
		if (ref == this) {
			warning("set ref account, ", _label, ", self reference not allowed");
			return -3;
		}
		/* establish ref-account relation from targeted CPU-session to us */
		_ref = ref;
		_ref->_insert_ref_member(this);
		return 0;
	};
	return _session_ep.apply(ref_cap, lambda);
}


Cpu_session_component::Cpu_session_component(Rpc_entrypoint         &session_ep,
                                             Resources        const &resources,
                                             Label            const &label,
                                             Diag             const &diag,
                                             Ram_allocator          &ram_alloc,
                                             Local_rm               &local_rm,
                                             Rpc_entrypoint         &thread_ep,
                                             Pager_entrypoint       &pager_ep,
                                             Trace::Source_registry &trace_sources,
                                             char             const *args,
                                             Affinity         const &affinity,
                                             size_t           const  quota)
:
	Session_object(session_ep, resources, label, diag),
	_session_ep(session_ep), _thread_ep(thread_ep), _pager_ep(pager_ep),
	_local_rm(local_rm),
	_ram_alloc(ram_alloc, _ram_quota_guard(), _cap_quota_guard()),
	_md_alloc(_ram_alloc, local_rm),
	_thread_slab(_md_alloc), _priority(0),

	/* map affinity to a location within the physical affinity space */
	_location(affinity.scale_to(platform().affinity_space())),

	_trace_sources(trace_sources),
	_trace_control_area(_ram_alloc, local_rm),
	_quota(quota), _ref(0),
	_native_cpu(*this, args)
{
	Arg a = Arg_string::find_arg(args, "priority");
	if (a.valid()) {
		_priority = (unsigned)a.ulong_value(0);

		/* clamp priority value to valid range */
		_priority = min((unsigned)(PRIORITY_LIMIT - 1), _priority);
	}
}


Cpu_session_component::~Cpu_session_component()
{
	_deinit_threads();
	_deinit_ref_account();
}


void Cpu_session_component::_deinit_ref_account()
{
	/* rewire child ref accounts to this sessions's ref account */
	{
		Mutex::Guard lock_guard(_ref_members_lock);
		for (Cpu_session_component * s; (s = _ref_members.first()); ) {
			_unsync_remove_ref_member(*s);
			if (_ref)
				_ref->_insert_ref_member(s);
		}
	}

	if (_ref) {
		/* give back our remaining quota to our ref account */
		_transfer_quota(*_ref, _quota);

		/* remove ref-account relation between us and our ref-account */
		_ref->_remove_ref_member(*this);
	}
}


void Cpu_session_component::_deinit_threads()
{
	Mutex::Guard lock_guard(_thread_list_lock);

	/*
	 * We have to keep the '_thread_list_lock' during the whole destructor to
	 * prevent races with incoming calls of the 'create_thread' function,
	 * adding new threads while we are destroying them.
	 */

	for (Cpu_thread_component *thread; (thread = _thread_list.first()); )
		_unsynchronized_kill_thread(thread->cap());
}


void Cpu_session_component::
_update_thread_quota(Cpu_thread_component &thread) const
{
	thread.quota(_weight_to_quota(thread.weight()));
}


void Cpu_session_component::_incr_weight(size_t const weight)
{
	_weight += weight;
	if (_quota) { _update_each_thread_quota(); }
}


void Cpu_session_component::_decr_weight(size_t const weight)
{
	_weight -= weight;
	if (_quota) { _update_each_thread_quota(); }
}


void Cpu_session_component::_decr_quota(size_t const quota)
{
	Mutex::Guard lock_guard(_thread_list_lock);
	_quota -= quota;
	_update_each_thread_quota();
}


void Cpu_session_component::_incr_quota(size_t const quota)
{
	Mutex::Guard lock_guard(_thread_list_lock);
	_quota += quota;
	_update_each_thread_quota();
}


void Cpu_session_component::_update_each_thread_quota()
{
	Cpu_thread_component * thread = _thread_list.first();
	for (; thread; thread = thread->next()) { _update_thread_quota(*thread); }
}


size_t Cpu_session_component::_weight_to_quota(size_t const weight) const
{
	if (_weight == 0) return 0;

	return (weight * _quota) / _weight;
}


/****************************
 ** Trace::Source_registry **
 ****************************/

Core::Trace::Source::Id Core::Trace::Source::_alloc_unique_id()
{
	static Mutex lock;
	static unsigned cnt;
	Mutex::Guard guard(lock);
	return { cnt++ };
}

