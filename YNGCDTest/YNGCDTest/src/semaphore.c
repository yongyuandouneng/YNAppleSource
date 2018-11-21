/*
 * Copyright (c) 2008-2011 Apple Inc. All rights reserved.
 *
 * @APPLE_APACHE_LICENSE_HEADER_START@
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * @APPLE_APACHE_LICENSE_HEADER_END@
 */

#include "internal.h"

// semaphores are too fundamental to use the dispatch_assume*() macros
#if USE_MACH_SEM
#define DISPATCH_SEMAPHORE_VERIFY_KR(x) do { \
		if (slowpath(x)) { \
			DISPATCH_CRASH("flawed group/semaphore logic"); \
		} \
	} while (0)
#else
#define DISPATCH_SEMAPHORE_VERIFY_RET(x) do { \
		if (slowpath((x) == -1)) { \
			DISPATCH_CRASH("flawed group/semaphore logic"); \
		} \
	} while (0)
#endif

DISPATCH_WEAK // rdar://problem/8503746
long _dispatch_semaphore_signal_slow(dispatch_semaphore_t dsema);

static long _dispatch_group_wake(dispatch_semaphore_t dsema);

#pragma mark -
#pragma mark dispatch_semaphore_t
/// 信号量的构造函数
static void
_dispatch_semaphore_init(long value, dispatch_object_t dou)
{
	dispatch_semaphore_t dsema = dou._dsema;

	dsema->do_next = DISPATCH_OBJECT_LISTLESS;
	dsema->do_targetq = dispatch_get_global_queue(
			DISPATCH_QUEUE_PRIORITY_DEFAULT, 0);
	dsema->dsema_value = value;
	dsema->dsema_orig = value;
#if USE_POSIX_SEM
	int ret = sem_init(&dsema->dsema_sem, 0, 0);
	DISPATCH_SEMAPHORE_VERIFY_RET(ret);
#elif USE_FUTEX_SEM
	int ret = _dispatch_futex_init(&dsema->dsema_futex);
	DISPATCH_SEMAPHORE_VERIFY_RET(ret);
#endif
}

dispatch_semaphore_t
dispatch_semaphore_create(long value)
{
	dispatch_semaphore_t dsema;

	// If the internal value is negative, then the absolute of the value is
	// equal to the number of waiting threads. Therefore it is bogus to
	// initialize the semaphore with a negative value.
	if (value < 0) {
		return NULL;
	}
	/// 创建信号量
	dsema = _dispatch_alloc(DISPATCH_VTABLE(semaphore),
			sizeof(struct dispatch_semaphore_s));
	/// 初始化信号量的 value
	_dispatch_semaphore_init(value, dsema);
	return dsema;
}

#if USE_MACH_SEM
static void
_dispatch_semaphore_create_port(semaphore_t *s4)
{
	kern_return_t kr;
	semaphore_t tmp;

	if (*s4) {
		return;
	}
	_dispatch_safe_fork = false;

	// lazily allocate the semaphore port

	// Someday:
	// 1) Switch to a doubly-linked FIFO in user-space.
	// 2) User-space timers for the timeout.
	// 3) Use the per-thread semaphore port.

	while ((kr = semaphore_create(mach_task_self(), &tmp,
			SYNC_POLICY_FIFO, 0))) {
		DISPATCH_VERIFY_MIG(kr);
		sleep(1);
	}

	if (!dispatch_atomic_cmpxchg(s4, 0, tmp)) {
		kr = semaphore_destroy(mach_task_self(), tmp);
		DISPATCH_SEMAPHORE_VERIFY_KR(kr);
	}
}
#endif

