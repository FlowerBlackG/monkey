/*
 * \brief  Kernel back-end for execution contexts in userland
 * \author Martin Stein
 * \author Stefan Kalkowski
 * \date   2013-09-15
 */

/*
 * Copyright (C) 2013-2025 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

/* Genode includes */
#include <base/thread_state.h>
#include <cpu_session/cpu_session.h>

/* base-internal includes */
#include <base/internal/crt0.h>

/* core includes */
#include <hw/assert.h>
#include <kernel/cpu.h>
#include <kernel/thread.h>
#include <kernel/irq.h>
#include <kernel/log.h>
#include <map_local.h>
#include <platform_pd.h>

extern "C" void _core_start(void);

using namespace Kernel;


Thread::Ipc_alloc_result Thread::_ipc_alloc_recv_caps(unsigned cap_count)
{
	using Allocator   = Genode::Allocator;
	using Result      = Ipc_alloc_result;
	using Alloc_error = Genode::Alloc_error;

	Allocator &slab = pd().platform_pd().capability_slab();
	for (unsigned i = 0; i < cap_count; i++) {
		if (_obj_id_ref_ptr[i] != nullptr)
			continue;

		Result const result =
			slab.try_alloc(sizeof(Object_identity_reference)).convert<Result>(

			[&] (Genode::Memory::Allocation &a) {
				_obj_id_ref_ptr[i] = a.ptr;
				a.deallocate = false;
				return Result::OK;
			},
			[&] (Alloc_error e) {

				/*
				 * Conditions other than DENIED cannot happen because the slab
				 * does not try to grow automatically. It is explicitely
				 * expanded by the client as response to the EXHAUSTED return
				 * value.
				 */
				if (e != Alloc_error::DENIED)
					Genode::raw("unexpected recv_caps allocation failure");

				return Result::EXHAUSTED;
			}
		);
		if (result == Result::EXHAUSTED)
			return result;
	}
	_ipc_rcv_caps = cap_count;
	return Result::OK;
}


void Thread::_ipc_free_recv_caps()
{
	for (unsigned i = 0; i < _ipc_rcv_caps; i++) {
		if (_obj_id_ref_ptr[i]) {
			Genode::Allocator &slab = pd().platform_pd().capability_slab();
			slab.free(_obj_id_ref_ptr[i], sizeof(Object_identity_reference));
		}
	}
	_ipc_rcv_caps = 0;
}


Thread::Ipc_alloc_result Thread::_ipc_init(Genode::Native_utcb &utcb, Thread &starter)
{
	_utcb = &utcb;

	switch (_ipc_alloc_recv_caps((unsigned)(starter._utcb->cap_cnt()))) {

	case Ipc_alloc_result::OK:
		ipc_copy_msg(starter);
		break;

	case Ipc_alloc_result::EXHAUSTED:
		return Ipc_alloc_result::EXHAUSTED;
	}
	return Ipc_alloc_result::OK;
}


void Thread::ipc_copy_msg(Thread &sender)
{
	using namespace Genode;
	using Reference = Object_identity_reference;

	/* copy payload and set destination capability id */
	*_utcb = *sender._utcb;
	_utcb->destination(sender._ipc_capid);

	/* translate capabilities */
	for (unsigned i = 0; i < _ipc_rcv_caps; i++) {

		capid_t id = sender._utcb->cap_get(i);

		/* if there is no capability to send, nothing to do */
		if (i >= sender._utcb->cap_cnt()) { continue; }

		/* lookup the capability id within the caller's cap space */
		Reference *oir = (id == cap_id_invalid())
			? nullptr : sender.pd().cap_tree().find(id);

		/* if the caller's capability is invalid, continue */
		if (!oir) {
			_utcb->cap_add(cap_id_invalid());
			continue;
		}

		/* lookup the capability id within the callee's cap space */
		Reference *dst_oir = oir->find(pd());

		/* if it is not found, and the target is not core, create a reference */
		if (!dst_oir && (&pd() != &_core_pd)) {
			dst_oir = oir->factory(_obj_id_ref_ptr[i], pd());
			if (dst_oir) _obj_id_ref_ptr[i] = nullptr;
		}

		if (dst_oir) dst_oir->add_to_utcb();

		/* add the translated capability id to the target buffer */
		_utcb->cap_add(dst_oir ? dst_oir->capid() : cap_id_invalid());
	}
}


