/*
 * Copyright (c) 2015 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _kernel_include_timeout_q__h_
#define _kernel_include_timeout_q__h_

/**
 * @file
 * @brief timeout queue for threads on kernel objects
 *
 * This file is meant to be included by kernel/include/wait_q.h only
 */

#include <misc/dlist.h>

#ifdef __cplusplus
extern "C" {
#endif

/* initialize the timeouts part of k_thread when enabled in the kernel */

static inline void _init_timeout(struct _timeout *t, _timeout_func_t func)
{
	/*
	 * Must be initialized here and when dequeueing a timeout so that code
	 * not dealing with timeouts does not have to handle this, such as when
	 * waiting forever on a semaphore.
	 */
	t->delta_ticks_from_prev = _INACTIVE;

	/*
	 * Must be initialized here so that the _fiber_wakeup family of APIs can
	 * verify the fiber is not on a wait queue before aborting a timeout.
	 */
	t->wait_q = NULL;

	/*
	 * Must be initialized here, so the _handle_one_timeout()
	 * routine can check if there is a fiber waiting on this timeout
	 */
	t->thread = NULL;

	/*
	 * Function must be initialized before being potentially called.
	 */
	t->func = func;

	/*
	 * These are initialized when enqueing on the timeout queue:
	 *
	 *   thread->timeout.node.next
	 *   thread->timeout.node.prev
	 */
}

static ALWAYS_INLINE void
_init_thread_timeout(struct _thread_base *thread_base)
{
	_init_timeout(&thread_base->timeout, NULL);
}

/* remove a thread timing out from kernel object's wait queue */

static inline void _unpend_thread_timing_out(struct k_thread *thread,
					     struct _timeout *timeout_obj)
{
	if (timeout_obj->wait_q) {
		_unpend_thread(thread);
		thread->base.timeout.wait_q = NULL;
	}
}

/*
 * Handle one timeout from the expired timeout queue. Removes it from the wait
 * queue it is on if waiting for an object; in this case, the return value is
 * kept as -EAGAIN, set previously in _Swap().
 */

static inline void _handle_one_expired_timeout(struct _timeout *timeout)
{
	struct k_thread *thread = timeout->thread;
	unsigned int key = irq_lock();

	timeout->delta_ticks_from_prev = _INACTIVE;

	K_DEBUG("timeout %p\n", timeout);
	if (thread) {
		_unpend_thread_timing_out(thread, timeout);
		_ready_thread(thread);
		irq_unlock(key);
	} else {
		irq_unlock(key);
		if (timeout->func) {
			timeout->func(timeout);
		}
	}
}

/*
 * Loop over all expired timeouts and handle them one by one. Should be called
 * with interrupts unlocked: interrupts will be locked on each interation only
 * for the amount of time necessary.
 */

static inline void _handle_expired_timeouts(sys_dlist_t *expired)
{
	sys_dnode_t *timeout, *next;

	SYS_DLIST_FOR_EACH_NODE_SAFE(expired, timeout, next) {
		sys_dlist_remove(timeout);
		_handle_one_expired_timeout((struct _timeout *)timeout);
	}
}

/* returns _INACTIVE if the timer is not active */
static inline int _abort_timeout(struct _timeout *timeout)
{
	if (timeout->delta_ticks_from_prev == _INACTIVE) {
		return _INACTIVE;
	}

	if (!sys_dlist_is_tail(&_timeout_q, &timeout->node)) {
		sys_dnode_t *next_node =
			sys_dlist_peek_next(&_timeout_q, &timeout->node);
		struct _timeout *next = (struct _timeout *)next_node;

		next->delta_ticks_from_prev += timeout->delta_ticks_from_prev;
	}
	sys_dlist_remove(&timeout->node);
	timeout->delta_ticks_from_prev = _INACTIVE;

	return 0;
}

/* returns _INACTIVE if the timer has already expired */
static inline int _abort_thread_timeout(struct k_thread *thread)
{
	return _abort_timeout(&thread->base.timeout);
}

static inline void _dump_timeout(struct _timeout *timeout, int extra_tab)
{
#ifdef CONFIG_KERNEL_DEBUG
	char *tab = extra_tab ? "\t" : "";

	K_DEBUG("%stimeout %p, prev: %p, next: %p\n"
		"%s\tthread: %p, wait_q: %p\n"
		"%s\tticks remaining: %d\n"
		"%s\tfunction: %p\n",
		tab, timeout, timeout->node.prev, timeout->node.next,
		tab, timeout->thread, timeout->wait_q,
		tab, timeout->delta_ticks_from_prev,
		tab, timeout->func);
#endif
}

static inline void _dump_timeout_q(void)
{
#ifdef CONFIG_KERNEL_DEBUG
	sys_dnode_t *node;

	K_DEBUG("_timeout_q: %p, head: %p, tail: %p\n",
		&_timeout_q, _timeout_q.head, _timeout_q.tail);

	SYS_DLIST_FOR_EACH_NODE(&_timeout_q, node) {
		_dump_timeout((struct _timeout *)node, 1);
	}
#endif
}

/*
 * Add timeout to timeout queue. Record waiting thread and wait queue if any.
 *
 * Cannot handle timeout == 0 and timeout == K_FOREVER.
 *
 * Must be called with interrupts locked.
 */

static inline void _add_timeout(struct k_thread *thread,
				struct _timeout *timeout,
				_wait_q_t *wait_q,
				int32_t timeout_in_ticks)
{
	__ASSERT(timeout_in_ticks > 0, "");

	timeout->delta_ticks_from_prev = timeout_in_ticks;
	timeout->thread = thread;
	timeout->wait_q = (sys_dlist_t *)wait_q;

	K_DEBUG("before adding timeout %p\n", timeout);
	_dump_timeout(timeout, 0);
	_dump_timeout_q();

	int32_t *delta = &timeout->delta_ticks_from_prev;
	sys_dnode_t *node;

	SYS_DLIST_FOR_EACH_NODE(&_timeout_q, node) {
		struct _timeout *in_q = (struct _timeout *)node;

		if (*delta <= in_q->delta_ticks_from_prev) {
			in_q->delta_ticks_from_prev -= *delta;
			sys_dlist_insert_before(&_timeout_q, node,
						&timeout->node);
			goto inserted;
		}

		*delta -= in_q->delta_ticks_from_prev;
	}

	sys_dlist_append(&_timeout_q, &timeout->node);

inserted:
	K_DEBUG("after adding timeout %p\n", timeout);
	_dump_timeout(timeout, 0);
	_dump_timeout_q();
}

/*
 * Put thread on timeout queue. Record wait queue if any.
 *
 * Cannot handle timeout == 0 and timeout == K_FOREVER.
 *
 * Must be called with interrupts locked.
 */

static inline void _add_thread_timeout(struct k_thread *thread,
				       _wait_q_t *wait_q,
				       int32_t timeout_in_ticks)
{
	_add_timeout(thread, &thread->base.timeout, wait_q, timeout_in_ticks);
}

/* find the closest deadline in the timeout queue */

static inline int32_t _get_next_timeout_expiry(void)
{
	struct _timeout *t = (struct _timeout *)
			     sys_dlist_peek_head(&_timeout_q);

	return t ? t->delta_ticks_from_prev : K_FOREVER;
}

#ifdef __cplusplus
}
#endif

#endif /* _kernel_include_timeout_q__h_ */