void
_dispatch_semaphore_dispose(dispatch_object_t dou)
{
	dispatch_semaphore_t dsema = dou._dsema;

	if (dsema->dsema_value < dsema->dsema_orig) {
		DISPATCH_CLIENT_CRASH(
				"Semaphore/group object deallocated while in use");
	}

#if USE_MACH_SEM
	kern_return_t kr;
	if (dsema->dsema_port) {
		kr = semaphore_destroy(mach_task_self(), dsema->dsema_port);
		DISPATCH_SEMAPHORE_VERIFY_KR(kr);
	}
	if (dsema->dsema_waiter_port) {
		kr = semaphore_destroy(mach_task_self(), dsema->dsema_waiter_port);
		DISPATCH_SEMAPHORE_VERIFY_KR(kr);
	}
#elif USE_POSIX_SEM
	int ret = sem_destroy(&dsema->dsema_sem);
	DISPATCH_SEMAPHORE_VERIFY_RET(ret);
#elif USE_FUTEX_SEM
	int ret = _dispatch_futex_dispose(&dsema->dsema_futex);
	DISPATCH_SEMAPHORE_VERIFY_RET(ret);
#endif
}

size_t
_dispatch_semaphore_debug(dispatch_object_t dou, char *buf, size_t bufsiz)
{
	dispatch_semaphore_t dsema = dou._dsema;

	size_t offset = 0;
	offset += snprintf(&buf[offset], bufsiz - offset, "%s[%p] = { ",
			dx_kind(dsema), dsema);
	offset += _dispatch_object_debug_attr(dsema, &buf[offset], bufsiz - offset);
#if USE_MACH_SEM
	offset += snprintf(&buf[offset], bufsiz - offset, "port = 0x%u, ",
			dsema->dsema_port);
#endif
	offset += snprintf(&buf[offset], bufsiz - offset,
			"value = %ld, orig = %ld }", dsema->dsema_value, dsema->dsema_orig);
	return offset;
}

DISPATCH_NOINLINE
long
_dispatch_semaphore_signal_slow(dispatch_semaphore_t dsema)
{
	// Before dsema_sent_ksignals is incremented we can rely on the reference
	// held by the waiter. However, once this value is incremented the waiter
	// may return between the atomic increment and the semaphore_signal(),
	// therefore an explicit reference must be held in order to safely access
	// dsema after the atomic increment.
	_dispatch_retain(dsema);

#if USE_MACH_SEM
	(void)dispatch_atomic_inc2o(dsema, dsema_sent_ksignals);
#endif

#if USE_MACH_SEM
	_dispatch_semaphore_create_port(&dsema->dsema_port);
	kern_return_t kr = semaphore_signal(dsema->dsema_port);
	DISPATCH_SEMAPHORE_VERIFY_KR(kr);
#elif USE_POSIX_SEM
	// POSIX semaphore is created in _dispatch_semaphore_init, not lazily.
	int ret = sem_post(&dsema->dsema_sem);
	DISPATCH_SEMAPHORE_VERIFY_RET(ret);
#elif USE_FUTEX_SEM
	// dispatch_futex_t is created in _dispatch_semaphore_init, not lazily.
	int ret = _dispatch_futex_signal(&dsema->dsema_futex);
	DISPATCH_SEMAPHORE_VERIFY_RET(ret);
#endif

	_dispatch_release(dsema);
	return 1;
}
/// dispatch_semaphore_signal 用来唤醒线程
long
dispatch_semaphore_signal(dispatch_semaphore_t dsema)
{
	dispatch_atomic_release_barrier();
	long value = dispatch_atomic_inc2o(dsema, dsema_value);
	if (fastpath(value > 0)) {
		return 0;
	}
	if (slowpath(value == LONG_MIN)) {
		DISPATCH_CLIENT_CRASH("Unbalanced call to dispatch_semaphore_signal()");
	}
	return _dispatch_semaphore_signal_slow(dsema);
}