Thread::
Tlb_invalidation::Tlb_invalidation(Inter_processor_work_list &global_work_list,
                                   Thread                    &caller,
                                   Pd                        &pd,
                                   addr_t                     addr,
                                   size_t                     size,
                                   unsigned                   cnt)
:
	global_work_list { global_work_list },
	caller           { caller },
	pd               { pd },
	addr             { addr },
	size             { size },
	cnt              { cnt }
{
	global_work_list.insert(&_le);
	caller._become_inactive(AWAITS_RESTART);
}


template <typename Obj>
Thread::Destroy<Obj>::Destroy(Thread &caller, Obj &to_destroy)
:
	_caller(caller), _obj_to_destroy(to_destroy)
{
	_obj_to_destroy->_cpu().work_list().insert(&_le);
	_caller._become_inactive(AWAITS_RESTART);
}


template <typename Obj>
void Thread::Destroy<Obj>::execute(Cpu &)
{
	_obj_to_destroy->_cpu().work_list().remove(&_le);
	_obj_to_destroy.destruct();
	_caller._restart();
}

template class Thread::Destroy<Thread>;
template class Thread::Destroy<Vcpu>;


void Thread_fault::print(Genode::Output &out) const
{
	Genode::print(out, "ip=",          Genode::Hex(ip));
	Genode::print(out, " fault-addr=", Genode::Hex(addr));
	Genode::print(out, " type=");
	switch (type) {
		case WRITE:        Genode::print(out, "write-fault"); return;
		case EXEC:         Genode::print(out, "exec-fault"); return;
		case PAGE_MISSING: Genode::print(out, "no-page"); return;
		case UNKNOWN:      Genode::print(out, "unknown"); return;
	};
}


void Thread::signal_context_kill_pending()
{
	assert(_state == ACTIVE);
	_become_inactive(AWAITS_SIGNAL_CONTEXT_KILL);
}


void Thread::signal_context_kill_done()
{
	assert(_state == AWAITS_SIGNAL_CONTEXT_KILL);
	user_arg_0(0);
	_become_active();
}


void Thread::signal_context_kill_failed()
{
	assert(_state == AWAITS_SIGNAL_CONTEXT_KILL);
	user_arg_0(-1);
	_become_active();
}


void Thread::signal_wait_for_signal()
{
	_become_inactive(AWAITS_SIGNAL);
}


void Thread::signal_receive_signal(void * const base, size_t const size)
{
	assert(_state == AWAITS_SIGNAL);
	Genode::memcpy(utcb()->data(), base, size);
	_become_active();
}


void Thread::ipc_send_request_succeeded()
{
	assert(_state == AWAITS_IPC);
	user_arg_0(0);
	_state = ACTIVE;
	_activate();
	helping_finished();
}


void Thread::ipc_send_request_failed()
{
	assert(_state == AWAITS_IPC);
	user_arg_0(-1);
	_state = ACTIVE;
	_activate();
	helping_finished();
}


void Thread::ipc_await_request_succeeded()
{
	assert(_state == AWAITS_IPC);
	user_arg_0(0);
	_become_active();
}


void Thread::_become_active()
{
	if (_state != ACTIVE && !_paused) Cpu_context::_activate();
	_state = ACTIVE;
}


void Thread::_become_inactive(State const s)
{
	if (_state == ACTIVE && !_paused) Cpu_context::_deactivate();
	_state = s;
}


void Thread::_die() { _become_inactive(DEAD); }


size_t Thread::_core_to_kernel_quota(size_t const quota) const
{
	using Genode::Cpu_session;

	/* we assert at timer construction that cpu_quota_us in ticks fits size_t */
	size_t const ticks = (size_t)
		_cpu().timer().us_to_ticks(Kernel::cpu_quota_us);
	return Cpu_session::quota_lim_downscale(quota, ticks);
}


void Thread::_call_thread_quota()
{
	Thread * const thread = (Thread *)user_arg_1();
	thread->Cpu_context::quota((unsigned)(_core_to_kernel_quota(user_arg_2())));
}


