/*
 * \brief  User-level task helpers
 * \author Christian Helmuth
 * \date   2016-01-22
 */

/*
 * Copyright (C) 2016-2025 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _INCLUDE__INTERNAL__CALL_FUNC_H_
#define _INCLUDE__INTERNAL__CALL_FUNC_H_

/* Libc includes */
#include <setjmp.h>  /* _setjmp() as we don't care about signal state */


/**
 * Call function with a new stack
 */
[[noreturn]] void call_func(void *sp, void *func, void *arg);

#endif /* _INCLUDE__INTERNAL__CALL_FUNC_H_ */