DISPATCH_NOINLINE
static long
_dispatch_semaphore_wait_slow(dispatch_semaphore_t dsema,
		dispatch_time_t timeout)
{
	long orig;

#if USE_MACH_SEM
	mach_timespec_t _timeout;
	kern_return_t kr;
#elif USE_POSIX_SEM || USE_FUTEX_SEM
	struct timespec _timeout;
	int ret;
#endif
#if USE_MACH_SEM
again:
	// Mach semaphores appear to sometimes spuriously wake up. Therefore,
	// we keep a parallel count of the number of times a Mach semaphore is
	// signaled (6880961).
	while ((orig = dsema->dsema_sent_ksignals)) {
		if (dispatch_atomic_cmpxchg2o(dsema, dsema_sent_ksignals, orig,
				orig - 1)) {
			return 0;
		}
	}
#endif

#if USE_MACH_SEM
	_dispatch_semaphore_create_port(&dsema->dsema_port);
#elif USE_POSIX_SEM || USE_FUTEX_SEM
	// Created in _dispatch_semaphore_init.
#endif

	// From xnu/osfmk/kern/sync_sema.c:
	// wait_semaphore->count = -1; /* we don't keep an actual count */
	//
	// The code above does not match the documentation, and that fact is
	// not surprising. The documented semantics are clumsy to use in any
	// practical way. The above hack effectively tricks the rest of the
	// Mach semaphore logic to behave like the libdispatch algorithm.

	switch (timeout) {
		// 这是我们设置的超时时间入口
	default:
		// 内核提供的 semaphore_timedwait 方法可以指定超时时间。
#if USE_MACH_SEM
		do {
			uint64_t nsec = _dispatch_timeout(timeout);
			_timeout.tv_sec = (typeof(_timeout.tv_sec))(nsec / NSEC_PER_SEC);
			_timeout.tv_nsec = (typeof(_timeout.tv_nsec))(nsec % NSEC_PER_SEC);
			kr = slowpath(semaphore_timedwait(dsema->dsema_port, _timeout));
		} while (kr == KERN_ABORTED);

		if (kr != KERN_OPERATION_TIMED_OUT) {
			DISPATCH_SEMAPHORE_VERIFY_KR(kr);
			break;
		}
#elif USE_POSIX_SEM
		do {
			_timeout = _dispatch_timeout_ts(timeout);
			ret = slowpath(sem_timedwait(&dsema->dsema_sem, &_timeout));
		} while (ret == -1 && errno == EINTR);

		if (!(ret == -1 && errno == ETIMEDOUT)) {
			DISPATCH_SEMAPHORE_VERIFY_RET(ret);
			break;
		}
#elif USE_FUTEX_SEM
		do {
			uint64_t nsec = _dispatch_timeout(timeout);
			_timeout.tv_sec = nsec / NSEC_PER_SEC;
			_timeout.tv_nsec = nsec % NSEC_PER_SEC;
			ret = slowpath(
					_dispatch_futex_wait(&dsema->dsema_futex, &_timeout));
		} while (ret == -1 && errno == EINTR);

		if (!(ret == -1 && errno == ETIMEDOUT)) {
			DISPATCH_SEMAPHORE_VERIFY_RET(ret);
			break;
		}
#endif
		// Fall through and try to undo what the fast path did to
		// dsema->dsema_value
		// 这种情况下会立刻判断 dsema->dsema_value 与 orig 是否相等。如果 while 判断成立，内部的 if 判断一定也成立，此时会将 value 加一(也就是变为 0) 并返回。加一的原因是为了抵消 wait 函数一开始的减一操作。此时函数调用方会得到返回值 KERN_OPERATION_TIMED_OUT，表示由于等待时间超时而返回。
	case DISPATCH_TIME_NOW:
		while ((orig = dsema->dsema_value) < 0) {
			if (dispatch_atomic_cmpxchg2o(dsema, dsema_value, orig, orig + 1)) {
#if USE_MACH_SEM
				return KERN_OPERATION_TIMED_OUT;
#elif USE_POSIX_SEM || USE_FUTEX_SEM
				errno = ETIMEDOUT;
				return -1;
#endif
			}
		}
		// Another thread called semaphore_signal().
		// Fall through and drain the wakeup.
		// 进入 do-while 循环后会调用系统的 semaphore_wait 方法，KERN_ABORTED 表示调用者被一个与信号量系统无关的原因唤醒。因此一旦发生这种情况，还是要继续等待，直到收到 signal 调用。
	case DISPATCH_TIME_FOREVER:
#if USE_MACH_SEM
		do {
			kr = semaphore_wait(dsema->dsema_port);
		} while (kr == KERN_ABORTED);
		DISPATCH_SEMAPHORE_VERIFY_KR(kr);
#elif USE_POSIX_SEM
		do {
			ret = sem_wait(&dsema->dsema_sem);
		} while (ret == -1 && errno == EINTR);
		DISPATCH_SEMAPHORE_VERIFY_RET(ret);
#elif USE_FUTEX_SEM
		do {
			ret = _dispatch_futex_wait(&dsema->dsema_futex, NULL);
		} while (ret == -1 && errno == EINTR);
		DISPATCH_SEMAPHORE_VERIFY_RET(ret);
#endif
		break;
	}
#if USE_MACH_SEM
	goto again;
#else
	return 0;
#endif
}
/// 信号量 等待方法
long
dispatch_semaphore_wait(dispatch_semaphore_t dsema, dispatch_time_t timeout)
{
//	第一行的 dispatch_atomic_dec2o 是一个宏，会调用 GCC 内置的函数 __sync_sub_and_fetch，实现减法的原子性操作。因此这一行的意思是将 dsema 的值减一，并把新的值赋给 value。
//	如果减一后的 value 大于等于 0 就立刻返回，没有任何操作，否则进入等待状态。
	long value = dispatch_atomic_dec2o(dsema, dsema_value);
	dispatch_atomic_acquire_barrier();
	if (fastpath(value >= 0)) {
		return 0;
	}
	/// _dispatch_semaphore_wait_slow 函数针对不同的 timeout 参数，分了三种情况考虑:
	return _dispatch_semaphore_wait_slow(dsema, timeout);
}