void Thread::_call_start_thread()
{
	user_arg_0(0);
	Thread &thread = *(Thread*)user_arg_1();

	assert(thread._state == AWAITS_START);

	/* join protection domain */
	thread._pd = (Pd *) user_arg_2();
	switch (thread._ipc_init(*(Native_utcb *)user_arg_3(), *this)) {
	case Ipc_alloc_result::OK:
		break;
	case Ipc_alloc_result::EXHAUSTED:
		user_arg_0(-2);
		return;
	}

	/*
	 * Sanity check core threads!
	 *
	 * Currently, the model assumes that there is only one core
	 * entrypoint, which serves requests, and which can destroy
	 * threads and pds. If this changes, we have to inform all
	 * cpus about pd destructions to remove their page-tables
	 * from the hardware in case that a core-thread running with
	 * that same pd is currently active. Therefore, warn if the
	 * semantic changes, and additional core threads are started
	 * across cpu cores.
	 */
	if (thread._pd == &_core_pd &&
	    thread._cpu().id() != _cpu_pool.primary_cpu().id())
	        Genode::raw("Error: do not start core threads"
	                    " on CPU cores different than boot cpu");

	thread._become_active();
}


void Thread::_call_pause_thread()
{
	Thread &thread = *reinterpret_cast<Thread*>(user_arg_1());
	if (thread._state == ACTIVE && !thread._paused)
		thread._deactivate();

	thread._paused = true;
}


void Thread::_call_resume_thread()
{
	Thread &thread = *reinterpret_cast<Thread*>(user_arg_1());
	if (thread._state == ACTIVE && thread._paused)
		thread._activate();

	thread._paused = false;
}


void Thread::_call_stop_thread()
{
	assert(_state == ACTIVE);
	_become_inactive(AWAITS_RESTART);
}


void Thread::_call_restart_thread()
{
	Thread *thread_ptr = pd().cap_tree().find<Thread>((capid_t)user_arg_1());

	if (!thread_ptr)
		return;

	Thread &thread = *thread_ptr;

	if (_type == USER && (&pd() != &thread.pd())) {
		raw(*this, ": failed to lookup thread ", (unsigned)user_arg_1(),
		        " to restart it");
		_die();
		return;
	}

	user_arg_0(thread._restart());
}


bool Thread::_restart()
{
	assert(_state == ACTIVE || _state == AWAITS_RESTART);

	if (_state == ACTIVE && _exception_state == NO_EXCEPTION)
		return false;

	_exception_state = NO_EXCEPTION;
	_become_active();
	return true;
}


void Thread::_call_yield_thread()
{
	Cpu_context::_yield();
}


void Thread::_call_delete_thread()
{
	Core::Kernel_object<Thread> & to_delete =
		*(Core::Kernel_object<Thread>*)user_arg_1();

	/**
	 * Delete a thread immediately if it is assigned to this cpu,
	 * or the assigned cpu did not scheduled it.
	 */
	if (to_delete->_cpu().id() == Cpu::executing_id() ||
	    &to_delete->_cpu().current_context() != &*to_delete) {
		_call_delete<Thread>();
		return;
	}

	/**
	 * Construct a cross-cpu work item and send an IPI
	 */
	_thread_destroy.construct(*this, to_delete);
	to_delete->_cpu().trigger_ip_interrupt();
}


void Thread::_call_delete_pd()
{
	Core::Kernel_object<Pd> & pd =
		*(Core::Kernel_object<Pd>*)user_arg_1();

	if (_cpu().active(pd->mmu_regs))
		_cpu().switch_to(_core_pd.mmu_regs);

	_call_delete<Pd>();
}


void Thread::_call_await_request_msg()
{
	if (_ipc_node.ready_to_wait()) {

		switch (_ipc_alloc_recv_caps((unsigned)user_arg_1())) {
		case Ipc_alloc_result::OK:
			break;
		case Ipc_alloc_result::EXHAUSTED:
			user_arg_0(-2);
			return;
		}
		_ipc_node.wait();
		if (_ipc_node.waiting()) {
			_become_inactive(AWAITS_IPC);
		} else {
			user_arg_0(0);
		}
	} else {
		Genode::raw("IPC await request: bad state, will block");
		_become_inactive(DEAD);
	}
}


void Thread::_call_timeout()
{
	Timer & t = _cpu().timer();
	_timeout_sigid = (Kernel::capid_t)user_arg_2();
	t.set_timeout(*this, t.us_to_ticks(user_arg_1()));
}


void Thread::_call_timeout_max_us()
{
	user_ret_time(_cpu().timer().timeout_max_us());
}


void Thread::_call_time()
{
	Timer & t = _cpu().timer();
	user_ret_time(t.ticks_to_us(t.time()));
}


