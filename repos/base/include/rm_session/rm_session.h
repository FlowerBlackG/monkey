/*
 * \brief  Region-map session interface
 * \author Norman Feske
 * \date   2016-04-15
 */

/*
 * Copyright (C) 2016-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _INCLUDE__RM_SESSION__RM_SESSION_H_
#define _INCLUDE__RM_SESSION__RM_SESSION_H_

#include <region_map/region_map.h>
#include <session/session.h>

namespace Genode { struct Rm_session; }


struct Genode::Rm_session : Session
{
	/**
	 * \noapi
	 */
	static const char *service_name() { return "RM"; }

	/*
	 * An RM session consumes a dataspace capability for the session-object
	 * allocation and its session capability.
	 */
	enum { CAP_QUOTA = 2 };

	using Create_result = Attempt<Capability<Region_map>, Alloc_error>;

	/**
	 * Create region map
	 *
	 * \param size  upper bound of region map
	 * \return      region-map capability
	 */
	virtual Create_result create(size_t size) = 0;

	/**
	 * Destroy region map
	 */
	virtual void destroy(Capability<Region_map>) = 0;


	/*********************
	 ** RPC declaration **
	 *********************/

	GENODE_RPC(Rpc_create, Create_result, create, size_t);
	GENODE_RPC(Rpc_destroy, void, destroy, Capability<Region_map>);

	GENODE_RPC_INTERFACE(Rpc_create, Rpc_destroy);
};

#endif /* _INCLUDE__RM_SESSION__RM_SESSION_H_ */