#pragma mark -
#pragma mark dispatch_group_t
/// 线程组
dispatch_group_t
dispatch_group_create(void)
{
	dispatch_group_t dg = _dispatch_alloc(DISPATCH_VTABLE(group),
			sizeof(struct dispatch_semaphore_s));
	/// 这里调用了信号量 group 就是一个 value 为 LONG_MAX 的信号量。
	_dispatch_semaphore_init(LONG_MAX, dg);
	return dg;
}

void
dispatch_group_enter(dispatch_group_t dg)
{
	dispatch_semaphore_t dsema = (dispatch_semaphore_t)dg;
	/// 信号量减 1 并且 一直sleep
	(void)dispatch_semaphore_wait(dsema, DISPATCH_TIME_FOREVER);
}

DISPATCH_NOINLINE
static long
_dispatch_group_wake(dispatch_semaphore_t dsema)
{
	struct dispatch_sema_notify_s *next, *head, *tail = NULL;
	long rval;

	head = dispatch_atomic_xchg2o(dsema, dsema_notify_head, NULL);
	if (head) {
		// snapshot before anything is notified/woken <rdar://problem/8554546>
		tail = dispatch_atomic_xchg2o(dsema, dsema_notify_tail, NULL);
	}
	rval = dispatch_atomic_xchg2o(dsema, dsema_group_waiters, 0);
	if (rval) {
		// wake group waiters
#if USE_MACH_SEM
		_dispatch_semaphore_create_port(&dsema->dsema_waiter_port);
		do {
			kern_return_t kr = semaphore_signal(dsema->dsema_waiter_port);
			DISPATCH_SEMAPHORE_VERIFY_KR(kr);
		} while (--rval);
#elif USE_POSIX_SEM
		do {
			int ret = sem_post(&dsema->dsema_sem);
			DISPATCH_SEMAPHORE_VERIFY_RET(ret);
		} while (--rval);
#elif USE_FUTEX_SEM
		do {
			int ret = _dispatch_futex_signal(&dsema->dsema_futex);
			DISPATCH_SEMAPHORE_VERIFY_RET(ret);
		} while (--rval);
#endif
	}
	if (head) {
		// async group notify blocks
		do {
			dispatch_async_f(head->dsn_queue, head->dsn_ctxt, head->dsn_func);
			_dispatch_release(head->dsn_queue);
			next = fastpath(head->dsn_next);
			if (!next && head != tail) {
				while (!(next = fastpath(head->dsn_next))) {
					_dispatch_hardware_pause();
				}
			}
			free(head);
		} while ((head = next));
		_dispatch_release(dsema);
	}
	return 0;
}