void Thread::timeout_triggered()
{
	Signal_context * const c =
		pd().cap_tree().find<Signal_context>(_timeout_sigid);
	if (c) c->submit(1);
	else Genode::warning(*this, ": failed to submit timeout signal");
}


void Thread::_call_send_request_msg()
{
	Object_identity_reference * oir = pd().cap_tree().find((capid_t)user_arg_1());
	Thread * const dst = (oir) ? oir->object<Thread>() : nullptr;
	if (!dst) {
		Genode::raw(*this, ": cannot send to unknown recipient ",
		                 (unsigned)user_arg_1());
		_become_inactive(DEAD);
		return;
	}
	bool const help = Cpu_context::_helping_possible(*dst);
	oir = oir->find(dst->pd());

	if (!_ipc_node.ready_to_send()) {
		Genode::raw("IPC send request: bad state");
	} else {
		switch (_ipc_alloc_recv_caps((unsigned)user_arg_2())) {
		case Ipc_alloc_result::OK:
			break;
		case Ipc_alloc_result::EXHAUSTED:
			user_arg_0(-2);
			return;
		}
		_ipc_capid = oir ? oir->capid() : cap_id_invalid();
		_ipc_node.send(dst->_ipc_node);
	}

	_state = AWAITS_IPC;
	if (help) Cpu_context::_help(*dst);
	if (!help || !dst->ready()) _deactivate();
}


void Thread::_call_send_reply_msg()
{
	_ipc_node.reply();
	bool const await_request_msg = user_arg_2();
	if (await_request_msg) { _call_await_request_msg(); }
	else { user_arg_0(0); }
}


void Thread::_call_pager()
{
	/* override event route */
	Thread &thread = *(Thread *)user_arg_1();
	Thread &pager  = *(Thread *)user_arg_2();
	Signal_context &sc = *pd().cap_tree().find<Signal_context>((Kernel::capid_t)user_arg_3());
	thread._fault_context.construct(pager, sc);
}


void Thread::_call_print_char() { Kernel::log((char)user_arg_1()); }


void Thread::_call_await_signal()
{
	/* lookup receiver */
	Signal_receiver * const r = pd().cap_tree().find<Signal_receiver>((Kernel::capid_t)user_arg_1());
	if (!r) {
		Genode::raw(*this, ": cannot await, unknown signal receiver ",
		                (unsigned)user_arg_1());
		user_arg_0(-1);
		return;
	}
	/* register handler at the receiver */
	if (!r->add_handler(_signal_handler)) {
		Genode::raw("failed to register handler at signal receiver");
		user_arg_0(-1);
		return;
	}
	user_arg_0(0);
}


void Thread::_call_pending_signal()
{
	/* lookup receiver */
	Signal_receiver * const r = pd().cap_tree().find<Signal_receiver>((Kernel::capid_t)user_arg_1());
	if (!r) {
		Genode::raw(*this, ": cannot await, unknown signal receiver ",
		                (unsigned)user_arg_1());
		user_arg_0(-1);
		return;
	}

	/* register handler at the receiver */
	if (!r->add_handler(_signal_handler)) {
		user_arg_0(-1);
		return;
	}

	if (_state == AWAITS_SIGNAL) {
		_signal_handler.cancel_waiting();
		_become_active();
		user_arg_0(-1);
		return;
	}

	user_arg_0(0);
}


void Thread::_call_submit_signal()
{
	/* lookup signal context */
	Signal_context * const c = pd().cap_tree().find<Signal_context>((Kernel::capid_t)user_arg_1());
	if(c) c->submit((unsigned)user_arg_2());
}


void Thread::_call_ack_signal()
{
	/* lookup signal context */
	Signal_context * const c = pd().cap_tree().find<Signal_context>((Kernel::capid_t)user_arg_1());
	if (c) c->ack();
	else Genode::warning(*this, ": cannot ack unknown signal context");
}


void Thread::_call_kill_signal_context()
{
	/* lookup signal context */
	Signal_context * const c = pd().cap_tree().find<Signal_context>((Kernel::capid_t)user_arg_1());
	if (c) c->kill(_signal_context_killer);
	else Genode::warning(*this, ": cannot kill unknown signal context");
}


