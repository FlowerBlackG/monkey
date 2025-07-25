/*
 * \brief  Fork bomb to stress Genode
 * \author Christian Helmuth
 * \author Norman Feske
 * \author Alexander Böttcher
 * \date   2007-08-16
 */

/*
 * Copyright (C) 2007-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#include <base/component.h>
#include <base/child.h>
#include <base/service.h>
#include <base/attached_rom_dataspace.h>
#include <init/child_policy.h>
#include <timer_session/connection.h>
#include <os/child_policy_dynamic_rom.h>
#include <os/static_parent_services.h>

using namespace Genode;


class Bomb_child : public Child_policy
{
	private:

		Env              &_env;
		Binary_name const _binary_name;
		Name        const _label;
		Cap_quota   const _cap_quota;
		Ram_quota   const _ram_quota;

		Registry<Registered<Parent_service> > &_parent_services;

		Id_space<Parent::Server> _server_ids { };

		Child_policy_dynamic_rom_file _config_policy { _env.rm(), "config", _env.ep().rpc_ep(), &_env.ram() };

		Child _child { _env.rm(), _env.ep().rpc_ep(), *this };

	public:

		Bomb_child(Env                                   &env,
		           Name                            const &binary_name,
		           Name                            const &label,
		           Cap_quota                       const  cap_quota,
		           Ram_quota                       const  ram_quota,
		           Registry<Registered<Parent_service> > &parent_services,
		           unsigned                               generation)
		:
			_env(env), _binary_name(binary_name), _label(label),
			_cap_quota(Child::effective_quota(cap_quota)),
			_ram_quota(Child::effective_quota(ram_quota)),
			_parent_services(parent_services)
		{
			String<64> config("<config generations=\"", generation, "\" master=\"no\"/>");
			_config_policy.load(config.string(), config.length());
		}

		~Bomb_child() { log(__PRETTY_FUNCTION__); }


		/****************************
		 ** Child-policy interface **
		 ****************************/

		Name name() const override { return _label; }

		Binary_name binary_name() const override { return _binary_name; }

		void init(Pd_session &pd, Pd_session_capability pd_cap) override
		{
			pd.ref_account(_env.pd_session_cap());
			_env.pd().transfer_quota(pd_cap, _cap_quota);
			_env.pd().transfer_quota(pd_cap, _ram_quota);
		}

		Ram_allocator &session_md_ram() override { return _env.ram(); }

		Pd_account            &ref_account()           override { return _env.pd(); }
		Capability<Pd_account> ref_account_cap() const override { return _env.pd_session_cap(); }

		void _with_matching_service(Service::Name const &service_name,
		                           Session_label  const &label,
		                           auto const &fn, auto const &denied_fn)
		{
			Service *service = nullptr;

			/* check for config file request */
			if ((service = _config_policy.resolve_session_request(service_name, label))) {
				fn(*service);
				return;
			}

			_parent_services.for_each([&] (Service &s) {
				if (!service && service_name == s.name())
					service = &s; });

			if (service) fn(*service); else denied_fn();
		}

		void _with_route(Service::Name     const &service_name,
		                 Session_label     const &label,
		                 Session::Diag     const  diag,
		                 With_route::Ft    const &fn,
		                 With_no_route::Ft const &denied_fn) override
		{
			_with_matching_service(service_name, label,
				[&] (Service &service) {
					fn(Route { .service = service,
					           .label   = label,
					           .diag    = diag });
				}, denied_fn);
		}

		Id_space<Parent::Server> &server_id_space() override { return _server_ids; }
};


using Children = Registry<Registered<Bomb_child> >;


/**
 * Check if a program with the specified name already exists
 */
static bool child_name_exists(Children const &children,
                              Bomb_child::Name const &name)
{
	bool found = false;

	children.for_each([&] (Bomb_child const &child) {
		if (!found && child.name() == name)
			found = true; });

	return found;
}


/**
 * Create a unique name based on the filename
 *
 * If a program with the filename as name already exists, we
 * add a counting number as suffix.
 */