void
dispatch_group_leave(dispatch_group_t dg)
{
	dispatch_semaphore_t dsema = (dispatch_semaphore_t)dg;
	dispatch_atomic_release_barrier();
	long value = dispatch_atomic_inc2o(dsema, dsema_value);
	if (slowpath(value == LONG_MIN)) {
		DISPATCH_CLIENT_CRASH("Unbalanced call to dispatch_group_leave()");
	}
	if (slowpath(value == dsema->dsema_orig)) {
		(void)_dispatch_group_wake(dsema);
	}
}

DISPATCH_NOINLINE
static long
_dispatch_group_wait_slow(dispatch_semaphore_t dsema, dispatch_time_t timeout)
{
	long orig;

#if USE_MACH_SEM
	mach_timespec_t _timeout;
	kern_return_t kr;
#elif USE_POSIX_SEM || USE_FUTEX_SEM // KVV
	struct timespec _timeout;
	int ret;
#endif

again:
	// check before we cause another signal to be sent by incrementing
	// dsema->dsema_group_waiters
	if (dsema->dsema_value == dsema->dsema_orig) {
		return _dispatch_group_wake(dsema);
	}
	// Mach semaphores appear to sometimes spuriously wake up. Therefore,
	// we keep a parallel count of the number of times a Mach semaphore is
	// signaled (6880961).
	(void)dispatch_atomic_inc2o(dsema, dsema_group_waiters);
	// check the values again in case we need to wake any threads
	if (dsema->dsema_value == dsema->dsema_orig) {
		return _dispatch_group_wake(dsema);
	}

#if USE_MACH_SEM
	_dispatch_semaphore_create_port(&dsema->dsema_port);
#elif USE_POSIX_SEM || USE_FUTEX_SEM
	// Created in _dispatch_semaphore_init.
#endif

	// From xnu/osfmk/kern/sync_sema.c:
	// wait_semaphore->count = -1; /* we don't keep an actual count */
	//
	// The code above does not match the documentation, and that fact is
	// not surprising. The documented semantics are clumsy to use in any
	// practical way. The above hack effectively tricks the rest of the
	// Mach semaphore logic to behave like the libdispatch algorithm.

	switch (timeout) {
	default:
#if USE_MACH_SEM
		do {
			uint64_t nsec = _dispatch_timeout(timeout);
			_timeout.tv_sec = (typeof(_timeout.tv_sec))(nsec / NSEC_PER_SEC);
			_timeout.tv_nsec = (typeof(_timeout.tv_nsec))(nsec % NSEC_PER_SEC);
			kr = slowpath(semaphore_timedwait(dsema->dsema_waiter_port,
					_timeout));
		} while (kr == KERN_ABORTED);

		if (kr != KERN_OPERATION_TIMED_OUT) {
			DISPATCH_SEMAPHORE_VERIFY_KR(kr);
			break;
		}
#elif USE_POSIX_SEM
		do {
			_timeout = _dispatch_timeout_ts(timeout);
			ret = slowpath(sem_timedwait(&dsema->dsema_sem, &_timeout));
		} while (ret == -1 && errno == EINTR);

		if (!(ret == -1 && errno == ETIMEDOUT)) {
			DISPATCH_SEMAPHORE_VERIFY_RET(ret);
			break;
		}
#elif USE_FUTEX_SEM
		do {
			uint64_t nsec = _dispatch_timeout(timeout);
			_timeout.tv_sec = nsec / NSEC_PER_SEC;
			_timeout.tv_nsec = nsec % NSEC_PER_SEC;
			ret = slowpath(
					_dispatch_futex_wait(&dsema->dsema_futex, &_timeout));
		} while (ret == -1 && errno == EINTR);

		if (!(ret == -1 && errno == ETIMEDOUT)) {
			DISPATCH_SEMAPHORE_VERIFY_RET(ret);
			break;
		}
#endif
		// Fall through and try to undo the earlier change to
		// dsema->dsema_group_waiters
	case DISPATCH_TIME_NOW:
		while ((orig = dsema->dsema_group_waiters)) {
			if (dispatch_atomic_cmpxchg2o(dsema, dsema_group_waiters, orig,
					orig - 1)) {
#if USE_MACH_SEM
				return KERN_OPERATION_TIMED_OUT;
#elif USE_POSIX_SEM || USE_FUTEX_SEM
				errno = ETIMEDOUT;
				return -1;
#endif
			}
		}
		// Another thread called semaphore_signal().
		// Fall through and drain the wakeup.
	case DISPATCH_TIME_FOREVER:
#if USE_MACH_SEM
		do {
			kr = semaphore_wait(dsema->dsema_waiter_port);
		} while (kr == KERN_ABORTED);
		DISPATCH_SEMAPHORE_VERIFY_KR(kr);
#elif USE_POSIX_SEM
		do {
			ret = sem_wait(&dsema->dsema_sem);
		} while (ret == -1 && errno == EINTR);
		DISPATCH_SEMAPHORE_VERIFY_RET(ret);
#elif USE_FUTEX_SEM
		do {
			ret = _dispatch_futex_wait(&dsema->dsema_futex, NULL);
		} while (ret == -1 && errno == EINTR);
		DISPATCH_SEMAPHORE_VERIFY_RET(ret);
#endif
		break;
	}

	goto again;
}