void Thread::_call_new_irq()
{
	Signal_context * const c = pd().cap_tree().find<Signal_context>((Kernel::capid_t)user_arg_4());
	if (!c) {
		Genode::raw(*this, ": invalid signal context for interrupt");
		user_arg_0(-1);
		return;
	}

	Genode::Irq_session::Trigger  trigger  =
		(Genode::Irq_session::Trigger)  ((user_arg_3() >> 2) & 0b11);
	Genode::Irq_session::Polarity polarity =
		(Genode::Irq_session::Polarity) (user_arg_3() & 0b11);

	_call_new<User_irq>((unsigned)user_arg_2(), trigger, polarity, *c,
	                    _cpu().pic(), _user_irq_pool);
}


void Thread::_call_ack_irq() {
	reinterpret_cast<User_irq*>(user_arg_1())->enable(); }


void Thread::_call_new_obj()
{
	/* lookup thread */
	Object_identity_reference * ref = pd().cap_tree().find((Kernel::capid_t)user_arg_2());
	Thread * thread = ref ? ref->object<Thread>() : nullptr;
	if (!thread ||
		(static_cast<Core_object<Thread>*>(thread)->capid() != ref->capid())) {
		if (thread)
			Genode::raw("faked thread", thread);
		user_arg_0(cap_id_invalid());
		return;
	}

	using Thread_identity = Genode::Constructible<Core_object_identity<Thread>>;
	Thread_identity & coi = *(Thread_identity*)user_arg_1();
	coi.construct(_core_pd, *thread);
	user_arg_0(coi->core_capid());
}


void Thread::_call_delete_obj()
{
	using Thread_identity = Genode::Constructible<Core_object_identity<Thread>>;
	Thread_identity & coi = *(Thread_identity*)user_arg_1();
	coi.destruct();
}


void Thread::_call_ack_cap()
{
	Object_identity_reference * oir = pd().cap_tree().find((Kernel::capid_t)user_arg_1());
	if (oir) oir->remove_from_utcb();
}


void Thread::_call_delete_cap()
{
	Object_identity_reference * oir = pd().cap_tree().find((Kernel::capid_t)user_arg_1());
	if (!oir) return;

	if (oir->in_utcb()) return;

	destroy(pd().platform_pd().capability_slab(), oir);
}


void Kernel::Thread::_call_invalidate_tlb()
{
	Pd * const pd = (Pd *) user_arg_1();
	addr_t addr   = (addr_t) user_arg_2();
	size_t size   = (size_t) user_arg_3();
	unsigned cnt = 0;

	_cpu_pool.for_each_cpu([&] (Cpu & cpu) {
		/* if a cpu needs to update increase the counter */
		if (pd->invalidate_tlb(cpu, addr, size)) cnt++; });

	/* insert the work item in the list if there are outstanding cpus */
	if (cnt) {
		_tlb_invalidation.construct(
			_cpu_pool.work_list(), *this, *pd, addr, size, cnt);
	}
}


void Thread::_call_get_cpu_state() {
	Thread &thread = *(Thread*)user_arg_1();
	Cpu_state &cpu_state = *(Cpu_state*)user_arg_2();
	cpu_state = *thread.regs;
}


void Thread::_call_set_cpu_state() {
	Thread &thread = *(Thread*)user_arg_1();
	Cpu_state &cpu_state = *(Cpu_state*)user_arg_2();
	static_cast<Cpu_state&>(*thread.regs) = cpu_state;
}


void Thread::_call_exception_state() {
	Thread &thread = *(Thread*)user_arg_1();
	Exception_state &exception_state = *(Exception_state*)user_arg_2();
	exception_state = thread.exception_state();
}


void Thread::_call_single_step() {
	Thread &thread = *(Thread*)user_arg_1();
	bool on = *(bool*)user_arg_2();
	Cpu::single_step(*thread.regs, on);
}


void Thread::_call_ack_pager_signal()
{
	Signal_context * const c = pd().cap_tree().find<Signal_context>((Kernel::capid_t)user_arg_1());
	if (!c)
		Genode::raw(*this, ": cannot ack unknown signal context");
	else
		c->ack();

	Thread &thread = *(Thread*)user_arg_2();
	thread.helping_finished();

	bool resolved = user_arg_3() ||
	                thread._exception_state == NO_EXCEPTION;
	if (resolved) thread._restart();
	else          thread._become_inactive(AWAITS_RESTART);
}