static Bomb_child::Name
unique_child_name(Children const &children, Bomb_child::Name const &binary_name,
                  unsigned const generation)
{
	/* serialize calls to this function */
	static Mutex mutex;
	Mutex::Guard guard(mutex);

	for (unsigned cnt = 1; ; cnt++) {

		/* if such a program name does not exist yet, we are happy */
		Bomb_child::Name const unique(binary_name, "_g", generation, ".", cnt);
		if (!child_name_exists(children, unique.string()))
			return unique;
	}
}


struct Bomb
{
	Genode::Env &env;

	Constructible<Timer::Connection> timer { };

	Genode::Signal_handler<Bomb> signal_timeout  { env.ep(), *this, &Bomb::destruct_children };
	Genode::Signal_handler<Bomb> signal_resource { env.ep(), *this, &Bomb::resource_request };

	Attached_rom_dataspace config { env, "config" };

	unsigned round = 0;
	unsigned const rounds     = config.xml().attribute_value("rounds", 1U);
	unsigned const generation = config.xml().attribute_value("generations", 1U);
	unsigned const children   = config.xml().attribute_value("children", 2U);
	uint64_t const sleeptime  = config.xml().attribute_value("sleep", (uint64_t)2000);
	size_t   const ram_demand = config.xml().attribute_value("demand", 1024UL * 1024);
	bool     const master     = config.xml().attribute_value("master", true);

	Heap heap { env.ram(), env.rm() };

	Children child_registry { };

	Static_parent_services<Pd_session, Cpu_session, Rom_session, Log_session, Timer::Session>
		parent_services { env };

	void construct_children()
	{
		size_t const preserved_ram = Pd_connection::RAM_QUOTA;
		size_t const avail_ram     = env.pd().avail_ram().value;

		if (avail_ram < preserved_ram + ram_demand) {
			error("RAM demand exceeds available RAM");
			return;
		}

		Ram_quota const ram_amount { (avail_ram - preserved_ram - ram_demand) / children };

		if (ram_amount.value < (ram_demand * children)) {
			log("I'm a leaf node - generation ", generation,
			    " - not enough memory.");
			return;
		}

		size_t const avail_caps     = env.pd().avail_caps().value;
		size_t const preserved_caps = children*30;

		if (avail_caps < preserved_caps) {
			log("I ran out of capabilities.");
			return;
		}

		Cap_quota const cap_quota { (avail_caps - preserved_caps) / children };

		if (generation == 0) {
			log("I'm a leaf node - generation 0");
			return;
		}

		log("[", round, "] It's time to start all my children...");

		Bomb_child::Name const binary_name("bomb");

		for (unsigned i = children; i; --i) {
			try {
				new (heap)
					Registered<Bomb_child>(child_registry, env, binary_name,
					                       unique_child_name(child_registry,
					                                         binary_name,
					                                         generation - 1),
					                       cap_quota, ram_amount,
					                       parent_services, generation - 1);
			} catch (...) {
				Genode::error("creation of child ", i, " failed");
			}
		}

		/* master if we have a timer connection */
		if (master)
			timer->trigger_once(sleeptime * 1000);
	}

	void destruct_children()
	{
		log("[", round, "] It's time to kill all my children...");

		child_registry.for_each([&] (Registered<Bomb_child> &child) {
			destroy(heap, &child); });

		log("[", round, "] Done.");

		++round;

		/* master if we have a timer connection */
		if (round == rounds && master) {
			log("Done. Going to sleep");
			return;
		}

		construct_children();
	}

	void resource_request()
	{
		Genode::error("resource request");
	}

	Bomb(Genode::Env &env) : env(env)
	{
		/*
		 * Don't ask parent for further resources if we ran out of memory.
		 * Prevent us to block for resource upgrades caused by clients
		 */
		env.parent().resource_avail_sigh(signal_resource);

		log("--- bomb started ---");

		if (master) {
			timer.construct(env);

			timer->sigh(signal_timeout);

			log("rounds=", rounds, " generations=", generation, " children=",
			    children, " sleep=", sleeptime, " demand=", ram_demand/1024, "K");
		}

		construct_children();
	}
};

void Component::construct(Genode::Env &env) { static Bomb bomb(env); }