long
dispatch_group_wait(dispatch_group_t dg, dispatch_time_t timeout)
{
	dispatch_semaphore_t dsema = (dispatch_semaphore_t)dg;

	if (dsema->dsema_value == dsema->dsema_orig) {
		return 0;
	}
	if (timeout == 0) {
#if USE_MACH_SEM
		return KERN_OPERATION_TIMED_OUT;
#elif USE_POSIX_SEM || USE_FUTEX_SEM
		errno = ETIMEDOUT;
		return (-1);
#endif
	}
	return _dispatch_group_wait_slow(dsema, timeout);
}

DISPATCH_NOINLINE
void
dispatch_group_notify_f(dispatch_group_t dg, dispatch_queue_t dq, void *ctxt,
		void (*func)(void *))
{
	dispatch_semaphore_t dsema = (dispatch_semaphore_t)dg;
	struct dispatch_sema_notify_s *dsn, *prev;

	// FIXME -- this should be updated to use the continuation cache
	while (!(dsn = calloc(1, sizeof(*dsn)))) {
		sleep(1);
	}

	dsn->dsn_queue = dq;
	dsn->dsn_ctxt = ctxt;
	dsn->dsn_func = func;
	_dispatch_retain(dq);
	dispatch_atomic_store_barrier();
	prev = dispatch_atomic_xchg2o(dsema, dsema_notify_tail, dsn);
	if (fastpath(prev)) {
		prev->dsn_next = dsn;
	} else {
		_dispatch_retain(dg);
		(void)dispatch_atomic_xchg2o(dsema, dsema_notify_head, dsn);
		if (dsema->dsema_value == dsema->dsema_orig) {
			_dispatch_group_wake(dsema);
		}
	}
}

#ifdef __BLOCKS__
/// 收到通知
void
dispatch_group_notify(dispatch_group_t dg, dispatch_queue_t dq,
		dispatch_block_t db)
{
	dispatch_group_notify_f(dg, dq, _dispatch_Block_copy(db),
			_dispatch_call_block_and_release);
}
#endif

#pragma mark -
#pragma mark _dispatch_thread_semaphore_t

DISPATCH_NOINLINE
static _dispatch_thread_semaphore_t
_dispatch_thread_semaphore_create(void)
{
	_dispatch_safe_fork = false;
#if USE_MACH_SEM
	semaphore_t s4;
	kern_return_t kr;
	while (slowpath(kr = semaphore_create(mach_task_self(), &s4,
			SYNC_POLICY_FIFO, 0))) {
		DISPATCH_VERIFY_MIG(kr);
		sleep(1);
	}
	return s4;
#elif USE_POSIX_SEM
	sem_t *s4 = malloc(sizeof(*s4));
	int ret = sem_init(s4, 0, 0);
	DISPATCH_SEMAPHORE_VERIFY_RET(ret);
	return (_dispatch_thread_semaphore_t) s4;
#elif USE_FUTEX_SEM
	dispatch_futex_t *futex = malloc(sizeof(*futex));
	int ret = _dispatch_futex_init(futex);
	DISPATCH_SEMAPHORE_VERIFY_RET(ret);
	return (_dispatch_thread_semaphore_t)futex;
#endif
}