void Thread::_call()
{
	/* switch over unrestricted kernel calls */
	unsigned const call_id = (unsigned)user_arg_0();
	switch (call_id) {
	case call_id_cache_coherent_region():    _call_cache_coherent_region(); return;
	case call_id_cache_clean_inv_region():   _call_cache_clean_invalidate_data_region(); return;
	case call_id_cache_inv_region():         _call_cache_invalidate_data_region(); return;
	case call_id_cache_line_size():          _call_cache_line_size(); return;
	case call_id_stop_thread():              _call_stop_thread(); return;
	case call_id_restart_thread():           _call_restart_thread(); return;
	case call_id_yield_thread():             _call_yield_thread(); return;
	case call_id_send_request_msg():         _call_send_request_msg(); return;
	case call_id_send_reply_msg():           _call_send_reply_msg(); return;
	case call_id_await_request_msg():        _call_await_request_msg(); return;
	case call_id_kill_signal_context():      _call_kill_signal_context(); return;
	case call_id_submit_signal():            _call_submit_signal(); return;
	case call_id_await_signal():             _call_await_signal(); return;
	case call_id_pending_signal():           _call_pending_signal(); return;
	case call_id_ack_signal():               _call_ack_signal(); return;
	case call_id_print_char():               _call_print_char(); return;
	case call_id_ack_cap():                  _call_ack_cap(); return;
	case call_id_delete_cap():               _call_delete_cap(); return;
	case call_id_timeout():                  _call_timeout(); return;
	case call_id_timeout_max_us():           _call_timeout_max_us(); return;
	case call_id_time():                     _call_time(); return;
	case call_id_run_vcpu():                 _call_run_vcpu(); return;
	case call_id_pause_vcpu():               _call_pause_vcpu(); return;
	default:
		/* check wether this is a core thread */
		if (_type != CORE) {
			Genode::raw(*this, ": not entitled to do kernel call");
			_die();
			return;
		}
	}
	/* switch over kernel calls that are restricted to core */
	switch (call_id) {
	case call_id_new_thread():
		_call_new<Thread>(_addr_space_id_alloc, _user_irq_pool, _cpu_pool,
		                  _cpu_pool.cpu((unsigned)user_arg_2()),
		                  _core_pd, (unsigned) user_arg_3(),
		                  (unsigned) _core_to_kernel_quota(user_arg_4()),
		                  (char const *) user_arg_5(), USER);
		return;
	case call_id_new_core_thread():
		_call_new<Thread>(_addr_space_id_alloc, _user_irq_pool, _cpu_pool,
		                  _cpu_pool.cpu((unsigned)user_arg_2()),
		                  _core_pd, (char const *) user_arg_3());
		return;
	case call_id_thread_quota():           _call_thread_quota(); return;
	case call_id_delete_thread():          _call_delete_thread(); return;
	case call_id_start_thread():           _call_start_thread(); return;
	case call_id_resume_thread():          _call_resume_thread(); return;
	case call_id_thread_pager():           _call_pager(); return;
	case call_id_invalidate_tlb():         _call_invalidate_tlb(); return;
	case call_id_new_pd():
		_call_new<Pd>(*(Hw::Page_table *)    user_arg_2(),
		              *(Core::Platform_pd *) user_arg_3(),
		              _addr_space_id_alloc);
		return;
	case call_id_delete_pd():              _call_delete_pd(); return;
	case call_id_new_signal_receiver():    _call_new<Signal_receiver>(); return;
	case call_id_new_signal_context():
		_call_new<Signal_context>(*(Signal_receiver*) user_arg_2(), user_arg_3());
		return;
	case call_id_delete_signal_context():  _call_delete<Signal_context>(); return;
	case call_id_delete_signal_receiver(): _call_delete<Signal_receiver>(); return;
	case call_id_new_vcpu():               _call_new_vcpu(); return;
	case call_id_delete_vcpu():            _call_delete_vcpu(); return;
	case call_id_pause_thread():           _call_pause_thread(); return;
	case call_id_new_irq():                _call_new_irq(); return;
	case call_id_delete_irq():             _call_delete<User_irq>(); return;
	case call_id_ack_irq():                _call_ack_irq(); return;
	case call_id_new_obj():                _call_new_obj(); return;
	case call_id_delete_obj():             _call_delete_obj(); return;
	case call_id_suspend():                _call_suspend(); return;
	case call_id_get_cpu_state():          _call_get_cpu_state(); return;
	case call_id_set_cpu_state():          _call_set_cpu_state(); return;
	case call_id_exception_state():        _call_exception_state(); return;
	case call_id_single_step():            _call_single_step(); return;
	case call_id_ack_pager_signal():       _call_ack_pager_signal(); return;
	default:
		Genode::raw(*this, ": unknown kernel call");
		_die();
		return;
	}
}