DISPATCH_NOINLINE
void
_dispatch_thread_semaphore_dispose(_dispatch_thread_semaphore_t sema)
{
#if USE_MACH_SEM
	semaphore_t s4 = (semaphore_t)sema;
	kern_return_t kr = semaphore_destroy(mach_task_self(), s4);
	DISPATCH_SEMAPHORE_VERIFY_KR(kr);
#elif USE_POSIX_SEM
	int ret = sem_destroy((sem_t *)sema);
	free((sem_t *) sema);
	DISPATCH_SEMAPHORE_VERIFY_RET(ret);
#elif USE_FUTEX_SEM
	int ret = _dispatch_futex_dispose((dispatch_futex_t *)sema);
	free((dispatch_futex_t *)sema);
	DISPATCH_SEMAPHORE_VERIFY_RET(ret);
#endif
}

void
_dispatch_thread_semaphore_signal(_dispatch_thread_semaphore_t sema)
{
#if USE_MACH_SEM
	semaphore_t s4 = (semaphore_t)sema;
	kern_return_t kr = semaphore_signal(s4);
	DISPATCH_SEMAPHORE_VERIFY_KR(kr);
#elif USE_POSIX_SEM
	int ret = sem_post((sem_t *)sema);
	DISPATCH_SEMAPHORE_VERIFY_RET(ret);
#elif USE_FUTEX_SEM
	int ret = _dispatch_futex_signal((dispatch_futex_t *)sema);
	DISPATCH_SEMAPHORE_VERIFY_RET(ret);
#endif
}

void
_dispatch_thread_semaphore_wait(_dispatch_thread_semaphore_t sema)
{
#if USE_MACH_SEM
	semaphore_t s4 = (semaphore_t)sema;
	kern_return_t kr;
	do {
		kr = semaphore_wait(s4);
	} while (slowpath(kr == KERN_ABORTED));
	DISPATCH_SEMAPHORE_VERIFY_KR(kr);
#elif USE_POSIX_SEM
	int ret;
	do {
		ret = sem_wait((sem_t *) sema);
	} while (slowpath(ret == -1 && errno == EINTR));
	DISPATCH_SEMAPHORE_VERIFY_RET(ret);
#elif USE_FUTEX_SEM
	int ret;
	do {
		ret = _dispatch_futex_wait((dispatch_futex_t *)sema, NULL);
	} while (slowpath(ret == -1 && errno == EINTR));
	DISPATCH_SEMAPHORE_VERIFY_RET(ret);
#endif
}

_dispatch_thread_semaphore_t
_dispatch_get_thread_semaphore(void)
{
	_dispatch_thread_semaphore_t sema = (_dispatch_thread_semaphore_t)
			_dispatch_thread_getspecific(dispatch_sema4_key);
	if (slowpath(!sema)) {
		return _dispatch_thread_semaphore_create();
	}
	_dispatch_thread_setspecific(dispatch_sema4_key, NULL);
	return sema;
}

void
_dispatch_put_thread_semaphore(_dispatch_thread_semaphore_t sema)
{
	_dispatch_thread_semaphore_t old_sema = (_dispatch_thread_semaphore_t)
			_dispatch_thread_getspecific(dispatch_sema4_key);
	_dispatch_thread_setspecific(dispatch_sema4_key, (void*)sema);
	if (slowpath(old_sema)) {
		return _dispatch_thread_semaphore_dispose(old_sema);
	}
}

#if USE_FUTEX_SEM
#define DISPATCH_FUTEX_NUM_SPINS 100
#define DISPATCH_FUTEX_VALUE_MAX INT_MAX
#define DISPATCH_FUTEX_NWAITERS_SHIFT 32
#define DISPATCH_FUTEX_VALUE_MASK ((1ull << DISPATCH_FUTEX_NWAITERS_SHIFT) - 1)

static int _dispatch_futex_trywait(dispatch_futex_t *dfx);
static int _dispatch_futex_wait_slow(dispatch_futex_t *dfx,
		const struct timespec* timeout);
static int _dispatch_futex_syscall(dispatch_futex_t *dfx, int op, int val,
		const struct timespec *timeout);

int
_dispatch_futex_init(dispatch_futex_t *dfx)
{
	dfx->dfx_data = 0;
	return 0;
}

int
_dispatch_futex_dispose(dispatch_futex_t *dfx)
{
	(void)dfx;
	return 0;
}

// Increments semaphore value and reads wait count in a single atomic
// operation, with release MO. If wait count is nonzero, issues a FUTEX_WAKE.
int
_dispatch_futex_signal(dispatch_futex_t *dfx)
{
	uint64_t orig;
	do {
		orig = dfx->dfx_data;
		if (slowpath((orig & DISPATCH_FUTEX_VALUE_MASK) ==
					 DISPATCH_FUTEX_VALUE_MAX)) {
			DISPATCH_CRASH("semaphore overflow");
		}
	} while (!dispatch_atomic_cmpxchg2o(dfx, dfx_data, orig, orig + 1));
	if (slowpath(orig >> DISPATCH_FUTEX_NWAITERS_SHIFT)) {
		int ret = _dispatch_futex_syscall(dfx, FUTEX_WAKE, 1, NULL);
		DISPATCH_SEMAPHORE_VERIFY_RET(ret);
	}
	return 0;
}

// `timeout` is relative, and can be NULL for an infinite timeout--see futex(2).
int
_dispatch_futex_wait(dispatch_futex_t *dfx, const struct timespec *timeout)
{
	if (fastpath(!_dispatch_futex_trywait(dfx))) {
		return 0;
	}
	return _dispatch_futex_wait_slow(dfx, timeout);
}

// Atomic-decrement-if-positive on the semaphore value, with acquire MO if
// successful.
int
_dispatch_futex_trywait(dispatch_futex_t *dfx)
{
	uint64_t orig;
	while ((orig = dfx->dfx_data) & DISPATCH_FUTEX_VALUE_MASK) {
		if (dispatch_atomic_cmpxchg2o(dfx, dfx_data, orig, orig - 1)) {
			return 0;
		}
	}
	return -1;
}

DISPATCH_NOINLINE
static int
_dispatch_futex_wait_slow(dispatch_futex_t *dfx, const struct timespec *timeout)
{
	int spins = DISPATCH_FUTEX_NUM_SPINS;
	// Spin for a short time (if there are no waiters).
	while (spins-- && !dfx->dfx_data) {
		_dispatch_hardware_pause();
	}
	while (_dispatch_futex_trywait(dfx)) {
		dispatch_atomic_add2o(dfx, dfx_data,
							  1ull << DISPATCH_FUTEX_NWAITERS_SHIFT);
		int ret = _dispatch_futex_syscall(dfx, FUTEX_WAIT, 0, timeout);
		dispatch_atomic_sub2o(dfx, dfx_data,
							  1ull << DISPATCH_FUTEX_NWAITERS_SHIFT);
		switch (ret == -1 ? errno : 0) {
		case EWOULDBLOCK:
			if (!timeout) {
				break;
			} else if (!_dispatch_futex_trywait(dfx)) {
				return 0;
			} else {
				// Caller must recompute timeout.
				errno = EINTR;
				return -1;
			}
		case EINTR:
		case ETIMEDOUT:
			return -1;
		default:
			DISPATCH_SEMAPHORE_VERIFY_RET(ret);
			break;
		}
	}
	return 0;
}

static int
_dispatch_futex_syscall(
		dispatch_futex_t *dfx, int op, int val, const struct timespec *timeout)
{
#ifdef DISPATCH_LITTLE_ENDIAN
	int *addr = (int *)&dfx->dfx_data;
#elif DISPATCH_BIG_ENDIAN
	int *addr = (int *)&dfx->dfx_data + 1;
#endif
	return (int)syscall(SYS_futex, addr, op | FUTEX_PRIVATE_FLAG, val, timeout,
						NULL, 0);
}
#endif /* USE_FUTEX_SEM */