void Thread::_signal_to_pager()
{
	if (!_fault_context.constructed()) {
		Genode::warning(*this, " could not send signal to pager");
		_die();
		return;
	}

	/* first signal to pager to wake it up */
	_fault_context->sc.submit(1);

	/* only help pager thread if runnable and scheduler allows it */
	bool const help = Cpu_context::_helping_possible(_fault_context->pager)
	                  && (_fault_context->pager._state == ACTIVE);
	if (help) Cpu_context::_help(_fault_context->pager);
	else _become_inactive(AWAITS_RESTART);
}


void Thread::_mmu_exception()
{
	using namespace Genode;
	using Genode::log;

	_exception_state = MMU_FAULT;
	Cpu::mmu_fault(*regs, _fault);
	_fault.ip = regs->ip;

	if (_fault.type == Thread_fault::UNKNOWN) {
		Genode::warning(*this, " raised unhandled MMU fault ", _fault);
		_die();
		return;
	}

	if (_type != USER) {
		error(*this, " raised a fault, which should never happen ",
		              _fault);
		log("Register dump: ", *regs);
		log("Backtrace:");

		Const_byte_range_ptr const stack {
			(char const*)Hw::Mm::core_stack_area().base,
			 Hw::Mm::core_stack_area().size };
		regs->for_each_return_address(stack, [&] (void **p) {
			log(*p); });
		_die();
		return;
	}

	_signal_to_pager();
}


void Thread::_exception()
{
	_exception_state = EXCEPTION;

	if (_type != USER) {
		Genode::raw(*this, " raised an exception, which should never happen");
		_die();
	}

	_signal_to_pager();
}


Thread::Thread(Board::Address_space_id_allocator &addr_space_id_alloc,
               Irq::Pool                         &user_irq_pool,
               Cpu_pool                          &cpu_pool,
               Cpu                               &cpu,
               Pd                                &core_pd,
               unsigned                    const  priority,
               unsigned                    const  quota,
               char                 const *const  label,
               Type                               type)
:
	Kernel::Object       { *this },
	Cpu_context          { cpu, priority, quota },
	_addr_space_id_alloc { addr_space_id_alloc },
	_user_irq_pool       { user_irq_pool },
	_cpu_pool            { cpu_pool },
	_core_pd             { core_pd },
	_ipc_node            { *this },
	_state               { AWAITS_START },
	_label               { label },
	_type                { type },
	regs                 { type != USER }
{ }


Thread::~Thread() { _ipc_free_recv_caps(); }


void Thread::print(Genode::Output &out) const
{
	Genode::print(out, _pd ? _pd->platform_pd().label() : "?");
	Genode::print(out, " -> ");
	Genode::print(out, label());
}


Genode::uint8_t __initial_stack_base[DEFAULT_STACK_SIZE];


/**********************
 ** Core_main_thread **
 **********************/

Core_main_thread::
Core_main_thread(Board::Address_space_id_allocator &addr_space_id_alloc,
                 Irq::Pool                         &user_irq_pool,
                 Cpu_pool                          &cpu_pool,
                 Pd                                &core_pd)
:
	Core_object<Thread>(core_pd, addr_space_id_alloc, user_irq_pool, cpu_pool,
	                    cpu_pool.primary_cpu(), core_pd, "core")
{
	using namespace Core;

	map_local(Platform::core_phys_addr((addr_t)&_utcb_instance),
	          (addr_t)utcb_main_thread(),
	          sizeof(Native_utcb) / get_page_size());

	_utcb_instance.cap_add(core_capid());
	_utcb_instance.cap_add(cap_id_invalid());
	_utcb_instance.cap_add(cap_id_invalid());

	/* start thread with stack pointer at the top of stack */
	regs->sp = (addr_t)&__initial_stack_base[0] + DEFAULT_STACK_SIZE;
	regs->ip = (addr_t)&_core_start;

	_utcb       = &_utcb_instance;
	Thread::_pd = &core_pd;
	_become_active();
}
