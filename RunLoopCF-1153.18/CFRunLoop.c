/*
 * Copyright (c) 2015 Apple Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 *
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this
 * file.
 *
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 *
 * @APPLE_LICENSE_HEADER_END@
 */

/*	CFRunLoop.c
 Copyright (c) 1998-2014, Apple Inc. All rights reserved.
 Responsibility: Tony Parker
 */

#include <CoreFoundation/CFRunLoop.h>
#include <CoreFoundation/CFSet.h>
#include <CoreFoundation/CFBag.h>
#include <CoreFoundation/CFNumber.h>
#include <CoreFoundation/CFPreferences.h>
#include "CFInternal.h"
#include <math.h>
#include <stdio.h>
#include <limits.h>
#include <pthread.h>
#include <dispatch/dispatch.h>


#if DEPLOYMENT_TARGET_WINDOWS
#include <typeinfo.h>
#endif
#include <checkint.h>

#if DEPLOYMENT_TARGET_MACOSX || DEPLOYMENT_TARGET_EMBEDDED || DEPLOYMENT_TARGET_EMBEDDED_MINI
#include <sys/param.h>
#include <dispatch/private.h>
#include <CoreFoundation/CFUserNotification.h>
#include <mach/mach.h>
#include <mach/clock_types.h>
#include <mach/clock.h>
#include <unistd.h>
#include <dlfcn.h>
#include <pthread/private.h>
#include <os/voucher_private.h>
extern mach_port_t _dispatch_get_main_queue_port_4CF(void);
extern void _dispatch_main_queue_callback_4CF(mach_msg_header_t *msg);
#elif DEPLOYMENT_TARGET_WINDOWS
#include <process.h>
DISPATCH_EXPORT HANDLE _dispatch_get_main_queue_handle_4CF(void);
DISPATCH_EXPORT void _dispatch_main_queue_callback_4CF(void);

#define MACH_PORT_NULL 0
#define mach_port_name_t HANDLE
#define mach_port_t HANDLE
#define _dispatch_get_main_queue_port_4CF _dispatch_get_main_queue_handle_4CF
#define _dispatch_main_queue_callback_4CF(x) _dispatch_main_queue_callback_4CF()

#define AbsoluteTime LARGE_INTEGER 

#endif

#if DEPLOYMENT_TARGET_WINDOWS || DEPLOYMENT_TARGET_IPHONESIMULATOR
CF_EXPORT pthread_t _CF_pthread_main_thread_np(void);
#define pthread_main_thread_np() _CF_pthread_main_thread_np()
#endif

#include <Block.h>
#include <Block_private.h>

#if DEPLOYMENT_TARGET_MACOSX
#define USE_DISPATCH_SOURCE_FOR_TIMERS 1
#define USE_MK_TIMER_TOO 1
#else
#define USE_DISPATCH_SOURCE_FOR_TIMERS 0
#define USE_MK_TIMER_TOO 1
#endif


static int _LogCFRunLoop = 0;
static void _runLoopTimerWithBlockContext(CFRunLoopTimerRef timer, void *opaqueBlock);

// for conservative arithmetic safety, such that (TIMER_DATE_LIMIT + TIMER_INTERVAL_LIMIT + kCFAbsoluteTimeIntervalSince1970) * 10^9 < 2^63
#define TIMER_DATE_LIMIT	4039289856.0
#define TIMER_INTERVAL_LIMIT	504911232.0

#define HANDLE_DISPATCH_ON_BASE_INVOCATION_ONLY 0

#define CRASH(string, errcode) do { char msg[256]; snprintf(msg, 256, string, errcode); CRSetCrashLogMessage(msg); HALT; } while (0)

#if DEPLOYMENT_TARGET_WINDOWS

static pthread_t kNilPthreadT = { nil, nil };
#define pthreadPointer(a) a.p
typedef	int kern_return_t;
#define KERN_SUCCESS 0

#else

static pthread_t kNilPthreadT = (pthread_t)0;
#define pthreadPointer(a) a
#define lockCount(a) a
#endif

#pragma mark -

#define CF_RUN_LOOP_PROBES 0

#if CF_RUN_LOOP_PROBES
#include "CFRunLoopProbes.h"
#else
#define	CFRUNLOOP_NEXT_TIMER_ARMED(arg0) do { } while (0)
#define	CFRUNLOOP_NEXT_TIMER_ARMED_ENABLED() (0)
#define	CFRUNLOOP_POLL() do { } while (0)
#define	CFRUNLOOP_POLL_ENABLED() (0)
#define	CFRUNLOOP_SLEEP() do { } while (0)
#define	CFRUNLOOP_SLEEP_ENABLED() (0)
#define	CFRUNLOOP_SOURCE_FIRED(arg0, arg1, arg2) do { } while (0)
#define	CFRUNLOOP_SOURCE_FIRED_ENABLED() (0)
#define	CFRUNLOOP_TIMER_CREATED(arg0, arg1, arg2, arg3, arg4, arg5, arg6) do { } while (0)
#define	CFRUNLOOP_TIMER_CREATED_ENABLED() (0)
#define	CFRUNLOOP_TIMER_FIRED(arg0, arg1, arg2, arg3, arg4) do { } while (0)
#define	CFRUNLOOP_TIMER_FIRED_ENABLED() (0)
#define	CFRUNLOOP_TIMER_RESCHEDULED(arg0, arg1, arg2, arg3, arg4, arg5) do { } while (0)
#define	CFRUNLOOP_TIMER_RESCHEDULED_ENABLED() (0)
#define	CFRUNLOOP_WAKEUP(arg0) do { } while (0)
#define	CFRUNLOOP_WAKEUP_ENABLED() (0)
#define	CFRUNLOOP_WAKEUP_FOR_DISPATCH() do { } while (0)
#define	CFRUNLOOP_WAKEUP_FOR_DISPATCH_ENABLED() (0)
#define	CFRUNLOOP_WAKEUP_FOR_NOTHING() do { } while (0)
#define	CFRUNLOOP_WAKEUP_FOR_NOTHING_ENABLED() (0)
#define	CFRUNLOOP_WAKEUP_FOR_SOURCE() do { } while (0)
#define	CFRUNLOOP_WAKEUP_FOR_SOURCE_ENABLED() (0)
#define	CFRUNLOOP_WAKEUP_FOR_TIMEOUT() do { } while (0)
#define	CFRUNLOOP_WAKEUP_FOR_TIMEOUT_ENABLED() (0)
#define	CFRUNLOOP_WAKEUP_FOR_TIMER() do { } while (0)
#define	CFRUNLOOP_WAKEUP_FOR_TIMER_ENABLED() (0)
#define	CFRUNLOOP_WAKEUP_FOR_WAKEUP() do { } while (0)
#define	CFRUNLOOP_WAKEUP_FOR_WAKEUP_ENABLED() (0)
#endif

// In order to reuse most of the code across Mach and Windows v1 RunLoopSources, we define a
// simple abstraction layer spanning Mach ports and Windows HANDLES
#if DEPLOYMENT_TARGET_MACOSX || DEPLOYMENT_TARGET_EMBEDDED || DEPLOYMENT_TARGET_EMBEDDED_MINI
/// 获取进程的Port总数
CF_PRIVATE uint32_t __CFGetProcessPortCount(void) {
    ipc_info_space_t info;
    ipc_info_name_array_t table = 0;
    mach_msg_type_number_t tableCount = 0;
    ipc_info_tree_name_array_t tree = 0;
    mach_msg_type_number_t treeCount = 0;
    
    kern_return_t ret = mach_port_space_info(mach_task_self(), &info, &table, &tableCount, &tree, &treeCount);
    if (ret != KERN_SUCCESS) {
        return (uint32_t)0;
    }
    if (table != NULL) {
        ret = vm_deallocate(mach_task_self(), (vm_address_t)table, tableCount * sizeof(*table));
    }
    if (tree != NULL) {
        ret = vm_deallocate(mach_task_self(), (vm_address_t)tree, treeCount * sizeof(*tree));
    }
    return (uint32_t)tableCount;
}
/// 停止所有的Thread
CF_PRIVATE CFArrayRef __CFStopAllThreads(void) {
    CFMutableArrayRef suspended_list = CFArrayCreateMutable(kCFAllocatorSystemDefault, 0, NULL);
    mach_port_t my_task = mach_task_self();
    mach_port_t my_thread = mach_thread_self();
    thread_act_array_t thr_list = 0;
    mach_msg_type_number_t thr_cnt = 0;
    
    // really, should loop doing the stopping until no more threads get added to the list N times in a row
    kern_return_t ret = task_threads(my_task, &thr_list, &thr_cnt);
    if (ret == KERN_SUCCESS) {
        for (CFIndex idx = 0; idx < thr_cnt; idx++) {
            thread_act_t thread = thr_list[idx];
            if (thread == my_thread) continue;
            if (CFArrayContainsValue(suspended_list, CFRangeMake(0, CFArrayGetCount(suspended_list)), (const void *)(uintptr_t)thread)) continue;
            ret = thread_suspend(thread);
            if (ret == KERN_SUCCESS) {
                CFArrayAppendValue(suspended_list, (const void *)(uintptr_t)thread);
            } else {
                mach_port_deallocate(my_task, thread);
            }
        }
        vm_deallocate(my_task, (vm_address_t)thr_list, sizeof(thread_t) * thr_cnt);
    }
    mach_port_deallocate(my_task, my_thread);
    return suspended_list;
}
/// 重启所有的Threads
CF_PRIVATE void __CFRestartAllThreads(CFArrayRef threads) {
    for (CFIndex idx = 0; idx < CFArrayGetCount(threads); idx++) {
        thread_act_t thread = (thread_act_t)(uintptr_t)CFArrayGetValueAtIndex(threads, idx);
        kern_return_t ret = thread_resume(thread);
        if (ret != KERN_SUCCESS) CRASH("*** Failure from thread_resume (%d) ***", ret);
        mach_port_deallocate(mach_task_self(), thread);
    }
}

static uint32_t __CF_last_warned_port_count = 0;

static void foo() __attribute__((unused));
static void foo() {
    uint32_t pcnt = __CFGetProcessPortCount();
    if (__CF_last_warned_port_count + 1000 < pcnt) {
        CFArrayRef threads = __CFStopAllThreads();
        
        
        // do stuff here
        CFOptionFlags responseFlags = 0;
        SInt32 result = CFUserNotificationDisplayAlert(0.0, kCFUserNotificationCautionAlertLevel, NULL, NULL, NULL, CFSTR("High Mach Port Usage"), CFSTR("This application is using a lot of Mach ports."), CFSTR("Default"), CFSTR("Altern"), CFSTR("Other b"), &responseFlags);
        if (0 != result) {
            CFLog(3, CFSTR("ERROR"));
        } else {
            switch (responseFlags) {
                case kCFUserNotificationDefaultResponse: CFLog(3, CFSTR("DefaultR")); break;
                case kCFUserNotificationAlternateResponse: CFLog(3, CFSTR("AltR")); break;
                case kCFUserNotificationOtherResponse: CFLog(3, CFSTR("OtherR")); break;
                case kCFUserNotificationCancelResponse: CFLog(3, CFSTR("CancelR")); break;
            }
        }
        
        
        __CFRestartAllThreads(threads);
        CFRelease(threads);
        __CF_last_warned_port_count = pcnt;
    }
}


typedef mach_port_t __CFPort;
#define CFPORT_NULL MACH_PORT_NULL
typedef mach_port_t __CFPortSet;

static void __THE_SYSTEM_HAS_NO_PORTS_AVAILABLE__(kern_return_t ret) __attribute__((noinline));
static void __THE_SYSTEM_HAS_NO_PORTS_AVAILABLE__(kern_return_t ret) { HALT; };
/// 初始化port
static __CFPort __CFPortAllocate(void) {
    __CFPort result = CFPORT_NULL;
    kern_return_t ret = mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &result);
    if (KERN_SUCCESS != ret) {
        char msg[256];
        snprintf(msg, 256, "*** The system has no mach ports available. You may be able to diagnose which application(s) are using ports by using 'top' or Activity Monitor. (%d) ***", ret);
        CRSetCrashLogMessage(msg); 
        __THE_SYSTEM_HAS_NO_PORTS_AVAILABLE__(ret); 
        return CFPORT_NULL;
    }
    
    ret = mach_port_insert_right(mach_task_self(), result, result, MACH_MSG_TYPE_MAKE_SEND);
    if (KERN_SUCCESS != ret) CRASH("*** Unable to set send right on mach port. (%d) ***", ret);
    
    
    mach_port_limits_t limits;
    limits.mpl_qlimit = 1;
    ret = mach_port_set_attributes(mach_task_self(), result, MACH_PORT_LIMITS_INFO, (mach_port_info_t)&limits, MACH_PORT_LIMITS_INFO_COUNT);
    if (KERN_SUCCESS != ret) CRASH("*** Unable to set attributes on mach port. (%d) ***", ret);
    
    return result;
}
/// 释放Port
CF_INLINE void __CFPortFree(__CFPort port) {
    mach_port_destroy(mach_task_self(), port);
}

static void __THE_SYSTEM_HAS_NO_PORT_SETS_AVAILABLE__(kern_return_t ret) __attribute__((noinline));
static void __THE_SYSTEM_HAS_NO_PORT_SETS_AVAILABLE__(kern_return_t ret) { HALT; };
/// 初始化Port Set 集合
CF_INLINE __CFPortSet __CFPortSetAllocate(void) {
    __CFPortSet result;
    kern_return_t ret = mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_PORT_SET, &result);
    if (KERN_SUCCESS != ret) { __THE_SYSTEM_HAS_NO_PORT_SETS_AVAILABLE__(ret); }
    return (KERN_SUCCESS == ret) ? result : CFPORT_NULL;
}
/// 往Port Set 里面插入一个port
CF_INLINE kern_return_t __CFPortSetInsert(__CFPort port, __CFPortSet portSet) {
    if (MACH_PORT_NULL == port) {
        return -1;
    }
    return mach_port_insert_member(mach_task_self(), port, portSet);
}
/// 从Port Set 里面移除一个port
CF_INLINE kern_return_t __CFPortSetRemove(__CFPort port, __CFPortSet portSet) {
    if (MACH_PORT_NULL == port) {
        return -1;
    }
    return mach_port_extract_member(mach_task_self(), port, portSet);
}
/// 释放 Port Set
CF_INLINE void __CFPortSetFree(__CFPortSet portSet) {
    kern_return_t ret;
    mach_port_name_array_t array;
    mach_msg_type_number_t idx, number;
    
    ret = mach_port_get_set_status(mach_task_self(), portSet, &array, &number);
    if (KERN_SUCCESS == ret) {
        for (idx = 0; idx < number; idx++) {
            mach_port_extract_member(mach_task_self(), array[idx], portSet);
        }
        vm_deallocate(mach_task_self(), (vm_address_t)array, number * sizeof(mach_port_name_t));
    }
    mach_port_destroy(mach_task_self(), portSet);
}

#elif DEPLOYMENT_TARGET_WINDOWS

typedef HANDLE __CFPort;
#define CFPORT_NULL NULL

// A simple dynamic array of HANDLEs, which grows to a high-water mark
/// Port Set 结构体
typedef struct ___CFPortSet {
    uint16_t	used;
    uint16_t	size;
    HANDLE	*handles;
    CFLock_t lock;		// insert and remove must be thread safe, like the Mach calls
} *__CFPortSet;

CF_INLINE __CFPort __CFPortAllocate(void) {
    return CreateEventA(NULL, true, false, NULL);
}

CF_INLINE void __CFPortFree(__CFPort port) {
    CloseHandle(port);
}

static __CFPortSet __CFPortSetAllocate(void) {
    __CFPortSet result = (__CFPortSet)CFAllocatorAllocate(kCFAllocatorSystemDefault, sizeof(struct ___CFPortSet), 0);
    result->used = 0;
    result->size = 4;
    result->handles = (HANDLE *)CFAllocatorAllocate(kCFAllocatorSystemDefault, result->size * sizeof(HANDLE), 0);
    CF_SPINLOCK_INIT_FOR_STRUCTS(result->lock);
    return result;
}

static void __CFPortSetFree(__CFPortSet portSet) {
    CFAllocatorDeallocate(kCFAllocatorSystemDefault, portSet->handles);
    CFAllocatorDeallocate(kCFAllocatorSystemDefault, portSet);
}

// Returns portBuf if ports fit in that space, else returns another ptr that must be freed
static __CFPort *__CFPortSetGetPorts(__CFPortSet portSet, __CFPort *portBuf, uint32_t bufSize, uint32_t *portsUsed) {
    __CFLock(&(portSet->lock));
    __CFPort *result = portBuf;
    if (bufSize < portSet->used)
        result = (__CFPort *)CFAllocatorAllocate(kCFAllocatorSystemDefault, portSet->used * sizeof(HANDLE), 0);
    if (portSet->used > 1) {
        // rotate the ports to vaguely simulate round-robin behaviour
        uint16_t lastPort = portSet->used - 1;
        HANDLE swapHandle = portSet->handles[0];
        memmove(portSet->handles, &portSet->handles[1], lastPort * sizeof(HANDLE));
        portSet->handles[lastPort] = swapHandle;
    }
    memmove(result, portSet->handles, portSet->used * sizeof(HANDLE));
    *portsUsed = portSet->used;
    __CFUnlock(&(portSet->lock));
    return result;
}

static kern_return_t __CFPortSetInsert(__CFPort port, __CFPortSet portSet) {
    if (NULL == port) {
        return -1;
    }
    __CFLock(&(portSet->lock));
    if (portSet->used >= portSet->size) {
        portSet->size += 4;
        portSet->handles = (HANDLE *)CFAllocatorReallocate(kCFAllocatorSystemDefault, portSet->handles, portSet->size * sizeof(HANDLE), 0);
    }
    if (portSet->used >= MAXIMUM_WAIT_OBJECTS) {
        CFLog(kCFLogLevelWarning, CFSTR("*** More than MAXIMUM_WAIT_OBJECTS (%d) ports add to a port set.  The last ones will be ignored."), MAXIMUM_WAIT_OBJECTS);
    }
    portSet->handles[portSet->used++] = port;
    __CFUnlock(&(portSet->lock));
    return KERN_SUCCESS;
}

static kern_return_t __CFPortSetRemove(__CFPort port, __CFPortSet portSet) {
    int i, j;
    if (NULL == port) {
        return -1;
    }
    __CFLock(&(portSet->lock));
    for (i = 0; i < portSet->used; i++) {
        if (portSet->handles[i] == port) {
            for (j = i+1; j < portSet->used; j++) {
                portSet->handles[j-1] = portSet->handles[j];
            }
            portSet->used--;
            __CFUnlock(&(portSet->lock));
            return true;
        }
    }
    __CFUnlock(&(portSet->lock));
    return KERN_SUCCESS;
}

#endif

#if !defined(__MACTYPES__) && !defined(_OS_OSTYPES_H)
#if defined(__BIG_ENDIAN__)
typedef	struct UnsignedWide {
    UInt32		hi;
    UInt32		lo;
} UnsignedWide;
#elif defined(__LITTLE_ENDIAN__)
typedef	struct UnsignedWide {
    UInt32		lo;
    UInt32		hi;
} UnsignedWide;
#endif
typedef UnsignedWide		AbsoluteTime;
#endif

/// MK_TIMER 创建和操作
#if USE_MK_TIMER_TOO
extern mach_port_name_t mk_timer_create(void);
extern kern_return_t mk_timer_destroy(mach_port_name_t name);
extern kern_return_t mk_timer_arm(mach_port_name_t name, AbsoluteTime expire_time);
extern kern_return_t mk_timer_cancel(mach_port_name_t name, AbsoluteTime *result_time);

CF_INLINE AbsoluteTime __CFUInt64ToAbsoluteTime(uint64_t x) {
    AbsoluteTime a;
    a.hi = x >> 32;
    a.lo = x & (uint64_t)0xFFFFFFFF;
    return a;
}
#endif
#pragma mark - 唤醒RunLoop mack_prot核心代码
static uint32_t __CFSendTrivialMachMessage(mach_port_t port, uint32_t msg_id, CFOptionFlags options, uint32_t timeout) {
    kern_return_t result;
    mach_msg_header_t header;
    header.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, 0);
    header.msgh_size = sizeof(mach_msg_header_t);
    header.msgh_remote_port = port;
    header.msgh_local_port = MACH_PORT_NULL;
    header.msgh_id = msg_id;
    /// 调用mach_msg发送端口消息
    result = mach_msg(&header, MACH_SEND_MSG|options, header.msgh_size, 0, MACH_PORT_NULL, timeout, MACH_PORT_NULL);
    if (result == MACH_SEND_TIMED_OUT) mach_msg_destroy(&header);
    return result;
}



/* unlock a run loop and modes before doing callouts/sleeping */
/* never try to take the run loop lock with a mode locked */
/* be very careful of common subexpression elimination and compacting code, particular across locks and unlocks! */
/* run loop mode structures should never be deallocated, even if they become empty */

static CFTypeID __kCFRunLoopModeTypeID = _kCFRuntimeNotATypeID;
static CFTypeID __kCFRunLoopTypeID = _kCFRuntimeNotATypeID;
static CFTypeID __kCFRunLoopSourceTypeID = _kCFRuntimeNotATypeID;
static CFTypeID __kCFRunLoopObserverTypeID = _kCFRuntimeNotATypeID;
static CFTypeID __kCFRunLoopTimerTypeID = _kCFRuntimeNotATypeID;

/// RunLoop Mode 结构体
typedef struct __CFRunLoopMode *CFRunLoopModeRef;

#pragma mark - __CFRunLoopMode 结构体
struct __CFRunLoopMode {
    CFRuntimeBase _base;
    // pthread_metext_t 锁
    pthread_mutex_t _lock;	/* must have the run loop locked before locking this */
    // mode 名称
    CFStringRef _name;
    Boolean _stopped;
    char _padding[3];
    // sources0
    CFMutableSetRef _sources0;
    // sources1
    CFMutableSetRef _sources1;
    // observers
    CFMutableArrayRef _observers;
    // timers
    CFMutableArrayRef _timers;
    //
    CFMutableDictionaryRef _portToV1SourceMap;
    __CFPortSet _portSet;
    CFIndex _observerMask;
    
/// GCD_TIMERS_SOURCE
#if USE_DISPATCH_SOURCE_FOR_TIMERS
    dispatch_source_t _timerSource;
    dispatch_queue_t _queue;
    Boolean _timerFired; // set to true by the source when a timer has fired
    Boolean _dispatchTimerArmed;
#endif
/// MK_TIMER
#if USE_MK_TIMER_TOO
    mach_port_t _timerPort;
    Boolean _mkTimerArmed;
#endif
    uint64_t _timerSoftDeadline; /* TSR */
    uint64_t _timerHardDeadline; /* TSR */
};
/// RunLoop 里面用到的线程锁 是 pthrea_mutext_lock
CF_INLINE void __CFRunLoopModeLock(CFRunLoopModeRef rlm) {
    pthread_mutex_lock(&(rlm->_lock));
    //CFLog(6, CFSTR("__CFRunLoopModeLock locked %p"), rlm);
}

CF_INLINE void __CFRunLoopModeUnlock(CFRunLoopModeRef rlm) {
    //CFLog(6, CFSTR("__CFRunLoopModeLock unlocking %p"), rlm);
    pthread_mutex_unlock(&(rlm->_lock));
}
/// 根据Mode里面的昵称判断两个Mode是否相等
static Boolean __CFRunLoopModeEqual(CFTypeRef cf1, CFTypeRef cf2) {
    CFRunLoopModeRef rlm1 = (CFRunLoopModeRef)cf1;
    CFRunLoopModeRef rlm2 = (CFRunLoopModeRef)cf2;
    return CFEqual(rlm1->_name, rlm2->_name);
}
/// 取得Mode的name的hash code
static CFHashCode __CFRunLoopModeHash(CFTypeRef cf) {
    CFRunLoopModeRef rlm = (CFRunLoopModeRef)cf;
    return CFHash(rlm->_name);
}

static CFStringRef __CFRunLoopModeCopyDescription(CFTypeRef cf) {
    CFRunLoopModeRef rlm = (CFRunLoopModeRef)cf;
    CFMutableStringRef result;
    result = CFStringCreateMutable(kCFAllocatorSystemDefault, 0);
    CFStringAppendFormat(result, NULL, CFSTR("<CFRunLoopMode %p [%p]>{name = %@, "), rlm, CFGetAllocator(rlm), rlm->_name);
    CFStringAppendFormat(result, NULL, CFSTR("port set = 0x%x, "), rlm->_portSet);
#if USE_DISPATCH_SOURCE_FOR_TIMERS
    CFStringAppendFormat(result, NULL, CFSTR("queue = %p, "), rlm->_queue);
    CFStringAppendFormat(result, NULL, CFSTR("source = %p (%s), "), rlm->_timerSource, rlm->_timerFired ? "fired" : "not fired");
#endif
#if USE_MK_TIMER_TOO
    CFStringAppendFormat(result, NULL, CFSTR("timer port = 0x%x, "), rlm->_timerPort);
#endif
#if DEPLOYMENT_TARGET_WINDOWS
    CFStringAppendFormat(result, NULL, CFSTR("MSGQ mask = %p, "), rlm->_msgQMask);
#endif
    CFStringAppendFormat(result, NULL, CFSTR("\n\tsources0 = %@,\n\tsources1 = %@,\n\tobservers = %@,\n\ttimers = %@,\n\tcurrently %0.09g (%lld) / soft deadline in: %0.09g sec (@ %lld) / hard deadline in: %0.09g sec (@ %lld)\n},\n"), rlm->_sources0, rlm->_sources1, rlm->_observers, rlm->_timers, CFAbsoluteTimeGetCurrent(), mach_absolute_time(), __CFTSRToTimeInterval(rlm->_timerSoftDeadline - mach_absolute_time()), rlm->_timerSoftDeadline, __CFTSRToTimeInterval(rlm->_timerHardDeadline - mach_absolute_time()), rlm->_timerHardDeadline);
    return result;
}

/// Mode 析构函数
static void __CFRunLoopModeDeallocate(CFTypeRef cf) {
    CFRunLoopModeRef rlm = (CFRunLoopModeRef)cf;
    /// release source0 source1 observers times sourceMap portSet GCD_Timer mk_timer pthread_mutex_lock
    if (NULL != rlm->_sources0) CFRelease(rlm->_sources0);
    if (NULL != rlm->_sources1) CFRelease(rlm->_sources1);
    if (NULL != rlm->_observers) CFRelease(rlm->_observers);
    if (NULL != rlm->_timers) CFRelease(rlm->_timers);
    if (NULL != rlm->_portToV1SourceMap) CFRelease(rlm->_portToV1SourceMap);
    CFRelease(rlm->_name);
    __CFPortSetFree(rlm->_portSet);
#if USE_DISPATCH_SOURCE_FOR_TIMERS
    if (rlm->_timerSource) {
        dispatch_source_cancel(rlm->_timerSource);
        dispatch_release(rlm->_timerSource);
    }
    if (rlm->_queue) {
        dispatch_release(rlm->_queue);
    }
#endif
#if USE_MK_TIMER_TOO
    if (MACH_PORT_NULL != rlm->_timerPort) mk_timer_destroy(rlm->_timerPort);
#endif
    pthread_mutex_destroy(&rlm->_lock);
    memset((char *)cf + sizeof(CFRuntimeBase), 0x7C, sizeof(struct __CFRunLoopMode) - sizeof(CFRuntimeBase));
}

/// Item
struct _block_item {
    struct _block_item *_next;
    CFTypeRef _mode;	// CFString or CFSet
    void (^_block)(void);
};

typedef struct _per_run_data {
    uint32_t a;
    uint32_t b;
    uint32_t stopped;
    uint32_t ignoreWakeUps;
} _per_run_data;
/// Run Loop 结构体
#pragma mark - Run Loop 结构体
struct __CFRunLoop {
    CFRuntimeBase _base;
    /// 锁
    pthread_mutex_t _lock;			/* locked for accessing mode list */
    /// wake up port
    __CFPort _wakeUpPort;			// used for CFRunLoopWakeUp
    /// 是否未使用
    Boolean _unused;
    volatile _per_run_data *_perRunData;              // reset for runs of the run loop
    /// pthread 线程
    pthread_t _pthread;
    /// win 线程
    uint32_t _winthread;
    /// 公共的Modes
    CFMutableSetRef _commonModes;
    /// 公共的Items
    CFMutableSetRef _commonModeItems;
    /// 当前的Mode
    CFRunLoopModeRef _currentMode;
    /// 所有的Modes
    CFMutableSetRef _modes;
    /// 头指针
    struct _block_item *_blocks_head;
    /// 尾指针
    struct _block_item *_blocks_tail;
    /// 运行时间
    CFAbsoluteTime _runTime;
    /// 休眠时间
    CFAbsoluteTime _sleepTime;
    CFTypeRef _counterpart;
};

/* Bit 0 of the base reserved bits is used for stopped state */
/* Bit 1 of the base reserved bits is used for sleeping state */
/* Bit 2 of the base reserved bits is used for deallocating state */
/// 开辟一段内存 存入 RunLoop 运行中的数据
CF_INLINE volatile _per_run_data *__CFRunLoopPushPerRunData(CFRunLoopRef rl) {
    volatile _per_run_data *previous = rl->_perRunData;
    rl->_perRunData = (volatile _per_run_data *)CFAllocatorAllocate(kCFAllocatorSystemDefault, sizeof(_per_run_data), 0);
    rl->_perRunData->a = 0x4346524C;
    rl->_perRunData->b = 0x4346524C; // 'CFRL'
    rl->_perRunData->stopped = 0x00000000;
    rl->_perRunData->ignoreWakeUps = 0x00000000;
    return previous;
}
/// 移除RunLoop中的那段开辟内存中的数据
CF_INLINE void __CFRunLoopPopPerRunData(CFRunLoopRef rl, volatile _per_run_data *previous) {
    if (rl->_perRunData) CFAllocatorDeallocate(kCFAllocatorSystemDefault, (void *)rl->_perRunData);
    rl->_perRunData = previous;
}
/// 判断RunLoop是否已经停止
CF_INLINE Boolean __CFRunLoopIsStopped(CFRunLoopRef rl) {
    return (rl->_perRunData->stopped) ? true : false;
}
/// 设置RunLoop已经停止
CF_INLINE void __CFRunLoopSetStopped(CFRunLoopRef rl) {
    rl->_perRunData->stopped = 0x53544F50;	// 'STOP'
}
/// 取消RunLoop已经停止的设置
CF_INLINE void __CFRunLoopUnsetStopped(CFRunLoopRef rl) {
    rl->_perRunData->stopped = 0x0;
}
/// 判断是否需要忽略 唤醒 的RunLoop
CF_INLINE Boolean __CFRunLoopIsIgnoringWakeUps(CFRunLoopRef rl) {
    return (rl->_perRunData->ignoreWakeUps) ? true : false;    
}
/// 设置RunLoop忽略唤醒
CF_INLINE void __CFRunLoopSetIgnoreWakeUps(CFRunLoopRef rl) {
    rl->_perRunData->ignoreWakeUps = 0x57414B45; // 'WAKE'
}
/// 取消设置RunLoop需要唤醒
CF_INLINE void __CFRunLoopUnsetIgnoreWakeUps(CFRunLoopRef rl) {
    rl->_perRunData->ignoreWakeUps = 0x0;
}
/// 判断RunLoop 是否正在休眠
CF_INLINE Boolean __CFRunLoopIsSleeping(CFRunLoopRef rl) {
    return (Boolean)__CFBitfieldGetValue(((const CFRuntimeBase *)rl)->_cfinfo[CF_INFO_BITS], 1, 1);
}
/// 设置RunLoop 休眠
CF_INLINE void __CFRunLoopSetSleeping(CFRunLoopRef rl) {
    __CFBitfieldSetValue(((CFRuntimeBase *)rl)->_cfinfo[CF_INFO_BITS], 1, 1, 1);
}

CF_INLINE void __CFRunLoopUnsetSleeping(CFRunLoopRef rl) {
    __CFBitfieldSetValue(((CFRuntimeBase *)rl)->_cfinfo[CF_INFO_BITS], 1, 1, 0);
}
/// RunLoop 是否正在析构
CF_INLINE Boolean __CFRunLoopIsDeallocating(CFRunLoopRef rl) {
    return (Boolean)__CFBitfieldGetValue(((const CFRuntimeBase *)rl)->_cfinfo[CF_INFO_BITS], 2, 2);
}
/// 设置 RunLoop 正在析构
CF_INLINE void __CFRunLoopSetDeallocating(CFRunLoopRef rl) {
    __CFBitfieldSetValue(((CFRuntimeBase *)rl)->_cfinfo[CF_INFO_BITS], 2, 2, 1);
}
/// 加锁
CF_INLINE void __CFRunLoopLock(CFRunLoopRef rl) {
    pthread_mutex_lock(&(((CFRunLoopRef)rl)->_lock));
    //    CFLog(6, CFSTR("__CFRunLoopLock locked %p"), rl);
}
/// 解锁
CF_INLINE void __CFRunLoopUnlock(CFRunLoopRef rl) {
    //    CFLog(6, CFSTR("__CFRunLoopLock unlocking %p"), rl);
    pthread_mutex_unlock(&(((CFRunLoopRef)rl)->_lock));
}

static CFStringRef __CFRunLoopCopyDescription(CFTypeRef cf) {
    CFRunLoopRef rl = (CFRunLoopRef)cf;
    CFMutableStringRef result;
    result = CFStringCreateMutable(kCFAllocatorSystemDefault, 0);

    CFStringAppendFormat(result, NULL, CFSTR("<CFRunLoop %p [%p]>{wakeup port = 0x%x, stopped = %s, ignoreWakeUps = %s, \ncurrent mode = %@,\n"), cf, CFGetAllocator(cf), rl->_wakeUpPort, __CFRunLoopIsStopped(rl) ? "true" : "false", __CFRunLoopIsIgnoringWakeUps(rl) ? "true" : "false", rl->_currentMode ? rl->_currentMode->_name : CFSTR("(none)"));

    CFStringAppendFormat(result, NULL, CFSTR("common modes = %@,\ncommon mode items = %@,\nmodes = %@}\n"), rl->_commonModes, rl->_commonModeItems, rl->_modes);
    return result;
}

CF_PRIVATE void __CFRunLoopDump() { // __private_extern__ to keep the compiler from discarding it
    CFShow(CFCopyDescription(CFRunLoopGetCurrent()));
}
/// 初始化RunLoop中的锁 是个递归锁 PTHREAD_MUTEX_RECURSIVE
CF_INLINE void __CFRunLoopLockInit(pthread_mutex_t *lock) {
    pthread_mutexattr_t mattr;
    pthread_mutexattr_init(&mattr);
    pthread_mutexattr_settype(&mattr, PTHREAD_MUTEX_RECURSIVE);
    int32_t mret = pthread_mutex_init(lock, &mattr);
    pthread_mutexattr_destroy(&mattr);
    if (0 != mret) {
    }
}
#pragma mark - 寻找Mode，没有就创建
/* call with rl locked, returns mode locked */
static CFRunLoopModeRef __CFRunLoopFindMode(CFRunLoopRef rl, CFStringRef modeName, Boolean create) {
    CHECK_FOR_FORK();
    CFRunLoopModeRef rlm;
    struct __CFRunLoopMode srlm;
    memset(&srlm, 0, sizeof(srlm));
    _CFRuntimeSetInstanceTypeIDAndIsa(&srlm, __kCFRunLoopModeTypeID);
    srlm._name = modeName;
    rlm = (CFRunLoopModeRef)CFSetGetValue(rl->_modes, &srlm);
    if (NULL != rlm) {
        __CFRunLoopModeLock(rlm);
        return rlm;
    }
    if (!create) {
        return NULL;
    }
    /// _CFRuntimeCreateInstance 创建对象
    rlm = (CFRunLoopModeRef)_CFRuntimeCreateInstance(kCFAllocatorSystemDefault, __kCFRunLoopModeTypeID, sizeof(struct __CFRunLoopMode) - sizeof(CFRuntimeBase), NULL);
    if (NULL == rlm) {
        return NULL;
    }
    __CFRunLoopLockInit(&rlm->_lock);
    rlm->_name = CFStringCreateCopy(kCFAllocatorSystemDefault, modeName);
    rlm->_stopped = false;
    rlm->_portToV1SourceMap = NULL;
    rlm->_sources0 = NULL;
    rlm->_sources1 = NULL;
    rlm->_observers = NULL;
    rlm->_timers = NULL;
    rlm->_observerMask = 0;
    rlm->_portSet = __CFPortSetAllocate();
    rlm->_timerSoftDeadline = UINT64_MAX;
    rlm->_timerHardDeadline = UINT64_MAX;
    
    kern_return_t ret = KERN_SUCCESS;
#if USE_DISPATCH_SOURCE_FOR_TIMERS
    rlm->_timerFired = false;
    rlm->_queue = _dispatch_runloop_root_queue_create_4CF("Run Loop Mode Queue", 0);
    mach_port_t queuePort = _dispatch_runloop_root_queue_get_port_4CF(rlm->_queue);
    if (queuePort == MACH_PORT_NULL) CRASH("*** Unable to create run loop mode queue port. (%d) ***", -1);
    rlm->_timerSource = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, rlm->_queue);
    
    __block Boolean *timerFiredPointer = &(rlm->_timerFired);
    dispatch_source_set_event_handler(rlm->_timerSource, ^{
        *timerFiredPointer = true;
    });
    
    // Set timer to far out there. The unique leeway makes this timer easy to spot in debug output.
    _dispatch_source_set_runloop_timer_4CF(rlm->_timerSource, DISPATCH_TIME_FOREVER, DISPATCH_TIME_FOREVER, 321);
    dispatch_resume(rlm->_timerSource);
    
    ret = __CFPortSetInsert(queuePort, rlm->_portSet);
    if (KERN_SUCCESS != ret) CRASH("*** Unable to insert timer port into port set. (%d) ***", ret);
    
#endif
#if USE_MK_TIMER_TOO
    rlm->_timerPort = mk_timer_create();
    ret = __CFPortSetInsert(rlm->_timerPort, rlm->_portSet);
    if (KERN_SUCCESS != ret) CRASH("*** Unable to insert timer port into port set. (%d) ***", ret);
#endif
    
    ret = __CFPortSetInsert(rl->_wakeUpPort, rlm->_portSet);
    if (KERN_SUCCESS != ret) CRASH("*** Unable to insert wake up port into port set. (%d) ***", ret);
    
    CFSetAddValue(rl->_modes, rlm);
    CFRelease(rlm);
    __CFRunLoopModeLock(rlm);	/* return mode locked */
    return rlm;
}

#pragma mark - 判断RunLoopMode是否是空的
static Boolean __CFRunLoopModeIsEmpty(CFRunLoopRef rl, CFRunLoopModeRef rlm, CFRunLoopModeRef previousMode) {
    CHECK_FOR_FORK();
    /// Mode 空的
    if (NULL == rlm) return true;

    /// 有 gcd 的 block return false
    Boolean libdispatchQSafe = pthread_main_np() && ((HANDLE_DISPATCH_ON_BASE_INVOCATION_ONLY && NULL == previousMode) || (!HANDLE_DISPATCH_ON_BASE_INVOCATION_ONLY && 0 == _CFGetTSD(__CFTSDKeyIsInGCDMainQ)));
    if (libdispatchQSafe && (CFRunLoopGetMain() == rl) && CFSetContainsValue(rl->_commonModes, rlm->_name)) return false; // represents the libdispatch main queue
    /// 有source 0 或 source1 或 times return false
    if (NULL != rlm->_sources0 && 0 < CFSetGetCount(rlm->_sources0)) return false;
    if (NULL != rlm->_sources1 && 0 < CFSetGetCount(rlm->_sources1)) return false;
    if (NULL != rlm->_timers && 0 < CFArrayGetCount(rlm->_timers)) return false;
    struct _block_item *item = rl->_blocks_head;
    while (item) {
        struct _block_item *curr = item;
        item = item->_next;
        Boolean doit = false;
        if (CFStringGetTypeID() == CFGetTypeID(curr->_mode)) {
            doit = CFEqual(curr->_mode, rlm->_name) || (CFEqual(curr->_mode, kCFRunLoopCommonModes) && CFSetContainsValue(rl->_commonModes, rlm->_name));
        } else {
            doit = CFSetContainsValue((CFSetRef)curr->_mode, rlm->_name) || (CFSetContainsValue((CFSetRef)curr->_mode, kCFRunLoopCommonModes) && CFSetContainsValue(rl->_commonModes, rlm->_name));
        }
        if (doit) return false;
    }
    return true;
}

CF_INLINE Boolean __CFIsValid(const void *cf) {
    return (Boolean)__CFBitfieldGetValue(((const CFRuntimeBase *)cf)->_cfinfo[CF_INFO_BITS], 3, 3);
}

CF_INLINE void __CFSetValid(void *cf) {
    __CFBitfieldSetValue(((CFRuntimeBase *)cf)->_cfinfo[CF_INFO_BITS], 3, 3, 1);
}

CF_INLINE void __CFUnsetValid(void *cf) {
    __CFBitfieldSetValue(((CFRuntimeBase *)cf)->_cfinfo[CF_INFO_BITS], 3, 3, 0);
}
#pragma mark - Source 结构体
/// RunLoop source
struct __CFRunLoopSource {
    CFRuntimeBase _base;
    uint32_t _bits;
    /// 锁
    pthread_mutex_t _lock;
    CFIndex _order;			/* immutable */
    CFMutableBagRef _runLoops;
    /// 上下文
    union {
        CFRunLoopSourceContext version0;	/* immutable, except invalidation */
        CFRunLoopSourceContext1 version1;	/* immutable, except invalidation */
    } _context;
};

/* Bit 1 of the base reserved bits is used for signalled state */

CF_INLINE Boolean __CFRunLoopSourceIsSignaled(CFRunLoopSourceRef rls) {
    return (Boolean)__CFBitfieldGetValue(rls->_bits, 1, 1);
}

CF_INLINE void __CFRunLoopSourceSetSignaled(CFRunLoopSourceRef rls) {
    __CFBitfieldSetValue(rls->_bits, 1, 1, 1);
}

CF_INLINE void __CFRunLoopSourceUnsetSignaled(CFRunLoopSourceRef rls) {
    __CFBitfieldSetValue(rls->_bits, 1, 1, 0);
}
/// 事件源加锁
CF_INLINE void __CFRunLoopSourceLock(CFRunLoopSourceRef rls) {
    pthread_mutex_lock(&(rls->_lock));
    //    CFLog(6, CFSTR("__CFRunLoopSourceLock locked %p"), rls);
}
/// 事件源解锁
CF_INLINE void __CFRunLoopSourceUnlock(CFRunLoopSourceRef rls) {
    //    CFLog(6, CFSTR("__CFRunLoopSourceLock unlocking %p"), rls);
    pthread_mutex_unlock(&(rls->_lock));
}

#pragma mark - Observers
/// 观察者
struct __CFRunLoopObserver {
    CFRuntimeBase _base;
    /// 观察者Lock
    pthread_mutex_t _lock;
    /// RunLoop
    CFRunLoopRef _runLoop;
    CFIndex _rlCount;
    /// 观察者活动
    CFOptionFlags _activities;		/* immutable */
    CFIndex _order;			/* immutable */
    /// 观察者回调
    CFRunLoopObserverCallBack _callout;	/* immutable */
    /// 观察者上下文
    CFRunLoopObserverContext _context;	/* immutable, except invalidation */
};

/* Bit 0 of the base reserved bits is used for firing state */
/* Bit 1 of the base reserved bits is used for repeats state */

CF_INLINE Boolean __CFRunLoopObserverIsFiring(CFRunLoopObserverRef rlo) {
    return (Boolean)__CFBitfieldGetValue(((const CFRuntimeBase *)rlo)->_cfinfo[CF_INFO_BITS], 0, 0);
}

CF_INLINE void __CFRunLoopObserverSetFiring(CFRunLoopObserverRef rlo) {
    __CFBitfieldSetValue(((CFRuntimeBase *)rlo)->_cfinfo[CF_INFO_BITS], 0, 0, 1);
}

CF_INLINE void __CFRunLoopObserverUnsetFiring(CFRunLoopObserverRef rlo) {
    __CFBitfieldSetValue(((CFRuntimeBase *)rlo)->_cfinfo[CF_INFO_BITS], 0, 0, 0);
}

CF_INLINE Boolean __CFRunLoopObserverRepeats(CFRunLoopObserverRef rlo) {
    return (Boolean)__CFBitfieldGetValue(((const CFRuntimeBase *)rlo)->_cfinfo[CF_INFO_BITS], 1, 1);
}

CF_INLINE void __CFRunLoopObserverSetRepeats(CFRunLoopObserverRef rlo) {
    __CFBitfieldSetValue(((CFRuntimeBase *)rlo)->_cfinfo[CF_INFO_BITS], 1, 1, 1);
}

CF_INLINE void __CFRunLoopObserverUnsetRepeats(CFRunLoopObserverRef rlo) {
    __CFBitfieldSetValue(((CFRuntimeBase *)rlo)->_cfinfo[CF_INFO_BITS], 1, 1, 0);
}
/// 观察者加锁
CF_INLINE void __CFRunLoopObserverLock(CFRunLoopObserverRef rlo) {
    pthread_mutex_lock(&(rlo->_lock));
    //    CFLog(6, CFSTR("__CFRunLoopObserverLock locked %p"), rlo);
}
/// 观察者解锁
CF_INLINE void __CFRunLoopObserverUnlock(CFRunLoopObserverRef rlo) {
    //    CFLog(6, CFSTR("__CFRunLoopObserverLock unlocking %p"), rlo);
    pthread_mutex_unlock(&(rlo->_lock));
}
/// 设置观察者
static void __CFRunLoopObserverSchedule(CFRunLoopObserverRef rlo, CFRunLoopRef rl, CFRunLoopModeRef rlm) {
    __CFRunLoopObserverLock(rlo);
    if (0 == rlo->_rlCount) {
        rlo->_runLoop = rl;
    }
    rlo->_rlCount++;
    __CFRunLoopObserverUnlock(rlo);
}
/// 取消观察者
static void __CFRunLoopObserverCancel(CFRunLoopObserverRef rlo, CFRunLoopRef rl, CFRunLoopModeRef rlm) {
    __CFRunLoopObserverLock(rlo);
    rlo->_rlCount--;
    if (0 == rlo->_rlCount) {
        rlo->_runLoop = NULL;
    }
    __CFRunLoopObserverUnlock(rlo);
}

#pragma mark - Timers
/// timer 结构体
struct __CFRunLoopTimer {
    CFRuntimeBase _base;
    uint16_t _bits;
    /// timer 锁
    pthread_mutex_t _lock;
    /// RunLoop
    CFRunLoopRef _runLoop;
    /// Mode
    CFMutableSetRef _rlModes;
    /// 下次启动时间
    CFAbsoluteTime _nextFireDate;
    /// 时间间隔
    CFTimeInterval _interval;		/* immutable */
    /// 宽容时间
    CFTimeInterval _tolerance;          /* mutable */
    uint64_t _fireTSR;			/* TSR units */
    CFIndex _order;			/* immutable */
    /// Timer回调
    CFRunLoopTimerCallBack _callout;	/* immutable */
    /// Timer 上下文
    CFRunLoopTimerContext _context;	/* immutable, except invalidation */
};

/* Bit 0 of the base reserved bits is used for firing state */
/* Bit 1 of the base reserved bits is used for fired-during-callout state */
/* Bit 2 of the base reserved bits is used for waking state */

CF_INLINE Boolean __CFRunLoopTimerIsFiring(CFRunLoopTimerRef rlt) {
    return (Boolean)__CFBitfieldGetValue(rlt->_bits, 0, 0);
}

CF_INLINE void __CFRunLoopTimerSetFiring(CFRunLoopTimerRef rlt) {
    __CFBitfieldSetValue(rlt->_bits, 0, 0, 1);
}

CF_INLINE void __CFRunLoopTimerUnsetFiring(CFRunLoopTimerRef rlt) {
    __CFBitfieldSetValue(rlt->_bits, 0, 0, 0);
}
/// Timer 是否正在析构
CF_INLINE Boolean __CFRunLoopTimerIsDeallocating(CFRunLoopTimerRef rlt) {
    return (Boolean)__CFBitfieldGetValue(rlt->_bits, 2, 2);
}
/// 设置Timer 进行析构
CF_INLINE void __CFRunLoopTimerSetDeallocating(CFRunLoopTimerRef rlt) {
    __CFBitfieldSetValue(rlt->_bits, 2, 2, 1);
}
/// Timer加锁
CF_INLINE void __CFRunLoopTimerLock(CFRunLoopTimerRef rlt) {
    pthread_mutex_lock(&(rlt->_lock));
    //    CFLog(6, CFSTR("__CFRunLoopTimerLock locked %p"), rlt);
}
/// Timer解锁
CF_INLINE void __CFRunLoopTimerUnlock(CFRunLoopTimerRef rlt) {
    //    CFLog(6, CFSTR("__CFRunLoopTimerLock unlocking %p"), rlt);
    pthread_mutex_unlock(&(rlt->_lock));
}

static CFLock_t __CFRLTFireTSRLock = CFLockInit;

CF_INLINE void __CFRunLoopTimerFireTSRLock(void) {
    __CFLock(&__CFRLTFireTSRLock);
}

CF_INLINE void __CFRunLoopTimerFireTSRUnlock(void) {
    __CFUnlock(&__CFRLTFireTSRLock);
}

#pragma mark -

/* CFRunLoop */
/// 定义两个常量 Mode  kCFRunLoopDefaultMode 为默认 Mode  kCFRunLoopCommonModes 为公共Mode
CONST_STRING_DECL(kCFRunLoopDefaultMode, "kCFRunLoopDefaultMode")
CONST_STRING_DECL(kCFRunLoopCommonModes, "kCFRunLoopCommonModes")
#pragma mark - 寻找Mack_port
/// 根据Mach Port 端口进行寻找事件源Source 从Mode结构体中的_portToV1SourceMap 去寻找 Mach Port 为 Map中的Key
static CFRunLoopSourceRef __CFRunLoopModeFindSourceForMachPort(CFRunLoopRef rl, CFRunLoopModeRef rlm, __CFPort port) {	/* DOES CALLOUT */
    CHECK_FOR_FORK();
    
    CFRunLoopSourceRef found = rlm->_portToV1SourceMap ? (CFRunLoopSourceRef)CFDictionaryGetValue(rlm->_portToV1SourceMap, (const void *)(uintptr_t)port) : NULL;
    return found;
}

/// 清掉 RunLoop 里面的 Source0 和 Source1
static void __CFRunLoopCleanseSources(const void *value, void *context) {
    CFRunLoopModeRef rlm = (CFRunLoopModeRef)value;
    CFRunLoopRef rl = (CFRunLoopRef)context;
    CFIndex idx, cnt;
    const void **list, *buffer[256];
    if (NULL == rlm->_sources0 && NULL == rlm->_sources1) return;
    
    cnt = (rlm->_sources0 ? CFSetGetCount(rlm->_sources0) : 0) + (rlm->_sources1 ? CFSetGetCount(rlm->_sources1) : 0);
    list = (const void **)((cnt <= 256) ? buffer : CFAllocatorAllocate(kCFAllocatorSystemDefault, cnt * sizeof(void *), 0));
    if (rlm->_sources0) CFSetGetValues(rlm->_sources0, list);
    if (rlm->_sources1) CFSetGetValues(rlm->_sources1, list + (rlm->_sources0 ? CFSetGetCount(rlm->_sources0) : 0));
    for (idx = 0; idx < cnt; idx++) {
        CFRunLoopSourceRef rls = (CFRunLoopSourceRef)list[idx];
        __CFRunLoopSourceLock(rls);
        if (NULL != rls->_runLoops) {
            CFBagRemoveValue(rls->_runLoops, rl);
        }
        __CFRunLoopSourceUnlock(rls);
    }
    if (list != buffer) CFAllocatorDeallocate(kCFAllocatorSystemDefault, list);
}

/// Source 的析构函数 清掉Source里面的内部资源
static void __CFRunLoopDeallocateSources(const void *value, void *context) {
    CFRunLoopModeRef rlm = (CFRunLoopModeRef)value;
    CFRunLoopRef rl = (CFRunLoopRef)context;
    CFIndex idx, cnt;
    const void **list, *buffer[256];
    if (NULL == rlm->_sources0 && NULL == rlm->_sources1) return;
    
    cnt = (rlm->_sources0 ? CFSetGetCount(rlm->_sources0) : 0) + (rlm->_sources1 ? CFSetGetCount(rlm->_sources1) : 0);
    list = (const void **)((cnt <= 256) ? buffer : CFAllocatorAllocate(kCFAllocatorSystemDefault, cnt * sizeof(void *), 0));
    if (rlm->_sources0) CFSetGetValues(rlm->_sources0, list);
    if (rlm->_sources1) CFSetGetValues(rlm->_sources1, list + (rlm->_sources0 ? CFSetGetCount(rlm->_sources0) : 0));
    for (idx = 0; idx < cnt; idx++) {
        CFRetain(list[idx]);
    }
    if (rlm->_sources0) CFSetRemoveAllValues(rlm->_sources0);
    if (rlm->_sources1) CFSetRemoveAllValues(rlm->_sources1);
    for (idx = 0; idx < cnt; idx++) {
        CFRunLoopSourceRef rls = (CFRunLoopSourceRef)list[idx];
        __CFRunLoopSourceLock(rls);
        if (NULL != rls->_runLoops) {
            CFBagRemoveValue(rls->_runLoops, rl);
        }
        __CFRunLoopSourceUnlock(rls);
        if (0 == rls->_context.version0.version) {
            if (NULL != rls->_context.version0.cancel) {
                rls->_context.version0.cancel(rls->_context.version0.info, rl, rlm->_name);	/* CALLOUT */
            }
        } else if (1 == rls->_context.version0.version) {
            __CFPort port = rls->_context.version1.getPort(rls->_context.version1.info);	/* CALLOUT */
            if (CFPORT_NULL != port) {
                __CFPortSetRemove(port, rlm->_portSet);
            }
        }
        CFRelease(rls);
    }
    if (list != buffer) CFAllocatorDeallocate(kCFAllocatorSystemDefault, list);
}
/// Observers 的析构函数 清掉里面的内部资源
static void __CFRunLoopDeallocateObservers(const void *value, void *context) {
    CFRunLoopModeRef rlm = (CFRunLoopModeRef)value;
    CFRunLoopRef rl = (CFRunLoopRef)context;
    CFIndex idx, cnt;
    const void **list, *buffer[256];
    if (NULL == rlm->_observers) return;
    cnt = CFArrayGetCount(rlm->_observers);
    list = (const void **)((cnt <= 256) ? buffer : CFAllocatorAllocate(kCFAllocatorSystemDefault, cnt * sizeof(void *), 0));
    CFArrayGetValues(rlm->_observers, CFRangeMake(0, cnt), list);
    for (idx = 0; idx < cnt; idx++) {
        CFRetain(list[idx]);
    }
    CFArrayRemoveAllValues(rlm->_observers);
    for (idx = 0; idx < cnt; idx++) {
        __CFRunLoopObserverCancel((CFRunLoopObserverRef)list[idx], rl, rlm);
        CFRelease(list[idx]);
    }
    if (list != buffer) CFAllocatorDeallocate(kCFAllocatorSystemDefault, list);
}
/// Timers 的析构函数 清掉里面的内部资源
static void __CFRunLoopDeallocateTimers(const void *value, void *context) {
    CFRunLoopModeRef rlm = (CFRunLoopModeRef)value;
    if (NULL == rlm->_timers) return;
    void (^deallocateTimers)(CFMutableArrayRef timers) = ^(CFMutableArrayRef timers) {
        CFIndex idx, cnt;
        const void **list, *buffer[256];
        cnt = CFArrayGetCount(timers);
        list = (const void **)((cnt <= 256) ? buffer : CFAllocatorAllocate(kCFAllocatorSystemDefault, cnt * sizeof(void *), 0));
        CFArrayGetValues(timers, CFRangeMake(0, CFArrayGetCount(timers)), list);
        for (idx = 0; idx < cnt; idx++) {
            CFRetain(list[idx]);
        }
        CFArrayRemoveAllValues(timers);
        for (idx = 0; idx < cnt; idx++) {
            CFRunLoopTimerRef rlt = (CFRunLoopTimerRef)list[idx];
            __CFRunLoopTimerLock(rlt);
            // if the run loop is deallocating, and since a timer can only be in one
            // run loop, we're going to be removing the timer from all modes, so be
            // a little heavy-handed and direct
            CFSetRemoveAllValues(rlt->_rlModes);
            rlt->_runLoop = NULL;
            __CFRunLoopTimerUnlock(rlt);
            CFRelease(list[idx]);
        }
        if (list != buffer) CFAllocatorDeallocate(kCFAllocatorSystemDefault, list);
    };
    
    if (rlm->_timers && CFArrayGetCount(rlm->_timers)) deallocateTimers(rlm->_timers);
}

CF_EXPORT CFRunLoopRef _CFRunLoopGet0b(pthread_t t);
#pragma mark - RunLoop 的析构函数
static void __CFRunLoopDeallocate(CFTypeRef cf) {
    CFRunLoopRef rl = (CFRunLoopRef)cf;
    
    if (_CFRunLoopGet0b(pthread_main_thread_np()) == cf) HALT;
    
    /* We try to keep the run loop in a valid state as long as possible,
     since sources may have non-retained references to the run loop.
     Another reason is that we don't want to lock the run loop for
     callback reasons, if we can get away without that.  We start by
     eliminating the sources, since they are the most likely to call
     back into the run loop during their "cancellation". Common mode
     items will be removed from the mode indirectly by the following
     three lines. */
    /// 设置当前的RunLoop为正在进行析构
    __CFRunLoopSetDeallocating(rl);
    /// 取出RunLoop中的所有Mode 进行清除Source,调用 Source/Observers/Timers的析构函数
    if (NULL != rl->_modes) {
        CFSetApplyFunction(rl->_modes, (__CFRunLoopCleanseSources), rl); // remove references to rl
        CFSetApplyFunction(rl->_modes, (__CFRunLoopDeallocateSources), rl);
        CFSetApplyFunction(rl->_modes, (__CFRunLoopDeallocateObservers), rl);
        CFSetApplyFunction(rl->_modes, (__CFRunLoopDeallocateTimers), rl);
    }
    /// 加递归锁
    __CFRunLoopLock(rl);
    /// 递归释放Item 由blocks_head 头指针 指向下一个Node节点指针 然后释放 curr item
    struct _block_item *item = rl->_blocks_head;
    while (item) {
        struct _block_item *curr = item;
        item = item->_next;
        CFRelease(curr->_mode);
        Block_release(curr->_block);
        free(curr);
    }
    /// 释放 _commonModeItems
    if (NULL != rl->_commonModeItems) {
        CFRelease(rl->_commonModeItems);
    }
    /// 释放 _commonModes
    if (NULL != rl->_commonModes) {
        CFRelease(rl->_commonModes);
    }
    /// 释放 _modes
    if (NULL != rl->_modes) {
        CFRelease(rl->_modes);
    }
    /// 释放 _wakeUpPort
    __CFPortFree(rl->_wakeUpPort);
    rl->_wakeUpPort = CFPORT_NULL;
    /// 释放 _per_run_data 变量
    __CFRunLoopPopPerRunData(rl, NULL);
    /// 解递归锁 然后 释放
    __CFRunLoopUnlock(rl);
    /// 释放 Lock
    pthread_mutex_destroy(&rl->_lock);
    /// 重置内存空间
    memset((char *)cf + sizeof(CFRuntimeBase), 0x8C, sizeof(struct __CFRunLoop) - sizeof(CFRuntimeBase));
}

static const CFRuntimeClass __CFRunLoopModeClass = {
    0,
    "CFRunLoopMode",
    NULL,      // init
    NULL,      // copy
    __CFRunLoopModeDeallocate,
    __CFRunLoopModeEqual,
    __CFRunLoopModeHash,
    NULL,      // 
    __CFRunLoopModeCopyDescription
};

static const CFRuntimeClass __CFRunLoopClass = {
    0,
    "CFRunLoop",
    NULL,      // init
    NULL,      // copy
    __CFRunLoopDeallocate,
    NULL,
    NULL,
    NULL,      // 
    __CFRunLoopCopyDescription
};

CF_PRIVATE void __CFFinalizeRunLoop(uintptr_t data);
#pragma mark - 取得 RunLoop 中的 Type ID
CFTypeID CFRunLoopGetTypeID(void) {
    static dispatch_once_t initOnce;
    dispatch_once(&initOnce, ^{ __kCFRunLoopTypeID = _CFRuntimeRegisterClass(&__CFRunLoopClass); __kCFRunLoopModeTypeID = _CFRuntimeRegisterClass(&__CFRunLoopModeClass); });
    return __kCFRunLoopTypeID;
}
#pragma mark - 根据传入的 pthread_t 线程 创建 RunLoop
static CFRunLoopRef __CFRunLoopCreate(pthread_t t) {
    CFRunLoopRef loop = NULL;
    CFRunLoopModeRef rlm;
    /// 取大小
    uint32_t size = sizeof(struct __CFRunLoop) - sizeof(CFRuntimeBase);
    /// 创建函数  CFRunLoopGetTypeID() 从上面取得 Type ID
    /// _CFRuntimeCreateInstance 创建对象
    loop = (CFRunLoopRef)_CFRuntimeCreateInstance(kCFAllocatorSystemDefault, CFRunLoopGetTypeID(), size, NULL);
    if (NULL == loop) {
        return NULL;
    }
    /// 开辟空间存数据
    (void)__CFRunLoopPushPerRunData(loop);
    /// 初始化内部递归所
    __CFRunLoopLockInit(&loop->_lock);
    /// 创建 wakeUpPort 唤醒的端口号
    loop->_wakeUpPort = __CFPortAllocate();
    if (CFPORT_NULL == loop->_wakeUpPort) HALT;
    /// 设置需要忽略 唤醒的 端口号 rl->_perRunData->ignoreWakeUps
    __CFRunLoopSetIgnoreWakeUps(loop);
    /// 创建commonModes 集合
    loop->_commonModes = CFSetCreateMutable(kCFAllocatorSystemDefault, 0, &kCFTypeSetCallBacks);
    /// 默认给commonModes 添加一个 kCFRunLoopDefaultMode
    CFSetAddValue(loop->_commonModes, kCFRunLoopDefaultMode);
    /// 初始化 置空
    loop->_commonModeItems = NULL;
    loop->_currentMode = NULL;
    loop->_modes = CFSetCreateMutable(kCFAllocatorSystemDefault, 0, &kCFTypeSetCallBacks);
    loop->_blocks_head = NULL;
    loop->_blocks_tail = NULL;
    loop->_counterpart = NULL;
    /// 给RunLoop pthread 赋值线程
    loop->_pthread = t;
    /// win 的线程
#if DEPLOYMENT_TARGET_WINDOWS
    loop->_winthread = GetCurrentThreadId();
#else
    loop->_winthread = 0;
#endif
    /// 创建RunLoop默认就创建了一个 kCFRunLoopDefaultMode    下面函数最后一个参数为true
    rlm = __CFRunLoopFindMode(loop, kCFRunLoopDefaultMode, true);
    if (NULL != rlm) __CFRunLoopModeUnlock(rlm);
    return loop;
}
#pragma mark -  全局的 __CFRunLoops 字典
static CFMutableDictionaryRef __CFRunLoops = NULL;
#pragma mark - 访问 loopsDic 时的锁
static CFLock_t loopsLock = CFLockInit;

// should only be called by Foundation
// t==0 is a synonym for "main thread" that always works
#pragma mark - 根据pthread线程获取RunLoop 如果 线程等于空的 则赋值 主线程 pthread_main_thread_np()
CF_EXPORT CFRunLoopRef _CFRunLoopGet0(pthread_t t) {
    if (pthread_equal(t, kNilPthreadT)) {
        t = pthread_main_thread_np();
    }
    /// 全局 锁
    __CFLock(&loopsLock);
    /// 如果全局的 __CFRunLoops 为 空 则进行 create 操作
    if (!__CFRunLoops) {
        /// 解锁
        __CFUnlock(&loopsLock);
        /// 创建 字典
        CFMutableDictionaryRef dict = CFDictionaryCreateMutable(kCFAllocatorSystemDefault, 0, NULL, &kCFTypeDictionaryValueCallBacks);
        /// 默认创建 主线程中的 RunLoop mainLoop
        CFRunLoopRef mainLoop = __CFRunLoopCreate(pthread_main_thread_np());
        /// 放到到字典里面 -- 以线程的指针地址为 key RunLoop 为值 存放
        CFDictionarySetValue(dict, pthreadPointer(pthread_main_thread_np()), mainLoop);
        /// 这里使用内核方法 就是 把 dict 赋值给 全局 __CFRunLoops 字典
        if (!OSAtomicCompareAndSwapPtrBarrier(NULL, dict, (void * volatile *)&__CFRunLoops)) {
            /// 释放临时变量 dict
            CFRelease(dict);
        }
        /// 释放临时变量 mainLoop
        CFRelease(mainLoop);
        /// 加锁
        __CFLock(&loopsLock);
    }
    /// 以线程的指针地址去全局的字典里面取RunLoop
    CFRunLoopRef loop = (CFRunLoopRef)CFDictionaryGetValue(__CFRunLoops, pthreadPointer(t));
    /// 解锁
    __CFUnlock(&loopsLock);
    /// 如果取不到值 则进行创建一个新的 RunLoop
    if (!loop) {
        /// 以线程 t 创建 RunLoop
        CFRunLoopRef newLoop = __CFRunLoopCreate(t);
        /// 加锁
        __CFLock(&loopsLock);
        /// 再次取值 进行判断
        loop = (CFRunLoopRef)CFDictionaryGetValue(__CFRunLoops, pthreadPointer(t));
        if (!loop) {
            /// 设置全局字典值
            CFDictionarySetValue(__CFRunLoops, pthreadPointer(t), newLoop);
            /// 复制操作
            loop = newLoop;
        }
        // don't release run loops inside the loopsLock, because CFRunLoopDeallocate may end up taking it
        /// 不要在 loops lock 里面进行释放，因为它可能正在执行 CFRunLoopDeallocate 函数
        __CFUnlock(&loopsLock);
        /// 解锁后释放
        CFRelease(newLoop);
    }
    /// 如果创建RunLoop 的线程 等于 当前的线程，类似的则把RunLoop存入到当前线程的开辟的私有本地内存中 TSD/TLS
    if (pthread_equal(t, pthread_self())) {
        _CFSetTSD(__CFTSDKeyRunLoop, (void *)loop, NULL);
        if (0 == _CFGetTSD(__CFTSDKeyRunLoopCntr)) {
            /// 注册一个回调，当线程销毁时，顺便也销毁其对应的 RunLoop。
            /// PTHREAD_DESTRUCTOR_ITERATIONS 最大尝试销毁线程相关数据(Thread Specific Data)的次数，见下面关于Thread-Specific Data的内容
            ///  Thread-Specific Data是一种很方便的将数据和线程联系起来的方法，在C Runtime中也大量用到Thread-Specific Data来维护线程相关的数据，一个典型的例子是errno：实际上errno是一个函数调用，返回和线程相关的错误值。Windows中有类似的机制，称为TLS (Thread Local Storage)
            /// 访问Thread-Specific Data需要使用Key。不同线程使用同一个key访问同一类型的数据（比如Errno），但是可以存放不同的值。Key的类型为pthread_key_t
            _CFSetTSD(__CFTSDKeyRunLoopCntr, (void *)(PTHREAD_DESTRUCTOR_ITERATIONS-1), (void (*)(void *))__CFFinalizeRunLoop);
        }
    }
    return loop;
}

// should only be called by Foundation
CFRunLoopRef _CFRunLoopGet0b(pthread_t t) {
    if (pthread_equal(t, kNilPthreadT)) {
        t = pthread_main_thread_np();
    }
    __CFLock(&loopsLock);
    CFRunLoopRef loop = NULL;
    if (__CFRunLoops) {
        loop = (CFRunLoopRef)CFDictionaryGetValue(__CFRunLoops, pthreadPointer(t));
    }
    __CFUnlock(&loopsLock);
    return loop;
}
/// 定义移除所有 RunLoop中的事件源方法
static void __CFRunLoopRemoveAllSources(CFRunLoopRef rl, CFStringRef modeName);
#pragma mark - 线程销毁时 收到的回调
// Called for each thread as it exits
CF_PRIVATE void __CFFinalizeRunLoop(uintptr_t data) {
    CFRunLoopRef rl = NULL;
    /// 满足条件
    if (data <= 1) {
        /// 加锁 -> 判断是否有值 -> 取值 -> 释放值 -> 移除键值对 -> 解锁
        __CFLock(&loopsLock);
        if (__CFRunLoops) {
            rl = (CFRunLoopRef)CFDictionaryGetValue(__CFRunLoops, pthreadPointer(pthread_self()));
            if (rl) CFRetain(rl);
            CFDictionaryRemoveValue(__CFRunLoops, pthreadPointer(pthread_self()));
        }
        __CFUnlock(&loopsLock);
    } else {
        /// 不满足条件则注册回调
        _CFSetTSD(__CFTSDKeyRunLoopCntr, (void *)(data - 1), (void (*)(void *))__CFFinalizeRunLoop);
    }
    if (rl && CFRunLoopGetMain() != rl) { // protect against cooperative threads
        if (NULL != rl->_counterpart) {
            CFRelease(rl->_counterpart);
            rl->_counterpart = NULL;
        }
        // purge all sources before deallocation
        /// 移除所有的source
        CFArrayRef array = CFRunLoopCopyAllModes(rl);
        for (CFIndex idx = CFArrayGetCount(array); idx--;) {
            CFStringRef modeName = (CFStringRef)CFArrayGetValueAtIndex(array, idx);
            __CFRunLoopRemoveAllSources(rl, modeName);
        }
        __CFRunLoopRemoveAllSources(rl, kCFRunLoopCommonModes);
        CFRelease(array);
    }
    if (rl) CFRelease(rl);
}
/// 传入RunLoop 取得 RunLoop 所在的线程
pthread_t _CFRunLoopGet1(CFRunLoopRef rl) {
    return rl->_pthread;
}

// should only be called by Foundation
CF_EXPORT CFTypeRef _CFRunLoopGet2(CFRunLoopRef rl) {
    CFTypeRef ret = NULL;
    __CFLock(&loopsLock);
    ret = rl->_counterpart;
    __CFUnlock(&loopsLock);
    return ret;
}

// should only be called by Foundation
CF_EXPORT CFTypeRef _CFRunLoopGet2b(CFRunLoopRef rl) {
    return rl->_counterpart;
}

#if DEPLOYMENT_TARGET_MACOSX
/// 设置当前的RunLoop
void _CFRunLoopSetCurrent(CFRunLoopRef rl) {
    /// 不允许给主线程设置，因为一开始就创建好了
    if (pthread_main_np()) return;
    CFRunLoopRef currentLoop = CFRunLoopGetCurrent();
    /// 取得当前的RunLoop 与将要设置的对比
    if (rl != currentLoop) {
        /// retain 之前的 currentLoop 防止在这里就被释放了
        CFRetain(currentLoop); // avoid a deallocation of the currentLoop inside the lock
        /// 加锁
        __CFLock(&loopsLock);
        /// 如果 r1 不为空 则设置，反之则清空 全局字典值，
        if (rl) {
            CFDictionarySetValue(__CFRunLoops, pthreadPointer(pthread_self()), rl);
        } else {
            CFDictionaryRemoveValue(__CFRunLoops, pthreadPointer(pthread_self()));
        }
        /// 解锁 -> relaese current RunLoop
        __CFUnlock(&loopsLock);
        CFRelease(currentLoop);
        /// TSD 处理
        _CFSetTSD(__CFTSDKeyRunLoop, NULL, NULL);
        _CFSetTSD(__CFTSDKeyRunLoopCntr, 0, (void (*)(void *))__CFFinalizeRunLoop);
    }
}
#endif
/// 获取主线程的 RunLoop ，其实就是调用 _CFRunLoopGet0 传入主线程作为参数
CFRunLoopRef CFRunLoopGetMain(void) {
    CHECK_FOR_FORK();
    static CFRunLoopRef __main = NULL; // no retain needed
    if (!__main) __main = _CFRunLoopGet0(pthread_main_thread_np()); // no CAS needed
    return __main;
}
/// 获取当前的线程中的RunLoop 实际上是调用 _CFRunLoopGet0 传入参数 pthread_self()
CFRunLoopRef CFRunLoopGetCurrent(void) {
    CHECK_FOR_FORK();
    CFRunLoopRef rl = (CFRunLoopRef)_CFGetTSD(__CFTSDKeyRunLoop);
    if (rl) return rl;
    return _CFRunLoopGet0(pthread_self());
}
/// copy RunLoop里面的currentMode 昵称
CFStringRef CFRunLoopCopyCurrentMode(CFRunLoopRef rl) {
    CHECK_FOR_FORK();
    CFStringRef result = NULL;
    __CFRunLoopLock(rl);
    if (NULL != rl->_currentMode) {
        result = (CFStringRef)CFRetain(rl->_currentMode->_name);
    }
    __CFRunLoopUnlock(rl);
    return result;
}
/// 把 Mode Name 存入 Mode上下文数组中
static void __CFRunLoopGetModeName(const void *value, void *context) {
    CFRunLoopModeRef rlm = (CFRunLoopModeRef)value;
    CFMutableArrayRef array = (CFMutableArrayRef)context;
    CFArrayAppendValue(array, rlm->_name);
}
/// copy RunLoop 中 Modes 的昵称 返回数组
CFArrayRef CFRunLoopCopyAllModes(CFRunLoopRef rl) {
    CHECK_FOR_FORK();
    CFMutableArrayRef array;
    __CFRunLoopLock(rl);
    array = CFArrayCreateMutable(kCFAllocatorSystemDefault, CFSetGetCount(rl->_modes), &kCFTypeArrayCallBacks);
    CFSetApplyFunction(rl->_modes, (__CFRunLoopGetModeName), array);
    __CFRunLoopUnlock(rl);
    return array;
}
#pragma mark -  添加items 到CommonMode 里面
static void __CFRunLoopAddItemsToCommonMode(const void *value, void *ctx) {
    CFTypeRef item = (CFTypeRef)value;
    CFRunLoopRef rl = (CFRunLoopRef)(((CFTypeRef *)ctx)[0]);
    CFStringRef modeName = (CFStringRef)(((CFTypeRef *)ctx)[1]);
    if (CFGetTypeID(item) == CFRunLoopSourceGetTypeID()) {
        CFRunLoopAddSource(rl, (CFRunLoopSourceRef)item, modeName);
    } else if (CFGetTypeID(item) == CFRunLoopObserverGetTypeID()) {
        CFRunLoopAddObserver(rl, (CFRunLoopObserverRef)item, modeName);
    } else if (CFGetTypeID(item) == CFRunLoopTimerGetTypeID()) {
        CFRunLoopAddTimer(rl, (CFRunLoopTimerRef)item, modeName);
    }
}
#pragma mark - 添加item 到CommonMode 里面
static void __CFRunLoopAddItemToCommonModes(const void *value, void *ctx) {
    CFStringRef modeName = (CFStringRef)value;
    CFRunLoopRef rl = (CFRunLoopRef)(((CFTypeRef *)ctx)[0]);
    CFTypeRef item = (CFTypeRef)(((CFTypeRef *)ctx)[1]);
    if (CFGetTypeID(item) == CFRunLoopSourceGetTypeID()) {
        CFRunLoopAddSource(rl, (CFRunLoopSourceRef)item, modeName);
    } else if (CFGetTypeID(item) == CFRunLoopObserverGetTypeID()) {
        CFRunLoopAddObserver(rl, (CFRunLoopObserverRef)item, modeName);
    } else if (CFGetTypeID(item) == CFRunLoopTimerGetTypeID()) {
        CFRunLoopAddTimer(rl, (CFRunLoopTimerRef)item, modeName);
    }
}
#pragma mark - 移除 RunLoop中的Item
static void __CFRunLoopRemoveItemFromCommonModes(const void *value, void *ctx) {
    CFStringRef modeName = (CFStringRef)value;
    CFRunLoopRef rl = (CFRunLoopRef)(((CFTypeRef *)ctx)[0]);
    CFTypeRef item = (CFTypeRef)(((CFTypeRef *)ctx)[1]);
    if (CFGetTypeID(item) == CFRunLoopSourceGetTypeID()) {
        CFRunLoopRemoveSource(rl, (CFRunLoopSourceRef)item, modeName);
    } else if (CFGetTypeID(item) == CFRunLoopObserverGetTypeID()) {
        CFRunLoopRemoveObserver(rl, (CFRunLoopObserverRef)item, modeName);
    } else if (CFGetTypeID(item) == CFRunLoopTimerGetTypeID()) {
        CFRunLoopRemoveTimer(rl, (CFRunLoopTimerRef)item, modeName);
    }
}
#pragma mark - 根据 modeName 判断RunLoop中的 _commonModes 是否包含某个 Mode
CF_EXPORT Boolean _CFRunLoop01(CFRunLoopRef rl, CFStringRef modeName) {
    __CFRunLoopLock(rl);
    Boolean present = CFSetContainsValue(rl->_commonModes, modeName);
    __CFRunLoopUnlock(rl);
    return present; 
}
#pragma mark - 把Mode 添加到 commonMode 中
void CFRunLoopAddCommonMode(CFRunLoopRef rl, CFStringRef modeName) {
    CHECK_FOR_FORK();
    /// 如果RunLopp正在进行析构，则直接return
    if (__CFRunLoopIsDeallocating(rl)) return;
    /// 加锁
    __CFRunLoopLock(rl);
    /// commonModes不包含要添加的Mode才进行操作
    if (!CFSetContainsValue(rl->_commonModes, modeName)) {
        CFSetRef set = rl->_commonModeItems ? CFSetCreateCopy(kCFAllocatorSystemDefault, rl->_commonModeItems) : NULL;
        CFSetAddValue(rl->_commonModes, modeName);
        if (NULL != set) {
            CFTypeRef context[2] = {rl, modeName};
            /* add all common-modes items to new mode */
            /// 添加所有的
            CFSetApplyFunction(set, (__CFRunLoopAddItemsToCommonMode), (void *)context);
            CFRelease(set);
        }
    } else {
    }
    /// 解锁
    __CFRunLoopUnlock(rl);
}


static void __CFRUNLOOP_IS_SERVICING_THE_MAIN_DISPATCH_QUEUE__() __attribute__((noinline));
static void __CFRUNLOOP_IS_SERVICING_THE_MAIN_DISPATCH_QUEUE__(void *msg) {
    _dispatch_main_queue_callback_4CF(msg);
    asm __volatile__(""); // thwart tail-call optimization
}

static void __CFRUNLOOP_IS_CALLING_OUT_TO_AN_OBSERVER_CALLBACK_FUNCTION__() __attribute__((noinline));
static void __CFRUNLOOP_IS_CALLING_OUT_TO_AN_OBSERVER_CALLBACK_FUNCTION__(CFRunLoopObserverCallBack func, CFRunLoopObserverRef observer, CFRunLoopActivity activity, void *info) {
    if (func) {
        func(observer, activity, info);
    }
    asm __volatile__(""); // thwart tail-call optimization
}
/// RunLoop timer 回调
static void __CFRUNLOOP_IS_CALLING_OUT_TO_A_TIMER_CALLBACK_FUNCTION__() __attribute__((noinline));
static void __CFRUNLOOP_IS_CALLING_OUT_TO_A_TIMER_CALLBACK_FUNCTION__(CFRunLoopTimerCallBack func, CFRunLoopTimerRef timer, void *info) {
    if (func) {
        func(timer, info);
    }
    asm __volatile__(""); // thwart tail-call optimization
}
/// RunLoop timer block 回调
static void __CFRUNLOOP_IS_CALLING_OUT_TO_A_BLOCK__() __attribute__((noinline));
static void __CFRUNLOOP_IS_CALLING_OUT_TO_A_BLOCK__(void (^block)(void)) {
    if (block) {
        block();
    }
    asm __volatile__(""); // thwart tail-call optimization
}
#pragma mark -  做 blocks 回调处理
static Boolean __CFRunLoopDoBlocks(CFRunLoopRef rl, CFRunLoopModeRef rlm) { // Call with rl and rlm locked
    if (!rl->_blocks_head) return false;
    if (!rlm || !rlm->_name) return false;
    Boolean did = false;
    /// 头和尾指针
    struct _block_item *head = rl->_blocks_head;
    struct _block_item *tail = rl->_blocks_tail;
    rl->_blocks_head = NULL;
    rl->_blocks_tail = NULL;
    /// commonModes 和 curMode
    CFSetRef commonModes = rl->_commonModes;
    CFStringRef curMode = rlm->_name;
    __CFRunLoopModeUnlock(rlm);
    __CFRunLoopUnlock(rl);
    
    struct _block_item *prev = NULL;
    struct _block_item *item = head;
    while (item) {
        struct _block_item *curr = item;
        item = item->_next;
        Boolean doit = false;
        /// 当前mode与制定mode相等或者当前mode为commonMode（此处为一个字符串）且commonMode（此处为一个集合，若有不懂，请看runLoop结构）这个集合中包含指定mode。
        if (CFStringGetTypeID() == CFGetTypeID(curr->_mode)) {
            doit = CFEqual(curr->_mode, curMode) || (CFEqual(curr->_mode, kCFRunLoopCommonModes) && CFSetContainsValue(commonModes, curMode));
        } else {
            doit = CFSetContainsValue((CFSetRef)curr->_mode, curMode) || (CFSetContainsValue((CFSetRef)curr->_mode, kCFRunLoopCommonModes) && CFSetContainsValue(commonModes, curMode));
        }
        if (!doit) prev = curr;
        if (doit) {
            if (prev) prev->_next = item;
            if (curr == head) head = item;
            if (curr == tail) tail = prev;
            void (^block)(void) = curr->_block;
            CFRelease(curr->_mode);
            free(curr);
            if (doit) {
                /// 执行Block回调
                __CFRUNLOOP_IS_CALLING_OUT_TO_A_BLOCK__(block);
                did = true;
            }
            Block_release(block); // do this before relocking to prevent deadlocks where some yahoo wants to run the run loop reentrantly from their dealloc
        }
    }
    __CFRunLoopLock(rl);
    __CFRunLoopModeLock(rlm);
    if (head) {
        tail->_next = rl->_blocks_head;
        rl->_blocks_head = head;
        if (!rl->_blocks_tail) rl->_blocks_tail = tail;
    }
    return did;
}

/* rl is locked, rlm is locked on entrance and exit */
#pragma mark - 观察者回调
static void __CFRunLoopDoObservers() __attribute__((noinline));
static void __CFRunLoopDoObservers(CFRunLoopRef rl, CFRunLoopModeRef rlm, CFRunLoopActivity activity) {	/* DOES CALLOUT */
    CHECK_FOR_FORK();
    /// 判断是否有观察者
    CFIndex cnt = rlm->_observers ? CFArrayGetCount(rlm->_observers) : 0;
    if (cnt < 1) return;
    
    /* Fire the observers */
    STACK_BUFFER_DECL(CFRunLoopObserverRef, buffer, (cnt <= 1024) ? cnt : 1);
    CFRunLoopObserverRef *collectedObservers = (cnt <= 1024) ? buffer : (CFRunLoopObserverRef *)malloc(cnt * sizeof(CFRunLoopObserverRef));
    CFIndex obs_cnt = 0;
    /// 遍历rlm->_observers，放到collectedObservers数组中
    for (CFIndex idx = 0; idx < cnt; idx++) {
        CFRunLoopObserverRef rlo = (CFRunLoopObserverRef)CFArrayGetValueAtIndex(rlm->_observers, idx);
        if (0 != (rlo->_activities & activity) && __CFIsValid(rlo) && !__CFRunLoopObserverIsFiring(rlo)) {
            collectedObservers[obs_cnt++] = (CFRunLoopObserverRef)CFRetain(rlo);
        }
    }
    __CFRunLoopModeUnlock(rlm);
    __CFRunLoopUnlock(rl);
    /// 遍历collectedObservers，调用rlo->_callout，外部设置好的CFRunLoopObserverRef回调函数
    /* 回顾一下添加obserber监听runloop状态的代码,两部分就对应上了
     CFRunLoopRef runLoop = CFRunLoopGetCurrent();
     CFStringRef runLoopMode = kCFRunLoopDefaultMode;
     //此处传入的block应该就是赋值给rlo->_callout
     CFRunLoopObserverRef observer = CFRunLoopObserverCreateWithHandler(kCFAllocatorDefault, kCFRunLoopBeforeWaiting, true, 0, ^(CFRunLoopObserverRef observer, CFRunLoopActivity _) {
     });
     // observer被放到了rlm->_observers数组中
     CFRunLoopAddObserver(runLoop, observer, runLoopMode);
     */
    for (CFIndex idx = 0; idx < obs_cnt; idx++) {
        CFRunLoopObserverRef rlo = collectedObservers[idx];
        __CFRunLoopObserverLock(rlo);
        if (__CFIsValid(rlo)) {
            Boolean doInvalidate = !__CFRunLoopObserverRepeats(rlo);
            __CFRunLoopObserverSetFiring(rlo);
            __CFRunLoopObserverUnlock(rlo);
            /// 调用callback
            __CFRUNLOOP_IS_CALLING_OUT_TO_AN_OBSERVER_CALLBACK_FUNCTION__(rlo->_callout, rlo, activity, rlo->_context.info);
            if (doInvalidate) {
                CFRunLoopObserverInvalidate(rlo);
            }
            __CFRunLoopObserverUnsetFiring(rlo);
        } else {
            __CFRunLoopObserverUnlock(rlo);
        }
        CFRelease(rlo);
    }
    __CFRunLoopLock(rl);
    __CFRunLoopModeLock(rlm);
    /// 释放 collectedObservers
    if (collectedObservers != buffer) free(collectedObservers);
}
/// 比较两个Sourece的优先级
static CFComparisonResult __CFRunLoopSourceComparator(const void *val1, const void *val2, void *context) {
    CFRunLoopSourceRef o1 = (CFRunLoopSourceRef)val1;
    CFRunLoopSourceRef o2 = (CFRunLoopSourceRef)val2;
    if (o1->_order < o2->_order) return kCFCompareLessThan;
    if (o2->_order < o1->_order) return kCFCompareGreaterThan;
    return kCFCompareEqualTo;
}
/// 收集Source0
static void __CFRunLoopCollectSources0(const void *value, void *context) {
    CFRunLoopSourceRef rls = (CFRunLoopSourceRef)value;
    CFTypeRef *sources = (CFTypeRef *)context;
    /// 这里有个条件是 必须是 被打了Signaled标记的才会收集
    if (0 == rls->_context.version0.version && __CFIsValid(rls) && __CFRunLoopSourceIsSignaled(rls)) {
        if (NULL == *sources) {
            *sources = CFRetain(rls);
        } else if (CFGetTypeID(*sources) == CFRunLoopSourceGetTypeID()) {
            CFTypeRef oldrls = *sources;
            *sources = CFArrayCreateMutable(kCFAllocatorSystemDefault, 0, &kCFTypeArrayCallBacks);
            CFArrayAppendValue((CFMutableArrayRef)*sources, oldrls);
            CFArrayAppendValue((CFMutableArrayRef)*sources, rls);
            CFRelease(oldrls);
        } else {
            CFArrayAppendValue((CFMutableArrayRef)*sources, rls);
        }
    }
}
#pragma mark - Source0 处理原事件的函数
static void __CFRUNLOOP_IS_CALLING_OUT_TO_A_SOURCE0_PERFORM_FUNCTION__() __attribute__((noinline));
static void __CFRUNLOOP_IS_CALLING_OUT_TO_A_SOURCE0_PERFORM_FUNCTION__(void (*perform)(void *), void *info) {
    if (perform) {
        perform(info);
    }
    asm __volatile__(""); // thwart tail-call optimization
}
#pragma mark -  Source1 处理原事件的函数
static void __CFRUNLOOP_IS_CALLING_OUT_TO_A_SOURCE1_PERFORM_FUNCTION__() __attribute__((noinline));
static void __CFRUNLOOP_IS_CALLING_OUT_TO_A_SOURCE1_PERFORM_FUNCTION__(
#if DEPLOYMENT_TARGET_MACOSX || DEPLOYMENT_TARGET_EMBEDDED || DEPLOYMENT_TARGET_EMBEDDED_MINI
                                                                       void *(*perform)(void *msg, CFIndex size, CFAllocatorRef allocator, void *info),
                                                                       mach_msg_header_t *msg, CFIndex size, mach_msg_header_t **reply,
#else
                                                                       void (*perform)(void *),
#endif
                                                                       void *info) {
    if (perform) {
#if DEPLOYMENT_TARGET_MACOSX || DEPLOYMENT_TARGET_EMBEDDED || DEPLOYMENT_TARGET_EMBEDDED_MINI
        *reply = perform(msg, size, kCFAllocatorSystemDefault, info);
#else
        perform(info);
#endif
    }
    asm __volatile__(""); // thwart tail-call optimization
}

/* rl is locked, rlm is locked on entrance and exit */
#pragma mark - Source0 处理
static Boolean __CFRunLoopDoSources0(CFRunLoopRef rl, CFRunLoopModeRef rlm, Boolean stopAfterHandle) __attribute__((noinline));
static Boolean __CFRunLoopDoSources0(CFRunLoopRef rl, CFRunLoopModeRef rlm, Boolean stopAfterHandle) {	/* DOES CALLOUT */
    CHECK_FOR_FORK();
    CFTypeRef sources = NULL;
    Boolean sourceHandled = false;
    
    /* Fire the version 0 sources */
    if (NULL != rlm->_sources0 && 0 < CFSetGetCount(rlm->_sources0)) {
        CFSetApplyFunction(rlm->_sources0, (__CFRunLoopCollectSources0), &sources);
    }
    if (NULL != sources) {
        __CFRunLoopModeUnlock(rlm);
        __CFRunLoopUnlock(rl);
        // sources is either a single (retained) CFRunLoopSourceRef or an array of (retained) CFRunLoopSourceRef
        if (CFGetTypeID(sources) == CFRunLoopSourceGetTypeID()) {
            CFRunLoopSourceRef rls = (CFRunLoopSourceRef)sources;
            __CFRunLoopSourceLock(rls);
            if (__CFRunLoopSourceIsSignaled(rls)) {
                __CFRunLoopSourceUnsetSignaled(rls);
                if (__CFIsValid(rls)) {
                    __CFRunLoopSourceUnlock(rls);
                    __CFRUNLOOP_IS_CALLING_OUT_TO_A_SOURCE0_PERFORM_FUNCTION__(rls->_context.version0.perform, rls->_context.version0.info);
                    CHECK_FOR_FORK();
                    sourceHandled = true;
                } else {
                    __CFRunLoopSourceUnlock(rls);
                }
            } else {
                __CFRunLoopSourceUnlock(rls);
            }
        } else {
            CFIndex cnt = CFArrayGetCount((CFArrayRef)sources);
            CFArraySortValues((CFMutableArrayRef)sources, CFRangeMake(0, cnt), (__CFRunLoopSourceComparator), NULL);
            for (CFIndex idx = 0; idx < cnt; idx++) {
                CFRunLoopSourceRef rls = (CFRunLoopSourceRef)CFArrayGetValueAtIndex((CFArrayRef)sources, idx);
                __CFRunLoopSourceLock(rls);
                if (__CFRunLoopSourceIsSignaled(rls)) {
                    __CFRunLoopSourceUnsetSignaled(rls);
                    if (__CFIsValid(rls)) {
                        __CFRunLoopSourceUnlock(rls);
                       /// 处理原事件源
                        __CFRUNLOOP_IS_CALLING_OUT_TO_A_SOURCE0_PERFORM_FUNCTION__(rls->_context.version0.perform, rls->_context.version0.info);
                        CHECK_FOR_FORK();
                        sourceHandled = true;
                    } else {
                        __CFRunLoopSourceUnlock(rls);
                    }
                } else {
                    __CFRunLoopSourceUnlock(rls);
                }
                if (stopAfterHandle && sourceHandled) {
                    break;
                }
            }
        }
        CFRelease(sources);
        __CFRunLoopLock(rl);
        __CFRunLoopModeLock(rlm);
    }
    return sourceHandled;
}

// msg, size and reply are unused on Windows
#pragma mark - 处理Source1
static Boolean __CFRunLoopDoSource1() __attribute__((noinline));
static Boolean __CFRunLoopDoSource1(CFRunLoopRef rl, CFRunLoopModeRef rlm, CFRunLoopSourceRef rls
#if DEPLOYMENT_TARGET_MACOSX || DEPLOYMENT_TARGET_EMBEDDED || DEPLOYMENT_TARGET_EMBEDDED_MINI
                                    , mach_msg_header_t *msg, CFIndex size, mach_msg_header_t **reply
#endif
) {	/* DOES CALLOUT */
    CHECK_FOR_FORK();
    Boolean sourceHandled = false;
    
    /* Fire a version 1 source */
    CFRetain(rls);
    __CFRunLoopModeUnlock(rlm);
    __CFRunLoopUnlock(rl);
    __CFRunLoopSourceLock(rls);
    if (__CFIsValid(rls)) {
        __CFRunLoopSourceUnsetSignaled(rls);
        __CFRunLoopSourceUnlock(rls);
        __CFRunLoopDebugInfoForRunLoopSource(rls);
        /// 处理原事件源
        __CFRUNLOOP_IS_CALLING_OUT_TO_A_SOURCE1_PERFORM_FUNCTION__(rls->_context.version1.perform,
#if DEPLOYMENT_TARGET_MACOSX || DEPLOYMENT_TARGET_EMBEDDED || DEPLOYMENT_TARGET_EMBEDDED_MINI
                                                                   msg, size, reply,
#endif
                                                                   rls->_context.version1.info);
        CHECK_FOR_FORK();
        sourceHandled = true;
    } else {
        if (_LogCFRunLoop) { CFLog(kCFLogLevelDebug, CFSTR("%p (%s) __CFRunLoopDoSource1 rls %p is invalid"), CFRunLoopGetCurrent(), *_CFGetProgname(), rls); }
        __CFRunLoopSourceUnlock(rls);
    }
    CFRelease(rls);
    __CFRunLoopLock(rl);
    __CFRunLoopModeLock(rlm);
    return sourceHandled;
}
// 根据timer的_fireTSR时间字段，利用二分查找的算法，将timer插入到已按照时间排列好的timerArray（rlm_timers）中，
// 这个rlm_timers的array是按照fireTSR的升序排列的
static CFIndex __CFRunLoopInsertionIndexInTimerArray(CFArrayRef array, CFRunLoopTimerRef rlt) __attribute__((noinline));
static CFIndex __CFRunLoopInsertionIndexInTimerArray(CFArrayRef array, CFRunLoopTimerRef rlt) {
    CFIndex cnt = CFArrayGetCount(array);
    if (cnt <= 0) {
        return 0;
    }
    if (256 < cnt) {
        CFRunLoopTimerRef item = (CFRunLoopTimerRef)CFArrayGetValueAtIndex(array, cnt - 1);
        if (item->_fireTSR <= rlt->_fireTSR) {
            return cnt;
        }
        item = (CFRunLoopTimerRef)CFArrayGetValueAtIndex(array, 0);
        if (rlt->_fireTSR < item->_fireTSR) {
            return 0;
        }
    }
    
    CFIndex add = (1 << flsl(cnt)) * 2;
    CFIndex idx = 0;
    Boolean lastTestLEQ;
    do {
        add = add / 2;
        lastTestLEQ = false;
        CFIndex testIdx = idx + add;
        if (testIdx < cnt) {
            CFRunLoopTimerRef item = (CFRunLoopTimerRef)CFArrayGetValueAtIndex(array, testIdx);
            if (item->_fireTSR <= rlt->_fireTSR) {
                idx = testIdx;
                lastTestLEQ = true;
            }
        }
    } while (0 < add);
    
    return lastTestLEQ ? idx + 1 : idx;
}
#pragma mark - 处理Mode的下个到来的Timer
// 根据mode中的最前面的那个timer的触发时间，将其通过dispatch_source_set_runloop_timer或者mk_timer的方式注册。
static void __CFArmNextTimerInMode(CFRunLoopModeRef rlm, CFRunLoopRef rl) {    
    uint64_t nextHardDeadline = UINT64_MAX;
    uint64_t nextSoftDeadline = UINT64_MAX;
    
    if (rlm->_timers) {
        // Look at the list of timers. We will calculate two TSR values; the next soft and next hard deadline.
        // The next soft deadline is the first time we can fire any timer. This is the fire date of the first timer in our sorted list of timers.
        // The next hard deadline is the last time at which we can fire the timer before we've moved out of the allowable tolerance of the timers in our list.
        for (CFIndex idx = 0, cnt = CFArrayGetCount(rlm->_timers); idx < cnt; idx++) {
            CFRunLoopTimerRef t = (CFRunLoopTimerRef)CFArrayGetValueAtIndex(rlm->_timers , idx);
            // discount timers currently firing
            if (__CFRunLoopTimerIsFiring(t)) continue;
            
            int32_t err = CHECKINT_NO_ERROR;
            // SoftDeadline是理应触发的时间
            uint64_t oneTimerSoftDeadline = t->_fireTSR;
            // HardDeadline是理应触发的时间加上tolerance
            uint64_t oneTimerHardDeadline = check_uint64_add(t->_fireTSR, __CFTimeIntervalToTSR(t->_tolerance), &err);
            if (err != CHECKINT_NO_ERROR) oneTimerHardDeadline = UINT64_MAX;
            
            // We can stop searching if the soft deadline for this timer exceeds the current hard deadline. Otherwise, later timers with lower tolerance could still have earlier hard deadlines.
            // 通过这几行代码对deadline进行修正，保证前边的长tolerance的timer不会影响后面的timer的触发
            if (oneTimerSoftDeadline > nextHardDeadline) {
                break;
            }
            
            if (oneTimerSoftDeadline < nextSoftDeadline) {
                nextSoftDeadline = oneTimerSoftDeadline;
            }
            
            if (oneTimerHardDeadline < nextHardDeadline) {
                nextHardDeadline = oneTimerHardDeadline;
            }
        }
        
        if (nextSoftDeadline < UINT64_MAX && (nextHardDeadline != rlm->_timerHardDeadline || nextSoftDeadline != rlm->_timerSoftDeadline)) {
            if (CFRUNLOOP_NEXT_TIMER_ARMED_ENABLED()) {
                CFRUNLOOP_NEXT_TIMER_ARMED((unsigned long)(nextSoftDeadline - mach_absolute_time()));
            }
#if USE_DISPATCH_SOURCE_FOR_TIMERS
            // We're going to hand off the range of allowable timer fire date to dispatch and let it fire when appropriate for the system.
            uint64_t leeway = __CFTSRToNanoseconds(nextHardDeadline - nextSoftDeadline);
            dispatch_time_t deadline = __CFTSRToDispatchTime(nextSoftDeadline);
#if USE_MK_TIMER_TOO
            
            if (leeway > 0) {
                // 对于有leeway的情况（有tolerance的情况），只采用_dispatch_source_set_runloop_timer_4CF的方法
                // Only use the dispatch timer if we have any leeway
                // <rdar://problem/14447675>
                
                // Cancel the mk timer
                if (rlm->_mkTimerArmed && rlm->_timerPort) {
                    AbsoluteTime dummy;
                    mk_timer_cancel(rlm->_timerPort, &dummy);
                    rlm->_mkTimerArmed = false;
                }
                
                // Arm the dispatch timer
                _dispatch_source_set_runloop_timer_4CF(rlm->_timerSource, deadline, DISPATCH_TIME_FOREVER, leeway);
                rlm->_dispatchTimerArmed = true;
            } else {
                // 对于leeway为0的情况（无tolerance的情况）,采用mk_timer的方式
                // Cancel the dispatch timer
                if (rlm->_dispatchTimerArmed) {
                    // Cancel the dispatch timer
                    _dispatch_source_set_runloop_timer_4CF(rlm->_timerSource, DISPATCH_TIME_FOREVER, DISPATCH_TIME_FOREVER, 888);
                    rlm->_dispatchTimerArmed = false;
                }
                
                // Arm the mk timer
                if (rlm->_timerPort) {
                    mk_timer_arm(rlm->_timerPort, __CFUInt64ToAbsoluteTime(nextSoftDeadline));
                    rlm->_mkTimerArmed = true;
                }
            }
#else
            _dispatch_source_set_runloop_timer_4CF(rlm->_timerSource, deadline, DISPATCH_TIME_FOREVER, leeway);
#endif
#else
            if (rlm->_timerPort) {
                mk_timer_arm(rlm->_timerPort, __CFUInt64ToAbsoluteTime(nextSoftDeadline));
            }
#endif
        } else if (nextSoftDeadline == UINT64_MAX) {
            // Disarm the timers - there is no timer scheduled
            // 移除timer
            if (rlm->_mkTimerArmed && rlm->_timerPort) {
                AbsoluteTime dummy;
                mk_timer_cancel(rlm->_timerPort, &dummy);
                rlm->_mkTimerArmed = false;
            }
            
#if USE_DISPATCH_SOURCE_FOR_TIMERS
            if (rlm->_dispatchTimerArmed) {
                _dispatch_source_set_runloop_timer_4CF(rlm->_timerSource, DISPATCH_TIME_FOREVER, DISPATCH_TIME_FOREVER, 333);
                rlm->_dispatchTimerArmed = false;
            }
#endif
        }
    }
    rlm->_timerHardDeadline = nextHardDeadline;
    rlm->_timerSoftDeadline = nextSoftDeadline;
}

// call with rlm and its run loop locked, and the TSRLock locked; rlt not locked; returns with same state
/// 重新排列这个mode中的所有timer触发时刻
static void __CFRepositionTimerInMode(CFRunLoopModeRef rlm, CFRunLoopTimerRef rlt, Boolean isInArray) __attribute__((noinline));
static void __CFRepositionTimerInMode(CFRunLoopModeRef rlm, CFRunLoopTimerRef rlt, Boolean isInArray) {
    if (!rlt) return;
    
    CFMutableArrayRef timerArray = rlm->_timers;
    if (!timerArray) return;
    Boolean found = false;
    
    // If we know in advance that the timer is not in the array (just being added now) then we can skip this search
    if (isInArray) {
        CFIndex idx = CFArrayGetFirstIndexOfValue(timerArray, CFRangeMake(0, CFArrayGetCount(timerArray)), rlt);
        if (kCFNotFound != idx) {
            CFRetain(rlt);
            CFArrayRemoveValueAtIndex(timerArray, idx);
            found = true;
        }
    }
    if (!found && isInArray) return;
    /// 根据timer的_fireTSR时间字段，利用二分查找的算法，将timer插入到已按照时间排列好的timerArray（rlm_timers）中，
    /// 这个rlm_timers的array是按照fireTSR的升序排列的
    CFIndex newIdx = __CFRunLoopInsertionIndexInTimerArray(timerArray, rlt);
    CFArrayInsertValueAtIndex(timerArray, newIdx, rlt);
    // 根据mode中的最前面的那个timer的触发时间，将其通过dispatch_source_set_runloop_timer或者mk_timer的方式注册。
    __CFArmNextTimerInMode(rlm, rlt->_runLoop);
    if (isInArray) CFRelease(rlt);
}

#pragma mark - 处理Timer
// mode and rl are locked on entry and exit
static Boolean __CFRunLoopDoTimer(CFRunLoopRef rl, CFRunLoopModeRef rlm, CFRunLoopTimerRef rlt) {	/* DOES CALLOUT */
    Boolean timerHandled = false;
    uint64_t oldFireTSR = 0;
    
    /* Fire a timer */
    CFRetain(rlt);
    __CFRunLoopTimerLock(rlt);
    
    if (__CFIsValid(rlt) && rlt->_fireTSR <= mach_absolute_time() && !__CFRunLoopTimerIsFiring(rlt) && rlt->_runLoop == rl) {
        void *context_info = NULL;
        void (*context_release)(const void *) = NULL;
        if (rlt->_context.retain) {
            context_info = (void *)rlt->_context.retain(rlt->_context.info);
            context_release = rlt->_context.release;
        } else {
            context_info = rlt->_context.info;
        }
        Boolean doInvalidate = (0.0 == rlt->_interval);
        __CFRunLoopTimerSetFiring(rlt);
        // Just in case the next timer has exactly the same deadlines as this one, we reset these values so that the arm next timer code can correctly find the next timer in the list and arm the underlying timer.
        rlm->_timerSoftDeadline = UINT64_MAX;
        rlm->_timerHardDeadline = UINT64_MAX;
        __CFRunLoopTimerUnlock(rlt);
        __CFRunLoopTimerFireTSRLock();
        oldFireTSR = rlt->_fireTSR;
        __CFRunLoopTimerFireTSRUnlock();
        
        __CFArmNextTimerInMode(rlm, rl);
        
        __CFRunLoopModeUnlock(rlm);
        __CFRunLoopUnlock(rl);
        __CFRUNLOOP_IS_CALLING_OUT_TO_A_TIMER_CALLBACK_FUNCTION__(rlt->_callout, rlt, context_info);
        CHECK_FOR_FORK();
        if (doInvalidate) {
            CFRunLoopTimerInvalidate(rlt);      /* DOES CALLOUT */
        }
        if (context_release) {
            context_release(context_info);
        }
        __CFRunLoopLock(rl);
        __CFRunLoopModeLock(rlm);
        __CFRunLoopTimerLock(rlt);
        timerHandled = true;
        __CFRunLoopTimerUnsetFiring(rlt);
    }
    if (__CFIsValid(rlt) && timerHandled) {
        /* This is just a little bit tricky: we want to support calling
         * CFRunLoopTimerSetNextFireDate() from within the callout and
         * honor that new time here if it is a later date, otherwise
         * it is completely ignored. */
        if (oldFireTSR < rlt->_fireTSR) {
            /* Next fire TSR was set, and set to a date after the previous
             * fire date, so we honor it. */
            __CFRunLoopTimerUnlock(rlt);
            // The timer was adjusted and repositioned, during the
            // callout, but if it was still the min timer, it was
            // skipped because it was firing.  Need to redo the
            // min timer calculation in case rlt should now be that
            // timer instead of whatever was chosen.
            __CFArmNextTimerInMode(rlm, rl);
        } else {
            uint64_t nextFireTSR = 0LL;
            uint64_t intervalTSR = 0LL;
            if (rlt->_interval <= 0.0) {
            } else if (TIMER_INTERVAL_LIMIT < rlt->_interval) {
                intervalTSR = __CFTimeIntervalToTSR(TIMER_INTERVAL_LIMIT);
            } else {
                intervalTSR = __CFTimeIntervalToTSR(rlt->_interval);
            }
            if (LLONG_MAX - intervalTSR <= oldFireTSR) {
                nextFireTSR = LLONG_MAX;
            } else {
                if (intervalTSR == 0) {
                    // 15304159: Make sure we don't accidentally loop forever here
                    CRSetCrashLogMessage("A CFRunLoopTimer with an interval of 0 is set to repeat");
                    HALT;
                }
                uint64_t currentTSR = mach_absolute_time();
                nextFireTSR = oldFireTSR;
                while (nextFireTSR <= currentTSR) {
                    nextFireTSR += intervalTSR;
                }
            }
            CFRunLoopRef rlt_rl = rlt->_runLoop;
            if (rlt_rl) {
                CFRetain(rlt_rl);
                CFIndex cnt = CFSetGetCount(rlt->_rlModes);
                STACK_BUFFER_DECL(CFTypeRef, modes, cnt);
                CFSetGetValues(rlt->_rlModes, (const void **)modes);
                // To avoid A->B, B->A lock ordering issues when coming up
                // towards the run loop from a source, the timer has to be
                // unlocked, which means we have to protect from object
                // invalidation, although that's somewhat expensive.
                for (CFIndex idx = 0; idx < cnt; idx++) {
                    CFRetain(modes[idx]);
                }
                __CFRunLoopTimerUnlock(rlt);
                for (CFIndex idx = 0; idx < cnt; idx++) {
                    CFStringRef name = (CFStringRef)modes[idx];
                    modes[idx] = (CFTypeRef)__CFRunLoopFindMode(rlt_rl, name, false);
                    CFRelease(name);
                }
                __CFRunLoopTimerFireTSRLock();
                rlt->_fireTSR = nextFireTSR;
                rlt->_nextFireDate = CFAbsoluteTimeGetCurrent() + __CFTimeIntervalUntilTSR(nextFireTSR);
                for (CFIndex idx = 0; idx < cnt; idx++) {
                    CFRunLoopModeRef rlm = (CFRunLoopModeRef)modes[idx];
                    if (rlm) {
                        __CFRepositionTimerInMode(rlm, rlt, true);
                    }
                }
                __CFRunLoopTimerFireTSRUnlock();
                for (CFIndex idx = 0; idx < cnt; idx++) {
                    __CFRunLoopModeUnlock((CFRunLoopModeRef)modes[idx]);
                }
                CFRelease(rlt_rl);
            } else {
                __CFRunLoopTimerUnlock(rlt);
                __CFRunLoopTimerFireTSRLock();
                rlt->_fireTSR = nextFireTSR;
                rlt->_nextFireDate = CFAbsoluteTimeGetCurrent() + __CFTimeIntervalUntilTSR(nextFireTSR);
                __CFRunLoopTimerFireTSRUnlock();
            }
        }
    } else {
        __CFRunLoopTimerUnlock(rlt);
    }
    CFRelease(rlt);
    return timerHandled;
}


// rl and rlm are locked on entry and exit
static Boolean __CFRunLoopDoTimers(CFRunLoopRef rl, CFRunLoopModeRef rlm, uint64_t limitTSR) {	/* DOES CALLOUT */
    /// 遍历runLoopMode维护的Timers数组，取其中有效的timer并加入新临时数组
    Boolean timerHandled = false;
    CFMutableArrayRef timers = NULL;
    for (CFIndex idx = 0, cnt = rlm->_timers ? CFArrayGetCount(rlm->_timers) : 0; idx < cnt; idx++) {
        /// timer
        CFRunLoopTimerRef rlt = (CFRunLoopTimerRef)CFArrayGetValueAtIndex(rlm->_timers, idx);
        
        if (__CFIsValid(rlt) && !__CFRunLoopTimerIsFiring(rlt)) {
            if (rlt->_fireTSR <= limitTSR) {
                if (!timers) timers = CFArrayCreateMutable(kCFAllocatorSystemDefault, 0, &kCFTypeArrayCallBacks);
                CFArrayAppendValue(timers, rlt);
            }
        }
    }
    /// 遍历临时数组，每个有效Timer调用__CFRunLoopDoTimer
    for (CFIndex idx = 0, cnt = timers ? CFArrayGetCount(timers) : 0; idx < cnt; idx++) {
        CFRunLoopTimerRef rlt = (CFRunLoopTimerRef)CFArrayGetValueAtIndex(timers, idx);
        /// do timer
        Boolean did = __CFRunLoopDoTimer(rl, rlm, rlt);
        timerHandled = timerHandled || did;
    }
    if (timers) CFRelease(timers);
    return timerHandled;
}

/// 判断RunLoop 是否完成
CF_EXPORT Boolean _CFRunLoopFinished(CFRunLoopRef rl, CFStringRef modeName) {
    CHECK_FOR_FORK();
    CFRunLoopModeRef rlm;
    Boolean result = false;
    __CFRunLoopLock(rl);
    rlm = __CFRunLoopFindMode(rl, modeName, false);
    if (NULL == rlm || __CFRunLoopModeIsEmpty(rl, rlm, NULL)) {
        result = true;
    }
    if (rlm) __CFRunLoopModeUnlock(rlm);
    __CFRunLoopUnlock(rl);
    return result;
}

#if DEPLOYMENT_TARGET_MACOSX || DEPLOYMENT_TARGET_EMBEDDED || DEPLOYMENT_TARGET_EMBEDDED_MINI

#define TIMEOUT_INFINITY (~(mach_msg_timeout_t)0)
/// source 1 的 内核 mach msg port 处理 线程会进入休眠
static Boolean __CFRunLoopServiceMachPort(mach_port_name_t port, mach_msg_header_t **buffer, size_t buffer_size, mach_port_t *livePort, mach_msg_timeout_t timeout, voucher_mach_msg_state_t *voucherState, voucher_t *voucherCopy) {
    Boolean originalBuffer = true;
    kern_return_t ret = KERN_SUCCESS;
    for (;;) {		/* In that sleep of death what nightmares may come ... */
        mach_msg_header_t *msg = (mach_msg_header_t *)*buffer;
        msg->msgh_bits = 0;
        msg->msgh_local_port = port;
        msg->msgh_remote_port = MACH_PORT_NULL;
        msg->msgh_size = buffer_size;
        msg->msgh_id = 0;
        if (TIMEOUT_INFINITY == timeout) { CFRUNLOOP_SLEEP(); } else { CFRUNLOOP_POLL(); }
        ret = mach_msg(msg, MACH_RCV_MSG|(voucherState ? MACH_RCV_VOUCHER : 0)|MACH_RCV_LARGE|((TIMEOUT_INFINITY != timeout) ? MACH_RCV_TIMEOUT : 0)|MACH_RCV_TRAILER_TYPE(MACH_MSG_TRAILER_FORMAT_0)|MACH_RCV_TRAILER_ELEMENTS(MACH_RCV_TRAILER_AV), 0, msg->msgh_size, port, timeout, MACH_PORT_NULL);
        
        // Take care of all voucher-related work right after mach_msg.
        // If we don't release the previous voucher we're going to leak it.
        voucher_mach_msg_revert(*voucherState);
        
        // Someone will be responsible for calling voucher_mach_msg_revert. This call makes the received voucher the current one.
        *voucherState = voucher_mach_msg_adopt(msg);
        
        if (voucherCopy) {
            if (*voucherState != VOUCHER_MACH_MSG_STATE_UNCHANGED) {
                // Caller requested a copy of the voucher at this point. By doing this right next to mach_msg we make sure that no voucher has been set in between the return of mach_msg and the use of the voucher copy.
                // CFMachPortBoost uses the voucher to drop importance explicitly. However, we want to make sure we only drop importance for a new voucher (not unchanged), so we only set the TSD when the voucher is not state_unchanged.
                *voucherCopy = voucher_copy();
            } else {
                *voucherCopy = NULL;
            }
        }
        
        CFRUNLOOP_WAKEUP(ret);
        if (MACH_MSG_SUCCESS == ret) {
            *livePort = msg ? msg->msgh_local_port : MACH_PORT_NULL;
            return true;
        }
        if (MACH_RCV_TIMED_OUT == ret) {
            if (!originalBuffer) free(msg);
            *buffer = NULL;
            *livePort = MACH_PORT_NULL;
            return false;
        }
        if (MACH_RCV_TOO_LARGE != ret) break;
        buffer_size = round_msg(msg->msgh_size + MAX_TRAILER_SIZE);
        if (originalBuffer) *buffer = NULL;
        originalBuffer = false;
        *buffer = realloc(*buffer, buffer_size);
    }
    HALT;
    return false;
}

#elif DEPLOYMENT_TARGET_WINDOWS

#define TIMEOUT_INFINITY INFINITE

// pass in either a portSet or onePort
static Boolean __CFRunLoopWaitForMultipleObjects(__CFPortSet portSet, HANDLE *onePort, DWORD timeout, DWORD mask, HANDLE *livePort, Boolean *msgReceived) {
    DWORD waitResult = WAIT_TIMEOUT;
    HANDLE handleBuf[MAXIMUM_WAIT_OBJECTS];
    HANDLE *handles = NULL;
    uint32_t handleCount = 0;
    Boolean freeHandles = false;
    Boolean result = false;
    
    if (portSet) {
        // copy out the handles to be safe from other threads at work
        handles = __CFPortSetGetPorts(portSet, handleBuf, MAXIMUM_WAIT_OBJECTS, &handleCount);
        freeHandles = (handles != handleBuf);
    } else {
        handles = onePort;
        handleCount = 1;
        freeHandles = FALSE;
    }
    
    // The run loop mode and loop are already in proper unlocked state from caller
    waitResult = MsgWaitForMultipleObjectsEx(__CFMin(handleCount, MAXIMUM_WAIT_OBJECTS), handles, timeout, mask, MWMO_INPUTAVAILABLE);
    
    CFAssert2(waitResult != WAIT_FAILED, __kCFLogAssertion, "%s(): error %d from MsgWaitForMultipleObjects", __PRETTY_FUNCTION__, GetLastError());
    
    if (waitResult == WAIT_TIMEOUT) {
        // do nothing, just return to caller
        result = false;
    } else if (waitResult >= WAIT_OBJECT_0 && waitResult < WAIT_OBJECT_0+handleCount) {
        // a handle was signaled
        if (livePort) *livePort = handles[waitResult-WAIT_OBJECT_0];
        result = true;
    } else if (waitResult == WAIT_OBJECT_0+handleCount) {
        // windows message received
        if (msgReceived) *msgReceived = true;
        result = true;
    } else if (waitResult >= WAIT_ABANDONED_0 && waitResult < WAIT_ABANDONED_0+handleCount) {
        // an "abandoned mutex object"
        if (livePort) *livePort = handles[waitResult-WAIT_ABANDONED_0];
        result = true;
    } else {
        CFAssert2(waitResult == WAIT_FAILED, __kCFLogAssertion, "%s(): unexpected result from MsgWaitForMultipleObjects: %d", __PRETTY_FUNCTION__, waitResult);
        result = false;
    }
    
    if (freeHandles) {
        CFAllocatorDeallocate(kCFAllocatorSystemDefault, handles);
    }
    
    return result;
}

#endif
/// 超时上下文
struct __timeout_context {
    dispatch_source_t ds;
    CFRunLoopRef rl;
    uint64_t termTSR;
};
/// 超时退出 释放
static void __CFRunLoopTimeoutCancel(void *arg) {
    struct __timeout_context *context = (struct __timeout_context *)arg;
    CFRelease(context->rl);
    dispatch_release(context->ds);
    free(context);
}
/// 超时
static void __CFRunLoopTimeout(void *arg) {
    struct __timeout_context *context = (struct __timeout_context *)arg;
    context->termTSR = 0ULL;
    CFRUNLOOP_WAKEUP_FOR_TIMEOUT();
    CFRunLoopWakeUp(context->rl);
    // The interval is DISPATCH_TIME_FOREVER, so this won't fire again
}

/* rl, rlm are locked on entrance and exit */
/**
 启动RunLoop

 @param rl RunLoop
 @param rlm RunLoopMode
 @param seconds 超时时间
 @param stopAfterHandle stop 回调
 @param previousMode 上一个Mode
 @return 状态
 */
#pragma mark - 启动RunLoop
static int32_t __CFRunLoopRun(CFRunLoopRef rl, CFRunLoopModeRef rlm, CFTimeInterval seconds, Boolean stopAfterHandle, CFRunLoopModeRef previousMode) {
    // 开始时间
    uint64_t startTSR = mach_absolute_time();
    // 判断RunLoop || RunLoopMode 是否已经停止 停止了直接返回
    if (__CFRunLoopIsStopped(rl)) {
        __CFRunLoopUnsetStopped(rl);
        return kCFRunLoopRunStopped;
    } else if (rlm->_stopped) {
        rlm->_stopped = false;
        return kCFRunLoopRunStopped;
    }
    
    mach_port_name_t dispatchPort = MACH_PORT_NULL;
    Boolean libdispatchQSafe = pthread_main_np() && ((HANDLE_DISPATCH_ON_BASE_INVOCATION_ONLY && NULL == previousMode) || (!HANDLE_DISPATCH_ON_BASE_INVOCATION_ONLY && 0 == _CFGetTSD(__CFTSDKeyIsInGCDMainQ)));
    if (libdispatchQSafe && (CFRunLoopGetMain() == rl) && CFSetContainsValue(rl->_commonModes, rlm->_name)) dispatchPort = _dispatch_get_main_queue_port_4CF();
    /// gcd source timers
#if USE_DISPATCH_SOURCE_FOR_TIMERS
    mach_port_name_t modeQueuePort = MACH_PORT_NULL;
    if (rlm->_queue) {
        modeQueuePort = _dispatch_runloop_root_queue_get_port_4CF(rlm->_queue);
        if (!modeQueuePort) {
            CRASH("Unable to get port for run loop mode queue (%d)", -1);
        }
    }
#endif
    
    dispatch_source_t timeout_timer = NULL;
    /// 超时上下文 用GCD实现超时
    struct __timeout_context *timeout_context = (struct __timeout_context *)malloc(sizeof(*timeout_context));
    if (seconds <= 0.0) { // instant timeout
        seconds = 0.0;
        timeout_context->termTSR = 0ULL;
    } else if (seconds <= TIMER_INTERVAL_LIMIT) { /// 在范围内
        /// 获取dispatch_queue_t 队列 优先取主线程
        dispatch_queue_t queue = pthread_main_np() ? __CFDispatchQueueGetGenericMatchingMain() : __CFDispatchQueueGetGenericBackground();
        /// 创建GCD source timer
        timeout_timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, queue);
        /// retain + 1    这里将数据模型存入上下文
        dispatch_retain(timeout_timer);
        timeout_context->ds = timeout_timer;
        timeout_context->rl = (CFRunLoopRef)CFRetain(rl);
        timeout_context->termTSR = startTSR + __CFTimeIntervalToTSR(seconds);
        /// 把context 设置到 dispatch 上
        dispatch_set_context(timeout_timer, timeout_context); // source gets ownership of context
        /// GCD Source Timer
        dispatch_source_set_event_handler_f(timeout_timer, __CFRunLoopTimeout);
        dispatch_source_set_cancel_handler_f(timeout_timer, __CFRunLoopTimeoutCancel);
        uint64_t ns_at = (uint64_t)((__CFTSRToTimeInterval(startTSR) + seconds) * 1000000000ULL);
        dispatch_source_set_timer(timeout_timer, dispatch_time(1, ns_at), DISPATCH_TIME_FOREVER, 1000ULL);
        dispatch_resume(timeout_timer);
    } else { // infinite timeout 已经超时
        seconds = 9999999999.0;
        timeout_context->termTSR = UINT64_MAX;
    }
    
    Boolean didDispatchPortLastTime = true;
    int32_t retVal = 0;
    /// 循环
    do {
#if DEPLOYMENT_TARGET_MACOSX || DEPLOYMENT_TARGET_EMBEDDED || DEPLOYMENT_TARGET_EMBEDDED_MINI
        voucher_mach_msg_state_t voucherState = VOUCHER_MACH_MSG_STATE_UNCHANGED;
        voucher_t voucherCopy = NULL;
#endif
        uint8_t msg_buffer[3 * 1024];
#if DEPLOYMENT_TARGET_MACOSX || DEPLOYMENT_TARGET_EMBEDDED || DEPLOYMENT_TARGET_EMBEDDED_MINI
        mach_msg_header_t *msg = NULL;
        mach_port_t livePort = MACH_PORT_NULL;

        /// Mode 中的 port 集合
        __CFPortSet waitSet = rlm->_portSet;
        
        __CFRunLoopUnsetIgnoreWakeUps(rl);
        /// 这里回调观察者的活动 运行循环将要处理一个计时器时。
        if (rlm->_observerMask & kCFRunLoopBeforeTimers) __CFRunLoopDoObservers(rl, rlm, kCFRunLoopBeforeTimers);
        /// 这里回调观察者的活动 运行循环将要处理一个输入源时。触发 Source0 (非port) 回调。
        if (rlm->_observerMask & kCFRunLoopBeforeSources) __CFRunLoopDoObservers(rl, rlm, kCFRunLoopBeforeSources);
        
        /// 设置Block回调
        __CFRunLoopDoBlocks(rl, rlm);
        /// 处理source0
        Boolean sourceHandledThisLoop = __CFRunLoopDoSources0(rl, rlm, stopAfterHandle);
        if (sourceHandledThisLoop) {
            __CFRunLoopDoBlocks(rl, rlm);
        }
        /// 标志是否等待端口唤醒
        Boolean poll = sourceHandledThisLoop || (0ULL == timeout_context->termTSR);
        /// 检测端口，如果端口有事件则跳转至handle_msg（首次执行不会进入判断，因为didDispatchPortLastTime为true）
        if (MACH_PORT_NULL != dispatchPort && !didDispatchPortLastTime) {
            msg = (mach_msg_header_t *)msg_buffer;
            if (__CFRunLoopServiceMachPort(dispatchPort, &msg, sizeof(msg_buffer), &livePort, 0, &voucherState, NULL)) {
                /// 跳转去source1收到的系统消息
                goto handle_msg;
            }
        }
        
        didDispatchPortLastTime = false;
        /// 这里回调观察者的活动 运行循环将要休眠
        if (!poll && (rlm->_observerMask & kCFRunLoopBeforeWaiting)) __CFRunLoopDoObservers(rl, rlm, kCFRunLoopBeforeWaiting);
        /// 设置休眠
        __CFRunLoopSetSleeping(rl);
        // do not do any user callouts after this point (after notifying of sleeping)
        
        // Must push the local-to-this-activation ports in on every loop
        // iteration, as this mode could be run re-entrantly and we don't
        // want these ports to get serviced.
        /// 存入 dispatchPort
        __CFPortSetInsert(dispatchPort, waitSet);
        /// 解锁
        __CFRunLoopModeUnlock(rlm);
        __CFRunLoopUnlock(rl);
        
        CFAbsoluteTime sleepStart = poll ? 0.0 : CFAbsoluteTimeGetCurrent();
    
#if DEPLOYMENT_TARGET_MACOSX || DEPLOYMENT_TARGET_EMBEDDED || DEPLOYMENT_TARGET_EMBEDDED_MINI
#if USE_DISPATCH_SOURCE_FOR_TIMERS
        do {
            if (kCFUseCollectableAllocator) {
                // objc_clear_stack(0);
                // <rdar://problem/16393959>
                memset(msg_buffer, 0, sizeof(msg_buffer));
            }
            msg = (mach_msg_header_t *)msg_buffer;
            /// 调用 mach_msg 等待接受 mach_port 的消息。线程将进入休眠, 直到被下面某一个事件唤醒。
            /// 进入循环开始不断的读取端口信息，如果端口有唤醒信息则唤醒当前runLoop.
            /// 一个基于 port 的Source 的事件。
            /// 一个 Timer 到时间了
            /// RunLoop 自身的超时时间到了
            /// 被其他什么调用者手动唤醒
            __CFRunLoopServiceMachPort(waitSet, &msg, sizeof(msg_buffer), &livePort, poll ? 0 : TIMEOUT_INFINITY, &voucherState, &voucherCopy);
            
            if (modeQueuePort != MACH_PORT_NULL && livePort == modeQueuePort) {
                // Drain the internal queue. If one of the callout blocks sets the timerFired flag, break out and service the timer.
                while (_dispatch_runloop_root_queue_perform_4CF(rlm->_queue));
                if (rlm->_timerFired) {
                    // Leave livePort as the queue port, and service timers below
                    rlm->_timerFired = false;
                    break;
                } else {
                    if (msg && msg != (mach_msg_header_t *)msg_buffer) free(msg);
                }
            } else {
                // Go ahead and leave the inner loop.
                break;
            }
        } while (1);
#else
        if (kCFUseCollectableAllocator) {
            // objc_clear_stack(0);
            // <rdar://problem/16393959>
            memset(msg_buffer, 0, sizeof(msg_buffer));
        }
        msg = (mach_msg_header_t *)msg_buffer;
        /// 调用 mach_msg 等待接受 mach_port 的消息。线程将进入休眠, 直到被下面某一个事件唤醒。
        /// 一个基于 port 的Source 的事件。
        /// 一个 Timer 到时间了
        /// RunLoop 自身的超时时间到了
        /// 被其他什么调用者手动唤醒
        __CFRunLoopServiceMachPort(waitSet, &msg, sizeof(msg_buffer), &livePort, poll ? 0 : TIMEOUT_INFINITY, &voucherState, &voucherCopy);
#endif
        
        __CFRunLoopLock(rl);
        __CFRunLoopModeLock(rlm);
        
        rl->_sleepTime += (poll ? 0.0 : (CFAbsoluteTimeGetCurrent() - sleepStart));
        
        // Must remove the local-to-this-activation ports in on every loop
        // iteration, as this mode could be run re-entrantly and we don't
        // want these ports to get serviced. Also, we don't want them left
        // in there if this function returns.
        /// 移除dispatchPort
        __CFPortSetRemove(dispatchPort, waitSet);
        /// 设置忽略的wakeUps RunLoop
        __CFRunLoopSetIgnoreWakeUps(rl);
        /// 取消睡眠
        // user callouts now OK again
        __CFRunLoopUnsetSleeping(rl);
        /// 观察者 RunLoop从睡眠中被唤醒
        if (!poll && (rlm->_observerMask & kCFRunLoopAfterWaiting)) __CFRunLoopDoObservers(rl, rlm, kCFRunLoopAfterWaiting);
    /// 收到消息，处理消息，执行端口的事件
#pragma mark - handle_msg 处理Source1收到的消息 唤醒RunLoop
    handle_msg:;
        // 设置此时runLoop忽略端口唤醒（保证线程安全）
        __CFRunLoopSetIgnoreWakeUps(rl);
        
        /// 无消息 handle nothing
        if (MACH_PORT_NULL == livePort) {
            CFRUNLOOP_WAKEUP_FOR_NOTHING();
            // handle nothing
        } else if (livePort == rl->_wakeUpPort) {
            CFRUNLOOP_WAKEUP_FOR_WAKEUP();
        }
        /// 如果一个 Timer 到时间了，触发这个Timer的回调。
#if USE_DISPATCH_SOURCE_FOR_TIMERS
        else if (modeQueuePort != MACH_PORT_NULL && livePort == modeQueuePort) {
            CFRUNLOOP_WAKEUP_FOR_TIMER();
            if (!__CFRunLoopDoTimers(rl, rlm, mach_absolute_time())) {
                // Re-arm the next timer, because we apparently fired early
                __CFArmNextTimerInMode(rlm, rl);
            }
        }
#endif
        /// 如果一个 Timer 到时间了，触发这个Timer的回调。 x
#if USE_MK_TIMER_TOO
        else if (rlm->_timerPort != MACH_PORT_NULL && livePort == rlm->_timerPort) {
            CFRUNLOOP_WAKEUP_FOR_TIMER();
     
            if (!__CFRunLoopDoTimers(rl, rlm, mach_absolute_time())) {
                // Re-arm the next timer
                __CFArmNextTimerInMode(rlm, rl);
            }
        }
#endif
        /// 如果有dispatch到main_queue的block，执行block。
        else if (livePort == dispatchPort) {
            CFRUNLOOP_WAKEUP_FOR_DISPATCH();
            __CFRunLoopModeUnlock(rlm);
            __CFRunLoopUnlock(rl);
            _CFSetTSD(__CFTSDKeyIsInGCDMainQ, (void *)6, NULL);
            __CFRUNLOOP_IS_SERVICING_THE_MAIN_DISPATCH_QUEUE__(msg);
            _CFSetTSD(__CFTSDKeyIsInGCDMainQ, (void *)0, NULL);
            __CFRunLoopLock(rl);
            __CFRunLoopModeLock(rlm);
            sourceHandledThisLoop = true;
            didDispatchPortLastTime = true;
        } else {
            /// 如果一个 Source1 (基于port) 发出事件了，处理这个事件
            CFRUNLOOP_WAKEUP_FOR_SOURCE();
            
            // If we received a voucher from this mach_msg, then put a copy of the new voucher into TSD. CFMachPortBoost will look in the TSD for the voucher. By using the value in the TSD we tie the CFMachPortBoost to this received mach_msg explicitly without a chance for anything in between the two pieces of code to set the voucher again.
            voucher_t previousVoucher = _CFSetTSD(__CFTSDKeyMachMessageHasVoucher, (void *)voucherCopy, os_release);
            /// 这里调用 __CFRunLoopModeFindSourceForMachPort 找到 source, key 为 mach_port 指为 source
            // Despite the name, this works for windows handles as well
            CFRunLoopSourceRef rls = __CFRunLoopModeFindSourceForMachPort(rl, rlm, livePort);
            if (rls) {
#if DEPLOYMENT_TARGET_MACOSX || DEPLOYMENT_TARGET_EMBEDDED || DEPLOYMENT_TARGET_EMBEDDED_MINI
                mach_msg_header_t *reply = NULL;
                /// 处理source 1
                sourceHandledThisLoop = __CFRunLoopDoSource1(rl, rlm, rls, msg, msg->msgh_size, &reply) || sourceHandledThisLoop;
                if (NULL != reply) {
                    (void)mach_msg(reply, MACH_SEND_MSG, reply->msgh_size, 0, MACH_PORT_NULL, 0, MACH_PORT_NULL);
                    CFAllocatorDeallocate(kCFAllocatorSystemDefault, reply);
                }
            }
            
            // Restore the previous voucher
            _CFSetTSD(__CFTSDKeyMachMessageHasVoucher, previousVoucher, os_release);
            
        } 
#if DEPLOYMENT_TARGET_MACOSX || DEPLOYMENT_TARGET_EMBEDDED || DEPLOYMENT_TARGET_EMBEDDED_MINI
        if (msg && msg != (mach_msg_header_t *)msg_buffer) free(msg);
#endif
        /// 执行加入到Loop的block
        __CFRunLoopDoBlocks(rl, rlm);
        
        /// 返回 RunLoop 状态
        if (sourceHandledThisLoop && stopAfterHandle) {
            /// 进入loop时参数说处理完事件就返回。
            retVal = kCFRunLoopRunHandledSource;
        } else if (timeout_context->termTSR < mach_absolute_time()) {
            /// 超出传入参数标记的超时时间了
            retVal = kCFRunLoopRunTimedOut;
        } else if (__CFRunLoopIsStopped(rl)) {
            /// 被外部调用者强制停止了
            __CFRunLoopUnsetStopped(rl);
            retVal = kCFRunLoopRunStopped;
        } else if (rlm->_stopped) {
            /// 已经停止了
            rlm->_stopped = false;
            retVal = kCFRunLoopRunStopped;
        } else if (__CFRunLoopModeIsEmpty(rl, rlm, previousMode)) {
            /// source/timer一个都没有了，
            retVal = kCFRunLoopRunFinished;
        }
        
#if DEPLOYMENT_TARGET_MACOSX || DEPLOYMENT_TARGET_EMBEDDED || DEPLOYMENT_TARGET_EMBEDDED_MINI
        voucher_mach_msg_revert(voucherState);
        os_release(voucherCopy);
#endif
    /// 如果没超时，mode里没空，loop也没被停止，那继续loop。
    } while (0 == retVal);
    
    if (timeout_timer) {
        dispatch_source_cancel(timeout_timer);
        dispatch_release(timeout_timer);
    } else {
        free(timeout_context);
    }
    
    return retVal;
}
/**
 启动RunLoop 核心方法

 @param rl RunLoop
 @param modeName Mode名称
 @param seconds 超时时间
 @param returnAfterSourceHandled 事件源回调
 @return
 */
#pragma mark -  启动RunLoop 核心方法
SInt32 CFRunLoopRunSpecific(CFRunLoopRef rl, CFStringRef modeName, CFTimeInterval seconds, Boolean returnAfterSourceHandled) {     /* DOES CALLOUT */
    CHECK_FOR_FORK();
    /// 如果RunLoop 正在析构 则直接返回完成状态
    if (__CFRunLoopIsDeallocating(rl)) return kCFRunLoopRunFinished;
    /// 加锁
    __CFRunLoopLock(rl);
    /// 根据Mode名称找到RunLoopMode
    CFRunLoopModeRef currentMode = __CFRunLoopFindMode(rl, modeName, false);
    if (NULL == currentMode || __CFRunLoopModeIsEmpty(rl, currentMode, rl->_currentMode)) {
        Boolean did = false;
        if (currentMode) __CFRunLoopModeUnlock(currentMode);
        __CFRunLoopUnlock(rl);
        return did ? kCFRunLoopRunHandledSource : kCFRunLoopRunFinished;
    }
    /// 取得RunLoop中的 run data
    volatile _per_run_data *previousPerRun = __CFRunLoopPushPerRunData(rl);
    /// 这里先退出之前的Mode 切换 CurrentMode
    CFRunLoopModeRef previousMode = rl->_currentMode;
    rl->_currentMode = currentMode;
    int32_t result = kCFRunLoopRunFinished;
    /// 这里回调观察者的活动 即将进入 RunLoop
    if (currentMode->_observerMask & kCFRunLoopEntry ) __CFRunLoopDoObservers(rl, currentMode, kCFRunLoopEntry);
    /// __CFRunLoopRun 函数里面处理RunLoop机制
    result = __CFRunLoopRun(rl, currentMode, seconds, returnAfterSourceHandled, previousMode);
    /// 这里回调观察者的活动 即将进入 退出
    if (currentMode->_observerMask & kCFRunLoopExit ) __CFRunLoopDoObservers(rl, currentMode, kCFRunLoopExit);
    ///
    __CFRunLoopModeUnlock(currentMode);
    /// 移除数据源
    __CFRunLoopPopPerRunData(rl, previousPerRun);
    /// 执行完毕后放回之前的Mode
    rl->_currentMode = previousMode;
    __CFRunLoopUnlock(rl);
    return result;
}
#pragma mark - 启动 kCFRunLoopDefaultMode RunLoop
void CFRunLoopRun(void) {	/* DOES CALLOUT */
    int32_t result;
    do {
        result = CFRunLoopRunSpecific(CFRunLoopGetCurrent(), kCFRunLoopDefaultMode, 1.0e10, false);
        CHECK_FOR_FORK();
    } while (kCFRunLoopRunStopped != result && kCFRunLoopRunFinished != result);
}
/// 在 某个 Mode 中 启动 RunLoop
SInt32 CFRunLoopRunInMode(CFStringRef modeName, CFTimeInterval seconds, Boolean returnAfterSourceHandled) {     /* DOES CALLOUT */
    CHECK_FOR_FORK();
    return CFRunLoopRunSpecific(CFRunLoopGetCurrent(), modeName, seconds, returnAfterSourceHandled);
}
/// 下个定时器到来的时间
CFAbsoluteTime CFRunLoopGetNextTimerFireDate(CFRunLoopRef rl, CFStringRef modeName) {
    CHECK_FOR_FORK();
    __CFRunLoopLock(rl);
    CFRunLoopModeRef rlm = __CFRunLoopFindMode(rl, modeName, false);
    CFAbsoluteTime at = 0.0;
    CFRunLoopTimerRef nextTimer = (rlm && rlm->_timers && 0 < CFArrayGetCount(rlm->_timers)) ? (CFRunLoopTimerRef)CFArrayGetValueAtIndex(rlm->_timers, 0) : NULL;
    if (nextTimer) {
        at = CFRunLoopTimerGetNextFireDate(nextTimer);
    }
    if (rlm) __CFRunLoopModeUnlock(rlm);
    __CFRunLoopUnlock(rl);
    return at;
}
/// 判断RunLoop 是否正在等待
Boolean CFRunLoopIsWaiting(CFRunLoopRef rl) {
    CHECK_FOR_FORK();
    return __CFRunLoopIsSleeping(rl);
}
/// 唤醒RunLoop
void CFRunLoopWakeUp(CFRunLoopRef rl) {
    CHECK_FOR_FORK();
    // This lock is crucial to ignorable wakeups, do not remove it.
    __CFRunLoopLock(rl);
    if (__CFRunLoopIsIgnoringWakeUps(rl)) {
        __CFRunLoopUnlock(rl);
        return;
    }
#if DEPLOYMENT_TARGET_MACOSX || DEPLOYMENT_TARGET_EMBEDDED || DEPLOYMENT_TARGET_EMBEDDED_MINI
    /// 这里利用内核发送 mach_port 消息
    kern_return_t ret;
    /* We unconditionally try to send the message, since we don't want
     * to lose a wakeup, but the send may fail if there is already a
     * wakeup pending, since the queue length is 1. */
    ret = __CFSendTrivialMachMessage(rl->_wakeUpPort, 0, MACH_SEND_TIMEOUT, 0);
    if (ret != MACH_MSG_SUCCESS && ret != MACH_SEND_TIMED_OUT) CRASH("*** Unable to send message to wake up port. (%d) ***", ret);
#elif DEPLOYMENT_TARGET_WINDOWS
    SetEvent(rl->_wakeUpPort);
#endif
    __CFRunLoopUnlock(rl);
}
/// 停止RunLoop
void CFRunLoopStop(CFRunLoopRef rl) {
    Boolean doWake = false;
    CHECK_FOR_FORK();
    __CFRunLoopLock(rl);
    if (rl->_currentMode) {
        __CFRunLoopSetStopped(rl);
        doWake = true;
    }
    __CFRunLoopUnlock(rl);
    if (doWake) {
        CFRunLoopWakeUp(rl);
    }
}
/// 设置Mode为停止状态
CF_EXPORT void _CFRunLoopStopMode(CFRunLoopRef rl, CFStringRef modeName) {
    CHECK_FOR_FORK();
    CFRunLoopModeRef rlm;
    __CFRunLoopLock(rl);
    rlm = __CFRunLoopFindMode(rl, modeName, true);
    if (NULL != rlm) {
        rlm->_stopped = true;
        __CFRunLoopModeUnlock(rlm);
    }
    __CFRunLoopUnlock(rl);
    CFRunLoopWakeUp(rl);
}
/// 判断是否包含某个Mode
CF_EXPORT Boolean _CFRunLoopModeContainsMode(CFRunLoopRef rl, CFStringRef modeName, CFStringRef candidateContainedName) {
    CHECK_FOR_FORK();
    return false;
}
/// RunLoop Perform Block
void CFRunLoopPerformBlock(CFRunLoopRef rl, CFTypeRef mode, void (^block)(void)) {
    CHECK_FOR_FORK();
    /// 找到当前执行perfonBlock中的RunLoop中的Mode
    if (CFStringGetTypeID() == CFGetTypeID(mode)) {
        mode = CFStringCreateCopy(kCFAllocatorSystemDefault, (CFStringRef)mode);
        __CFRunLoopLock(rl);
        // ensure mode exists
        CFRunLoopModeRef currentMode = __CFRunLoopFindMode(rl, (CFStringRef)mode, true);
        if (currentMode) __CFRunLoopModeUnlock(currentMode);
        __CFRunLoopUnlock(rl);
    } else if (CFArrayGetTypeID() == CFGetTypeID(mode)) {
        CFIndex cnt = CFArrayGetCount((CFArrayRef)mode);
        const void **values = (const void **)malloc(sizeof(const void *) * cnt);
        CFArrayGetValues((CFArrayRef)mode, CFRangeMake(0, cnt), values);
        mode = CFSetCreate(kCFAllocatorSystemDefault, values, cnt, &kCFTypeSetCallBacks);
        __CFRunLoopLock(rl);
        // ensure modes exist
        for (CFIndex idx = 0; idx < cnt; idx++) {
            CFRunLoopModeRef currentMode = __CFRunLoopFindMode(rl, (CFStringRef)values[idx], true);
            if (currentMode) __CFRunLoopModeUnlock(currentMode);
        }
        __CFRunLoopUnlock(rl);
        free(values);
    } else if (CFSetGetTypeID() == CFGetTypeID(mode)) {
        CFIndex cnt = CFSetGetCount((CFSetRef)mode);
        const void **values = (const void **)malloc(sizeof(const void *) * cnt);
        CFSetGetValues((CFSetRef)mode, values);
        mode = CFSetCreate(kCFAllocatorSystemDefault, values, cnt, &kCFTypeSetCallBacks);
        __CFRunLoopLock(rl);
        // ensure modes exist
        for (CFIndex idx = 0; idx < cnt; idx++) {
            CFRunLoopModeRef currentMode = __CFRunLoopFindMode(rl, (CFStringRef)values[idx], true);
            if (currentMode) __CFRunLoopModeUnlock(currentMode);
        }
        __CFRunLoopUnlock(rl);
        free(values);
    } else {
        mode = NULL;
    }
    /// copy block 放进堆
    block = Block_copy(block);
    /// 判断空
    if (!mode || !block) {
        if (mode) CFRelease(mode);
        if (block) Block_release(block);
        return;
    }
    /// 加锁
    __CFRunLoopLock(rl);
    /// 把block、mode 放进 RunLoop里面
    struct _block_item *new_item = (struct _block_item *)malloc(sizeof(struct _block_item));
    new_item->_next = NULL;
    new_item->_mode = mode;
    new_item->_block = block;
    if (!rl->_blocks_tail) {
        rl->_blocks_head = new_item;
    } else {
        rl->_blocks_tail->_next = new_item;
    }
    rl->_blocks_tail = new_item;
    __CFRunLoopUnlock(rl);
}
/// 判断RunLoop 是否包含 Source
Boolean CFRunLoopContainsSource(CFRunLoopRef rl, CFRunLoopSourceRef rls, CFStringRef modeName) {
    CHECK_FOR_FORK();
    CFRunLoopModeRef rlm;
    Boolean hasValue = false;
    __CFRunLoopLock(rl);
    if (modeName == kCFRunLoopCommonModes) {
        if (NULL != rl->_commonModeItems) {
            hasValue = CFSetContainsValue(rl->_commonModeItems, rls);
        }
    } else {
        rlm = __CFRunLoopFindMode(rl, modeName, false);
        if (NULL != rlm) {
            hasValue = (rlm->_sources0 ? CFSetContainsValue(rlm->_sources0, rls) : false) || (rlm->_sources1 ? CFSetContainsValue(rlm->_sources1, rls) : false);
            __CFRunLoopModeUnlock(rlm);
        }
    }
    __CFRunLoopUnlock(rl);
    return hasValue;
}
/// 为RunLoop中的Mode添加Source
void CFRunLoopAddSource(CFRunLoopRef rl, CFRunLoopSourceRef rls, CFStringRef modeName) {	/* DOES CALLOUT */
    CHECK_FOR_FORK();
    if (__CFRunLoopIsDeallocating(rl)) return;
    if (!__CFIsValid(rls)) return;
    Boolean doVer0Callout = false;
    __CFRunLoopLock(rl);
    if (modeName == kCFRunLoopCommonModes) {
        /// 如果runloop的_commonModes存在，则copy一个新的复制给set
        CFSetRef set = rl->_commonModes ? CFSetCreateCopy(kCFAllocatorSystemDefault, rl->_commonModes) : NULL;
        if (NULL == rl->_commonModeItems) {
            // 如果runl _commonModeItems为空，就初始化一个set
            rl->_commonModeItems = CFSetCreateMutable(kCFAllocatorSystemDefault, 0, &kCFTypeSetCallBacks);
        }
        /// 把传入的CFRunLoopSourceRef加入_commonModeItems
        CFSetAddValue(rl->_commonModeItems, rls);
        /// 如果刚才set copy到的数组里有数据
        if (NULL != set) {
            CFTypeRef context[2] = {rl, rls};
            /* add new item to all common-modes */
            /// 这句的作用是集合中的所有对象均调用__CFRunLoopAddItemToCommonModes这个方法。
            CFSetApplyFunction(set, (__CFRunLoopAddItemToCommonModes), (void *)context);
            CFRelease(set);
        }
        /// 以上分支的逻辑就是，如果你往kCFRunLoopCommonModes里面添加一个source，那么所有_commonModes里的mode都会添加这个source
    } else {
        /// 根据modeName去寻找Mode找不到就创建
        CFRunLoopModeRef rlm = __CFRunLoopFindMode(rl, modeName, true);
        if (NULL != rlm && NULL == rlm->_sources0) {
            /// 创建
            rlm->_sources0 = CFSetCreateMutable(kCFAllocatorSystemDefault, 0, &kCFTypeSetCallBacks);
            rlm->_sources1 = CFSetCreateMutable(kCFAllocatorSystemDefault, 0, &kCFTypeSetCallBacks);
            rlm->_portToV1SourceMap = CFDictionaryCreateMutable(kCFAllocatorSystemDefault, 0, NULL, NULL);
        }
        /// 根据source context.version0.version 判断是source0还是source1 把事件源放进去
        if (NULL != rlm && !CFSetContainsValue(rlm->_sources0, rls) && !CFSetContainsValue(rlm->_sources1, rls)) {
            if (0 == rls->_context.version0.version) { /// source 0
                /// source 0 存入 set
                CFSetAddValue(rlm->_sources0, rls);
            } else if (1 == rls->_context.version0.version) { /// source 1
                /// source 1 存入 set
                CFSetAddValue(rlm->_sources1, rls);
                /// 从 _context.version1.info 取得 port
                __CFPort src_port = rls->_context.version1.getPort(rls->_context.version1.info);
                if (CFPORT_NULL != src_port) {
                    /// 以 mach_port 为 key 值为 source1 存入到 Mode 的 _portToV1SourceMap Map中
                    CFDictionarySetValue(rlm->_portToV1SourceMap, (const void *)(uintptr_t)src_port, rls);
                    __CFPortSetInsert(src_port, rlm->_portSet);
                }
            }
            __CFRunLoopSourceLock(rls);
            /// 这里把RunLoop也放到 source结构体里面
            if (NULL == rls->_runLoops) {
                rls->_runLoops = CFBagCreateMutable(kCFAllocatorSystemDefault, 0, &kCFTypeBagCallBacks); // sources retain run loops!
            }
            CFBagAddValue(rls->_runLoops, rl);
            __CFRunLoopSourceUnlock(rls);
            if (0 == rls->_context.version0.version) {
                if (NULL != rls->_context.version0.schedule) {
                    doVer0Callout = true;
                }
            }
        }
        if (NULL != rlm) {
            __CFRunLoopModeUnlock(rlm);
        }
    }
    __CFRunLoopUnlock(rl);
    if (doVer0Callout) {
        // although it looses some protection for the source, we have no choice but
        // to do this after unlocking the run loop and mode locks, to avoid deadlocks
        // where the source wants to take a lock which is already held in another
        // thread which is itself waiting for a run loop/mode lock
        rls->_context.version0.schedule(rls->_context.version0.info, rl, modeName);	/* CALLOUT */
    }
}
/// 从Mode 移除RunLoop中的事件源
void CFRunLoopRemoveSource(CFRunLoopRef rl, CFRunLoopSourceRef rls, CFStringRef modeName) {	/* DOES CALLOUT */
    CHECK_FOR_FORK();
    Boolean doVer0Callout = false, doRLSRelease = false;
    __CFRunLoopLock(rl);
    if (modeName == kCFRunLoopCommonModes) {
        if (NULL != rl->_commonModeItems && CFSetContainsValue(rl->_commonModeItems, rls)) {
            CFSetRef set = rl->_commonModes ? CFSetCreateCopy(kCFAllocatorSystemDefault, rl->_commonModes) : NULL;
            CFSetRemoveValue(rl->_commonModeItems, rls);
            if (NULL != set) {
                CFTypeRef context[2] = {rl, rls};
                /* remove new item from all common-modes */
                CFSetApplyFunction(set, (__CFRunLoopRemoveItemFromCommonModes), (void *)context);
                CFRelease(set);
            }
        } else {
        }
    } else {
        CFRunLoopModeRef rlm = __CFRunLoopFindMode(rl, modeName, false);
        if (NULL != rlm && ((NULL != rlm->_sources0 && CFSetContainsValue(rlm->_sources0, rls)) || (NULL != rlm->_sources1 && CFSetContainsValue(rlm->_sources1, rls)))) {
            CFRetain(rls);
            if (1 == rls->_context.version0.version) {
                __CFPort src_port = rls->_context.version1.getPort(rls->_context.version1.info);
                if (CFPORT_NULL != src_port) {
                    CFDictionaryRemoveValue(rlm->_portToV1SourceMap, (const void *)(uintptr_t)src_port);
                    __CFPortSetRemove(src_port, rlm->_portSet);
                }
            }
            CFSetRemoveValue(rlm->_sources0, rls);
            CFSetRemoveValue(rlm->_sources1, rls);
            __CFRunLoopSourceLock(rls);
            if (NULL != rls->_runLoops) {
                CFBagRemoveValue(rls->_runLoops, rl);
            }
            __CFRunLoopSourceUnlock(rls);
            if (0 == rls->_context.version0.version) {
                if (NULL != rls->_context.version0.cancel) {
                    doVer0Callout = true;
                }
            }
            doRLSRelease = true;
        }
        if (NULL != rlm) {
            __CFRunLoopModeUnlock(rlm);
        }
    }
    __CFRunLoopUnlock(rl);
    if (doVer0Callout) {
        // although it looses some protection for the source, we have no choice but
        // to do this after unlocking the run loop and mode locks, to avoid deadlocks
        // where the source wants to take a lock which is already held in another
        // thread which is itself waiting for a run loop/mode lock
        rls->_context.version0.cancel(rls->_context.version0.info, rl, modeName);	/* CALLOUT */
    }
    if (doRLSRelease) CFRelease(rls);
}
/// 移除
static void __CFRunLoopRemoveSourcesFromCommonMode(const void *value, void *ctx) {
    CFStringRef modeName = (CFStringRef)value;
    CFRunLoopRef rl = (CFRunLoopRef)ctx;
    __CFRunLoopRemoveAllSources(rl, modeName);
}
/// 从当前的Mode移除Source
static void __CFRunLoopRemoveSourceFromMode(const void *value, void *ctx) {
    CFRunLoopSourceRef rls = (CFRunLoopSourceRef)value;
    CFRunLoopRef rl = (CFRunLoopRef)(((CFTypeRef *)ctx)[0]);
    CFStringRef modeName = (CFStringRef)(((CFTypeRef *)ctx)[1]);
    CFRunLoopRemoveSource(rl, rls, modeName);
}
/// 移除所有的 RunLoop 中的事件源
static void __CFRunLoopRemoveAllSources(CFRunLoopRef rl, CFStringRef modeName) {
    CHECK_FOR_FORK();
    CFRunLoopModeRef rlm;
    __CFRunLoopLock(rl);
    if (modeName == kCFRunLoopCommonModes) {
        if (NULL != rl->_commonModeItems) {
            CFSetRef set = rl->_commonModes ? CFSetCreateCopy(kCFAllocatorSystemDefault, rl->_commonModes) : NULL;
            if (NULL != set) {
                CFSetApplyFunction(set, (__CFRunLoopRemoveSourcesFromCommonMode), (void *)rl);
                CFRelease(set);
            }
        } else {
        }
    } else {
        rlm = __CFRunLoopFindMode(rl, modeName, false);
        if (NULL != rlm && NULL != rlm->_sources0) {
            CFSetRef set = CFSetCreateCopy(kCFAllocatorSystemDefault, rlm->_sources0);
            CFTypeRef context[2] = {rl, modeName};
            CFSetApplyFunction(set, (__CFRunLoopRemoveSourceFromMode), (void *)context);
            CFRelease(set);
        }
        if (NULL != rlm && NULL != rlm->_sources1) {
            CFSetRef set = CFSetCreateCopy(kCFAllocatorSystemDefault, rlm->_sources1);
            CFTypeRef context[2] = {rl, modeName};
            CFSetApplyFunction(set, (__CFRunLoopRemoveSourceFromMode), (void *)context);
            CFRelease(set);
        }
        if (NULL != rlm) {
            __CFRunLoopModeUnlock(rlm);
        }
    }
    __CFRunLoopUnlock(rl);
}
/// 从RunLoop中的Mode判断是否包含观察者
Boolean CFRunLoopContainsObserver(CFRunLoopRef rl, CFRunLoopObserverRef rlo, CFStringRef modeName) {
    CHECK_FOR_FORK();
    CFRunLoopModeRef rlm;
    Boolean hasValue = false;
    __CFRunLoopLock(rl);
    if (modeName == kCFRunLoopCommonModes) {
        if (NULL != rl->_commonModeItems) {
            hasValue = CFSetContainsValue(rl->_commonModeItems, rlo);
        }
    } else {
        rlm = __CFRunLoopFindMode(rl, modeName, false);
        if (NULL != rlm && NULL != rlm->_observers) {
            hasValue = CFArrayContainsValue(rlm->_observers, CFRangeMake(0, CFArrayGetCount(rlm->_observers)), rlo);
        }
        if (NULL != rlm) {
            __CFRunLoopModeUnlock(rlm);
        }
    }
    __CFRunLoopUnlock(rl);
    return hasValue;
}
/// 从RunLoop中的Mode添加观察者
void CFRunLoopAddObserver(CFRunLoopRef rl, CFRunLoopObserverRef rlo, CFStringRef modeName) {
    CHECK_FOR_FORK();
    CFRunLoopModeRef rlm;
    if (__CFRunLoopIsDeallocating(rl)) return;
    if (!__CFIsValid(rlo) || (NULL != rlo->_runLoop && rlo->_runLoop != rl)) return;
    __CFRunLoopLock(rl);
    if (modeName == kCFRunLoopCommonModes) {
        CFSetRef set = rl->_commonModes ? CFSetCreateCopy(kCFAllocatorSystemDefault, rl->_commonModes) : NULL;
        if (NULL == rl->_commonModeItems) {
            rl->_commonModeItems = CFSetCreateMutable(kCFAllocatorSystemDefault, 0, &kCFTypeSetCallBacks);
        }
        CFSetAddValue(rl->_commonModeItems, rlo);
        if (NULL != set) {
            CFTypeRef context[2] = {rl, rlo};
            /* add new item to all common-modes */
            CFSetApplyFunction(set, (__CFRunLoopAddItemToCommonModes), (void *)context);
            CFRelease(set);
        }
    } else {
        rlm = __CFRunLoopFindMode(rl, modeName, true);
        if (NULL != rlm && NULL == rlm->_observers) {
            rlm->_observers = CFArrayCreateMutable(kCFAllocatorSystemDefault, 0, &kCFTypeArrayCallBacks);
        }
        if (NULL != rlm && !CFArrayContainsValue(rlm->_observers, CFRangeMake(0, CFArrayGetCount(rlm->_observers)), rlo)) {
            Boolean inserted = false;
            for (CFIndex idx = CFArrayGetCount(rlm->_observers); idx--; ) {
                CFRunLoopObserverRef obs = (CFRunLoopObserverRef)CFArrayGetValueAtIndex(rlm->_observers, idx);
                if (obs->_order <= rlo->_order) {
                    CFArrayInsertValueAtIndex(rlm->_observers, idx + 1, rlo);
                    inserted = true;
                    break;
                }
            }
            if (!inserted) {
                CFArrayInsertValueAtIndex(rlm->_observers, 0, rlo);
            }
            rlm->_observerMask |= rlo->_activities;
            __CFRunLoopObserverSchedule(rlo, rl, rlm);
        }
        if (NULL != rlm) {
            __CFRunLoopModeUnlock(rlm);
        }
    }
    __CFRunLoopUnlock(rl);
}
/// 从RunLoop中的Mode移除观察者
void CFRunLoopRemoveObserver(CFRunLoopRef rl, CFRunLoopObserverRef rlo, CFStringRef modeName) {
    CHECK_FOR_FORK();
    CFRunLoopModeRef rlm;
    __CFRunLoopLock(rl);
    if (modeName == kCFRunLoopCommonModes) {
        if (NULL != rl->_commonModeItems && CFSetContainsValue(rl->_commonModeItems, rlo)) {
            CFSetRef set = rl->_commonModes ? CFSetCreateCopy(kCFAllocatorSystemDefault, rl->_commonModes) : NULL;
            CFSetRemoveValue(rl->_commonModeItems, rlo);
            if (NULL != set) {
                CFTypeRef context[2] = {rl, rlo};
                /* remove new item from all common-modes */
                CFSetApplyFunction(set, (__CFRunLoopRemoveItemFromCommonModes), (void *)context);
                CFRelease(set);
            }
        } else {
        }
    } else {
        rlm = __CFRunLoopFindMode(rl, modeName, false);
        if (NULL != rlm && NULL != rlm->_observers) {
            CFRetain(rlo);
            CFIndex idx = CFArrayGetFirstIndexOfValue(rlm->_observers, CFRangeMake(0, CFArrayGetCount(rlm->_observers)), rlo);
            if (kCFNotFound != idx) {
                CFArrayRemoveValueAtIndex(rlm->_observers, idx);
                __CFRunLoopObserverCancel(rlo, rl, rlm);
            }
            CFRelease(rlo);
        }
        if (NULL != rlm) {
            __CFRunLoopModeUnlock(rlm);
        }
    }
    __CFRunLoopUnlock(rl);
}
/// 判断是否包含timer
Boolean CFRunLoopContainsTimer(CFRunLoopRef rl, CFRunLoopTimerRef rlt, CFStringRef modeName) {
    CHECK_FOR_FORK();
    if (NULL == rlt->_runLoop || rl != rlt->_runLoop) return false;
    Boolean hasValue = false;
    __CFRunLoopLock(rl);
    if (modeName == kCFRunLoopCommonModes) {
        if (NULL != rl->_commonModeItems) {
            hasValue = CFSetContainsValue(rl->_commonModeItems, rlt);
        }
    } else {
        CFRunLoopModeRef rlm = __CFRunLoopFindMode(rl, modeName, false);
        if (NULL != rlm) {
            if (NULL != rlm->_timers) {
                CFIndex idx = CFArrayGetFirstIndexOfValue(rlm->_timers, CFRangeMake(0, CFArrayGetCount(rlm->_timers)), rlt);
                hasValue = (kCFNotFound != idx);
            }
            __CFRunLoopModeUnlock(rlm);
        }
    }
    __CFRunLoopUnlock(rl);
    return hasValue;
}
/// 在RunLoop中的Mode添加TIMER
void CFRunLoopAddTimer(CFRunLoopRef rl, CFRunLoopTimerRef rlt, CFStringRef modeName) {    
    CHECK_FOR_FORK();
    if (__CFRunLoopIsDeallocating(rl)) return;
    if (!__CFIsValid(rlt) || (NULL != rlt->_runLoop && rlt->_runLoop != rl)) return;
    __CFRunLoopLock(rl);
    /// 加入RunLoop的Mode中的timer等于 commonModes
    if (modeName == kCFRunLoopCommonModes) {
        /// 取到commonModes所代表的Mode的集合
        CFSetRef set = rl->_commonModes ? CFSetCreateCopy(kCFAllocatorSystemDefault, rl->_commonModes) : NULL;
        if (NULL == rl->_commonModeItems) {
            /// 创建
            rl->_commonModeItems = CFSetCreateMutable(kCFAllocatorSystemDefault, 0, &kCFTypeSetCallBacks);
        }
        /// 把timer 加入到 commonModeItems
        CFSetAddValue(rl->_commonModeItems, rlt);
        if (NULL != set) {
            CFTypeRef context[2] = {rl, rlt};
            /* add new item to all common-modes */
            /// 遍历copy出来的set集合 把新的item 加入到 commonModes 去
            CFSetApplyFunction(set, (__CFRunLoopAddItemToCommonModes), (void *)context);
            CFRelease(set);
        }
    } else { // 不在 kCFRunLoopCommonModes
        /// 找到RunLoopMode
        CFRunLoopModeRef rlm = __CFRunLoopFindMode(rl, modeName, true);
        if (NULL != rlm) {
            if (NULL == rlm->_timers) {
                CFArrayCallBacks cb = kCFTypeArrayCallBacks;
                cb.equal = NULL;
                rlm->_timers = CFArrayCreateMutable(kCFAllocatorSystemDefault, 0, &cb);
            }
        }
        if (NULL != rlm && !CFSetContainsValue(rlt->_rlModes, rlm->_name)) {
            __CFRunLoopTimerLock(rlt);
            if (NULL == rlt->_runLoop) {
                rlt->_runLoop = rl;
            } else if (rl != rlt->_runLoop) {
                __CFRunLoopTimerUnlock(rlt);
                __CFRunLoopModeUnlock(rlm);
                __CFRunLoopUnlock(rl);
                return;
            }
            // mode name 加入到 runLoopModes集合里面
            CFSetAddValue(rlt->_rlModes, rlm->_name);
            __CFRunLoopTimerUnlock(rlt);
            __CFRunLoopTimerFireTSRLock();
            // 重新排列这个mode中的所有timer触发时刻，吧timer加入到array中
            __CFRepositionTimerInMode(rlm, rlt, false);
            __CFRunLoopTimerFireTSRUnlock();
            if (!_CFExecutableLinkedOnOrAfter(CFSystemVersionLion)) {
                // Normally we don't do this on behalf of clients, but for
                // backwards compatibility due to the change in timer handling...
                if (rl != CFRunLoopGetCurrent()) CFRunLoopWakeUp(rl);
            }
        }
        if (NULL != rlm) {
            __CFRunLoopModeUnlock(rlm);
        }
    }
    __CFRunLoopUnlock(rl);
}
/// 在RunLoop中的Mode移除TIMER
void CFRunLoopRemoveTimer(CFRunLoopRef rl, CFRunLoopTimerRef rlt, CFStringRef modeName) {
    CHECK_FOR_FORK();
    __CFRunLoopLock(rl);
    if (modeName == kCFRunLoopCommonModes) {
        if (NULL != rl->_commonModeItems && CFSetContainsValue(rl->_commonModeItems, rlt)) {
            CFSetRef set = rl->_commonModes ? CFSetCreateCopy(kCFAllocatorSystemDefault, rl->_commonModes) : NULL;
            CFSetRemoveValue(rl->_commonModeItems, rlt);
            if (NULL != set) {
                CFTypeRef context[2] = {rl, rlt};
                /* remove new item from all common-modes */
                CFSetApplyFunction(set, (__CFRunLoopRemoveItemFromCommonModes), (void *)context);
                CFRelease(set);
            }
        } else {
        }
    } else {
        CFRunLoopModeRef rlm = __CFRunLoopFindMode(rl, modeName, false);
        CFIndex idx = kCFNotFound;
        CFMutableArrayRef timerList = NULL;
        if (NULL != rlm) {
            timerList = rlm->_timers;
            if (NULL != timerList) {
                idx = CFArrayGetFirstIndexOfValue(timerList, CFRangeMake(0, CFArrayGetCount(timerList)), rlt);
            }
        }
        if (kCFNotFound != idx) {
            __CFRunLoopTimerLock(rlt);
            CFSetRemoveValue(rlt->_rlModes, rlm->_name);
            if (0 == CFSetGetCount(rlt->_rlModes)) {
                rlt->_runLoop = NULL;
            }
            __CFRunLoopTimerUnlock(rlt);
            CFArrayRemoveValueAtIndex(timerList, idx);
            __CFArmNextTimerInMode(rlm, rl);
        }
        if (NULL != rlm) {
            __CFRunLoopModeUnlock(rlm);
        }
    }
    __CFRunLoopUnlock(rl);
}

/* CFRunLoopSource */
/// 判断两个事件源是否相等
static Boolean __CFRunLoopSourceEqual(CFTypeRef cf1, CFTypeRef cf2) {	/* DOES CALLOUT */
    CFRunLoopSourceRef rls1 = (CFRunLoopSourceRef)cf1;
    CFRunLoopSourceRef rls2 = (CFRunLoopSourceRef)cf2;
    if (rls1 == rls2) return true;/// 指针地址相等 return true
    if (__CFIsValid(rls1) != __CFIsValid(rls2)) return false;
    /// order
    if (rls1->_order != rls2->_order) return false;
    /// 版本号
    if (rls1->_context.version0.version != rls2->_context.version0.version) return false;
    /// hash
    if (rls1->_context.version0.hash != rls2->_context.version0.hash) return false;
    /// equal
    if (rls1->_context.version0.equal != rls2->_context.version0.equal) return false;
    if (0 == rls1->_context.version0.version && rls1->_context.version0.perform != rls2->_context.version0.perform) return false;
    if (1 == rls1->_context.version0.version && rls1->_context.version1.perform != rls2->_context.version1.perform) return false;
    if (rls1->_context.version0.equal)
        return rls1->_context.version0.equal(rls1->_context.version0.info, rls2->_context.version0.info);
    return (rls1->_context.version0.info == rls2->_context.version0.info);
}
/// 取RunLoop中的hash
static CFHashCode __CFRunLoopSourceHash(CFTypeRef cf) {	/* DOES CALLOUT */
    CFRunLoopSourceRef rls = (CFRunLoopSourceRef)cf;
    if (rls->_context.version0.hash)
        return rls->_context.version0.hash(rls->_context.version0.info);
    return (CFHashCode)rls->_context.version0.info;
}

static CFStringRef __CFRunLoopSourceCopyDescription(CFTypeRef cf) {	/* DOES CALLOUT */
    CFRunLoopSourceRef rls = (CFRunLoopSourceRef)cf;
    CFStringRef result;
    CFStringRef contextDesc = NULL;
    if (NULL != rls->_context.version0.copyDescription) {
        contextDesc = rls->_context.version0.copyDescription(rls->_context.version0.info);
    }
    if (NULL == contextDesc) {
        void *addr = rls->_context.version0.version == 0 ? (void *)rls->_context.version0.perform : (rls->_context.version0.version == 1 ? (void *)rls->_context.version1.perform : NULL);
#if DEPLOYMENT_TARGET_WINDOWS
        contextDesc = CFStringCreateWithFormat(kCFAllocatorSystemDefault, NULL, CFSTR("<CFRunLoopSource context>{version = %ld, info = %p, callout = %p}"), rls->_context.version0.version, rls->_context.version0.info, addr);
#elif DEPLOYMENT_TARGET_MACOSX || DEPLOYMENT_TARGET_EMBEDDED || DEPLOYMENT_TARGET_EMBEDDED_MINI
        Dl_info info;
        const char *name = (dladdr(addr, &info) && info.dli_saddr == addr && info.dli_sname) ? info.dli_sname : "???";
        contextDesc = CFStringCreateWithFormat(kCFAllocatorSystemDefault, NULL, CFSTR("<CFRunLoopSource context>{version = %ld, info = %p, callout = %s (%p)}"), rls->_context.version0.version, rls->_context.version0.info, name, addr);
#endif
    }
#if DEPLOYMENT_TARGET_WINDOWS
    result = CFStringCreateWithFormat(kCFAllocatorSystemDefault, NULL, CFSTR("<CFRunLoopSource %p [%p]>{signalled = %s, valid = %s, order = %d, context = %@}"), cf, CFGetAllocator(rls), __CFRunLoopSourceIsSignaled(rls) ? "Yes" : "No", __CFIsValid(rls) ? "Yes" : "No", rls->_order, contextDesc);
#else
    result = CFStringCreateWithFormat(kCFAllocatorSystemDefault, NULL, CFSTR("<CFRunLoopSource %p [%p]>{signalled = %s, valid = %s, order = %ld, context = %@}"), cf, CFGetAllocator(rls), __CFRunLoopSourceIsSignaled(rls) ? "Yes" : "No", __CFIsValid(rls) ? "Yes" : "No", (unsigned long)rls->_order, contextDesc);
#endif
    CFRelease(contextDesc);
    return result;
}
/// source事件源析构
static void __CFRunLoopSourceDeallocate(CFTypeRef cf) {	/* DOES CALLOUT */
    CFRunLoopSourceRef rls = (CFRunLoopSourceRef)cf;
    CFRunLoopSourceInvalidate(rls);
    if (rls->_context.version0.release) {
        rls->_context.version0.release(rls->_context.version0.info);
    }
    pthread_mutex_destroy(&rls->_lock);
    memset((char *)cf + sizeof(CFRuntimeBase), 0, sizeof(struct __CFRunLoopSource) - sizeof(CFRuntimeBase));
}

static const CFRuntimeClass __CFRunLoopSourceClass = {
    _kCFRuntimeScannedObject,
    "CFRunLoopSource",
    NULL,      // init
    NULL,      // copy
    __CFRunLoopSourceDeallocate,
    __CFRunLoopSourceEqual,
    __CFRunLoopSourceHash,
    NULL,      // 
    __CFRunLoopSourceCopyDescription
};
/// 取source type id
CFTypeID CFRunLoopSourceGetTypeID(void) {
    static dispatch_once_t initOnce;
    dispatch_once(&initOnce, ^{ __kCFRunLoopSourceTypeID = _CFRuntimeRegisterClass(&__CFRunLoopSourceClass); });
    return __kCFRunLoopSourceTypeID;
}
/// 创建Source事件源
CFRunLoopSourceRef CFRunLoopSourceCreate(CFAllocatorRef allocator, CFIndex order, CFRunLoopSourceContext *context) {
    CHECK_FOR_FORK();
    CFRunLoopSourceRef memory;
    uint32_t size;
    if (NULL == context) CRASH("*** NULL context value passed to CFRunLoopSourceCreate(). (%d) ***", -1);
    
    size = sizeof(struct __CFRunLoopSource) - sizeof(CFRuntimeBase);
    /// _CFRuntimeCreateInstance 创建对象
    memory = (CFRunLoopSourceRef)_CFRuntimeCreateInstance(allocator, CFRunLoopSourceGetTypeID(), size, NULL);
    if (NULL == memory) {
        return NULL;
    }
    __CFSetValid(memory);
    __CFRunLoopSourceUnsetSignaled(memory);
    __CFRunLoopLockInit(&memory->_lock);
    memory->_bits = 0;
    memory->_order = order;
    memory->_runLoops = NULL;
    size = 0;
    switch (context->version) {
        case 0:
            size = sizeof(CFRunLoopSourceContext);
            break;
        case 1:
            size = sizeof(CFRunLoopSourceContext1);
            break;
    }
    objc_memmove_collectable(&memory->_context, context, size);
    if (context->retain) {
        memory->_context.version0.info = (void *)context->retain(context->info);
    }
    return memory;
}
/// 取得事件源的Source
CFIndex CFRunLoopSourceGetOrder(CFRunLoopSourceRef rls) {
    CHECK_FOR_FORK();
    __CFGenericValidateType(rls, CFRunLoopSourceGetTypeID());
    return rls->_order;
}
/// Source唤醒RunLoop
static void __CFRunLoopSourceWakeUpLoop(const void *value, void *context) {
    CFRunLoopWakeUp((CFRunLoopRef)value);
}
/// 移除RunLoop Mode 中的所有 source
static void __CFRunLoopSourceRemoveFromRunLoop(const void *value, void *context) {
    CFRunLoopRef rl = (CFRunLoopRef)value;
    CFTypeRef *params = (CFTypeRef *)context;
    CFRunLoopSourceRef rls = (CFRunLoopSourceRef)params[0];
    CFIndex idx;
    if (rl == params[1]) return;
    
    // CFRunLoopRemoveSource will lock the run loop while it
    // needs that, but we also lock it out here to keep
    // changes from occurring for this whole sequence.
    __CFRunLoopLock(rl);
    CFArrayRef array = CFRunLoopCopyAllModes(rl);
    for (idx = CFArrayGetCount(array); idx--;) {
        CFStringRef modeName = (CFStringRef)CFArrayGetValueAtIndex(array, idx);
        CFRunLoopRemoveSource(rl, rls, modeName);
    }
    CFRunLoopRemoveSource(rl, rls, kCFRunLoopCommonModes);
    __CFRunLoopUnlock(rl);
    CFRelease(array);
    params[1] = rl;
}
?// 设置当前RunLoop中的Source无效 - 也就是移除当前RunLoop的source
void CFRunLoopSourceInvalidate(CFRunLoopSourceRef rls) {
    CHECK_FOR_FORK();
    __CFGenericValidateType(rls, CFRunLoopSourceGetTypeID());
    __CFRunLoopSourceLock(rls);
    CFRetain(rls);
    if (__CFIsValid(rls)) {
        CFBagRef rloops = rls->_runLoops;
        __CFUnsetValid(rls);
        __CFRunLoopSourceUnsetSignaled(rls);
        if (NULL != rloops) {
            // To avoid A->B, B->A lock ordering issues when coming up
            // towards the run loop from a source, the source has to be
            // unlocked, which means we have to protect from object
            // invalidation.
            rls->_runLoops = NULL; // transfer ownership to local stack
            __CFRunLoopSourceUnlock(rls);
            CFTypeRef params[2] = {rls, NULL};
            CFBagApplyFunction(rloops, (__CFRunLoopSourceRemoveFromRunLoop), params);
            CFRelease(rloops);
            __CFRunLoopSourceLock(rls);
        }
        /* for hashing- and equality-use purposes, can't actually release the context here */
    }
    __CFRunLoopSourceUnlock(rls);
    CFRelease(rls);
}
/// 判断Source是否有效
Boolean CFRunLoopSourceIsValid(CFRunLoopSourceRef rls) {
    CHECK_FOR_FORK();
    __CFGenericValidateType(rls, CFRunLoopSourceGetTypeID());
    return __CFIsValid(rls);
}
/// 获取source 上下文
void CFRunLoopSourceGetContext(CFRunLoopSourceRef rls, CFRunLoopSourceContext *context) {
    CHECK_FOR_FORK();
    __CFGenericValidateType(rls, CFRunLoopSourceGetTypeID());
    CFAssert1(0 == context->version || 1 == context->version, __kCFLogAssertion, "%s(): context version not initialized to 0 or 1", __PRETTY_FUNCTION__);
    CFIndex size = 0;
    switch (context->version) {
        case 0:
            size = sizeof(CFRunLoopSourceContext);
            break;
        case 1:
            size = sizeof(CFRunLoopSourceContext1);
            break;
    }
    memmove(context, &rls->_context, size);
}
/// 为RunLoop中的Mode中的Source 设置信号标志
void CFRunLoopSourceSignal(CFRunLoopSourceRef rls) {
    CHECK_FOR_FORK();
    __CFRunLoopSourceLock(rls);
    if (__CFIsValid(rls)) {
        __CFRunLoopSourceSetSignaled(rls);
    }
    __CFRunLoopSourceUnlock(rls);
}
/// 判断Source是否已经被信号标志
Boolean CFRunLoopSourceIsSignalled(CFRunLoopSourceRef rls) {
    CHECK_FOR_FORK();
    __CFRunLoopSourceLock(rls);
    Boolean ret = __CFRunLoopSourceIsSignaled(rls) ? true : false;
    __CFRunLoopSourceUnlock(rls);
    return ret;
}
/// 唤醒
CF_PRIVATE void _CFRunLoopSourceWakeUpRunLoops(CFRunLoopSourceRef rls) {
    CFBagRef loops = NULL;
    __CFRunLoopSourceLock(rls);
    if (__CFIsValid(rls) && NULL != rls->_runLoops) {
        loops = CFBagCreateCopy(kCFAllocatorSystemDefault, rls->_runLoops);
    }
    __CFRunLoopSourceUnlock(rls);
    if (loops) {
        CFBagApplyFunction(loops, __CFRunLoopSourceWakeUpLoop, NULL);
        CFRelease(loops);
    }
}

/* CFRunLoopObserver */

static CFStringRef __CFRunLoopObserverCopyDescription(CFTypeRef cf) {	/* DOES CALLOUT */
    CFRunLoopObserverRef rlo = (CFRunLoopObserverRef)cf;
    CFStringRef result;
    CFStringRef contextDesc = NULL;
    if (NULL != rlo->_context.copyDescription) {
        contextDesc = rlo->_context.copyDescription(rlo->_context.info);
    }
    if (!contextDesc) {
        contextDesc = CFStringCreateWithFormat(kCFAllocatorSystemDefault, NULL, CFSTR("<CFRunLoopObserver context %p>"), rlo->_context.info);
    }
#if DEPLOYMENT_TARGET_WINDOWS
    result = CFStringCreateWithFormat(kCFAllocatorSystemDefault, NULL, CFSTR("<CFRunLoopObserver %p [%p]>{valid = %s, activities = 0x%x, repeats = %s, order = %d, callout = %p, context = %@}"), cf, CFGetAllocator(rlo), __CFIsValid(rlo) ? "Yes" : "No", rlo->_activities, __CFRunLoopObserverRepeats(rlo) ? "Yes" : "No", rlo->_order, rlo->_callout, contextDesc);    
#elif DEPLOYMENT_TARGET_MACOSX || DEPLOYMENT_TARGET_EMBEDDED || DEPLOYMENT_TARGET_EMBEDDED_MINI
    void *addr = rlo->_callout;
    Dl_info info;
    const char *name = (dladdr(addr, &info) && info.dli_saddr == addr && info.dli_sname) ? info.dli_sname : "???";
    result = CFStringCreateWithFormat(kCFAllocatorSystemDefault, NULL, CFSTR("<CFRunLoopObserver %p [%p]>{valid = %s, activities = 0x%lx, repeats = %s, order = %ld, callout = %s (%p), context = %@}"), cf, CFGetAllocator(rlo), __CFIsValid(rlo) ? "Yes" : "No", (long)rlo->_activities, __CFRunLoopObserverRepeats(rlo) ? "Yes" : "No", (long)rlo->_order, name, addr, contextDesc);
#endif
    CFRelease(contextDesc);
    return result;
}
/// Observer 析构
static void __CFRunLoopObserverDeallocate(CFTypeRef cf) {	/* DOES CALLOUT */
    CFRunLoopObserverRef rlo = (CFRunLoopObserverRef)cf;
    CFRunLoopObserverInvalidate(rlo);
    pthread_mutex_destroy(&rlo->_lock);
}

static const CFRuntimeClass __CFRunLoopObserverClass = {
    0,
    "CFRunLoopObserver",
    NULL,      // init
    NULL,      // copy
    __CFRunLoopObserverDeallocate,
    NULL,
    NULL,
    NULL,      // 
    __CFRunLoopObserverCopyDescription
};
/// 获取Observer Type ID
CFTypeID CFRunLoopObserverGetTypeID(void) {
    static dispatch_once_t initOnce;
    dispatch_once(&initOnce, ^{ __kCFRunLoopObserverTypeID = _CFRuntimeRegisterClass(&__CFRunLoopObserverClass); });
    return __kCFRunLoopObserverTypeID;
}
/// 创建Observer观察者
CFRunLoopObserverRef CFRunLoopObserverCreate(CFAllocatorRef allocator, CFOptionFlags activities, Boolean repeats, CFIndex order, CFRunLoopObserverCallBack callout, CFRunLoopObserverContext *context) {
    CHECK_FOR_FORK();
    CFRunLoopObserverRef memory;
    UInt32 size;
    size = sizeof(struct __CFRunLoopObserver) - sizeof(CFRuntimeBase);
    /// _CFRuntimeCreateInstance 创建对象
    memory = (CFRunLoopObserverRef)_CFRuntimeCreateInstance(allocator, CFRunLoopObserverGetTypeID(), size, NULL);
    if (NULL == memory) {
        return NULL;
    }
    __CFSetValid(memory);
    __CFRunLoopObserverUnsetFiring(memory);
    if (repeats) {
        __CFRunLoopObserverSetRepeats(memory);
    } else {
        __CFRunLoopObserverUnsetRepeats(memory);
    }
    /// 初始化锁
    __CFRunLoopLockInit(&memory->_lock);
    memory->_runLoop = NULL;
    memory->_rlCount = 0;
    memory->_activities = activities;
    memory->_order = order;
    memory->_callout = callout;
    if (context) {
        if (context->retain) {
            memory->_context.info = (void *)context->retain(context->info);
        } else {
            memory->_context.info = context->info;
        }
        memory->_context.retain = context->retain;
        memory->_context.release = context->release;
        memory->_context.copyDescription = context->copyDescription;
    } else {
        memory->_context.info = 0;
        memory->_context.retain = 0;
        memory->_context.release = 0;
        memory->_context.copyDescription = 0;
    }
    return memory;
}
/// block 方式创建 Observer
static void _runLoopObserverWithBlockContext(CFRunLoopObserverRef observer, CFRunLoopActivity activity, void *opaqueBlock) {
    typedef void (^observer_block_t) (CFRunLoopObserverRef observer, CFRunLoopActivity activity);
    observer_block_t block = (observer_block_t)opaqueBlock;
    block(observer, activity);
}

CFRunLoopObserverRef CFRunLoopObserverCreateWithHandler(CFAllocatorRef allocator, CFOptionFlags activities, Boolean repeats, CFIndex order,
                                                        void (^block) (CFRunLoopObserverRef observer, CFRunLoopActivity activity)) {
    CFRunLoopObserverContext blockContext;
    blockContext.version = 0;
    blockContext.info = (void *)block;
    blockContext.retain = (const void *(*)(const void *info))_Block_copy;
    blockContext.release = (void (*)(const void *info))_Block_release;
    blockContext.copyDescription = NULL;
    return CFRunLoopObserverCreate(allocator, activities, repeats, order, _runLoopObserverWithBlockContext, &blockContext);
}
/// 获取当前Observer的activities
CFOptionFlags CFRunLoopObserverGetActivities(CFRunLoopObserverRef rlo) {
    CHECK_FOR_FORK();
    __CFGenericValidateType(rlo, CFRunLoopObserverGetTypeID());
    return rlo->_activities;
}
/// order
CFIndex CFRunLoopObserverGetOrder(CFRunLoopObserverRef rlo) {
    CHECK_FOR_FORK();
    __CFGenericValidateType(rlo, CFRunLoopObserverGetTypeID());
    return rlo->_order;
}
/// 判断是否repeat
Boolean CFRunLoopObserverDoesRepeat(CFRunLoopObserverRef rlo) {
    CHECK_FOR_FORK();
    __CFGenericValidateType(rlo, CFRunLoopObserverGetTypeID());
    return __CFRunLoopObserverRepeats(rlo);
}
/// 设置无效
void CFRunLoopObserverInvalidate(CFRunLoopObserverRef rlo) {    /* DOES CALLOUT */
    CHECK_FOR_FORK();
    __CFGenericValidateType(rlo, CFRunLoopObserverGetTypeID());
    __CFRunLoopObserverLock(rlo);
    CFRetain(rlo);
    if (__CFIsValid(rlo)) {
        CFRunLoopRef rl = rlo->_runLoop;
        void *info = rlo->_context.info;
        rlo->_context.info = NULL;
        __CFUnsetValid(rlo);
        if (NULL != rl) {
            // To avoid A->B, B->A lock ordering issues when coming up
            // towards the run loop from an observer, it has to be
            // unlocked, which means we have to protect from object
            // invalidation.
            CFRetain(rl);
            __CFRunLoopObserverUnlock(rlo);
            // CFRunLoopRemoveObserver will lock the run loop while it
            // needs that, but we also lock it out here to keep
            // changes from occurring for this whole sequence.
            __CFRunLoopLock(rl);
            CFArrayRef array = CFRunLoopCopyAllModes(rl);
            for (CFIndex idx = CFArrayGetCount(array); idx--;) {
                CFStringRef modeName = (CFStringRef)CFArrayGetValueAtIndex(array, idx);
                CFRunLoopRemoveObserver(rl, rlo, modeName);
            }
            CFRunLoopRemoveObserver(rl, rlo, kCFRunLoopCommonModes);
            __CFRunLoopUnlock(rl);
            CFRelease(array);
            CFRelease(rl);
            __CFRunLoopObserverLock(rlo);
        }
        if (NULL != rlo->_context.release) {
            rlo->_context.release(info);        /* CALLOUT */
        }
    }
    __CFRunLoopObserverUnlock(rlo);
    CFRelease(rlo);
}
/// 判断是否有效
Boolean CFRunLoopObserverIsValid(CFRunLoopObserverRef rlo) {
    CHECK_FOR_FORK();
    return __CFIsValid(rlo);
}
/// 取得上下文
void CFRunLoopObserverGetContext(CFRunLoopObserverRef rlo, CFRunLoopObserverContext *context) {
    CHECK_FOR_FORK();
    __CFGenericValidateType(rlo, CFRunLoopObserverGetTypeID());
    CFAssert1(0 == context->version, __kCFLogAssertion, "%s(): context version not initialized to 0", __PRETTY_FUNCTION__);
    *context = rlo->_context;
}

#pragma mark -
#pragma mark CFRunLoopTimer

static CFStringRef __CFRunLoopTimerCopyDescription(CFTypeRef cf) {	/* DOES CALLOUT */
    CFRunLoopTimerRef rlt = (CFRunLoopTimerRef)cf;
    CFStringRef contextDesc = NULL;
    if (NULL != rlt->_context.copyDescription) {
        contextDesc = rlt->_context.copyDescription(rlt->_context.info);
    }
    if (NULL == contextDesc) {
        contextDesc = CFStringCreateWithFormat(kCFAllocatorSystemDefault, NULL, CFSTR("<CFRunLoopTimer context %p>"), rlt->_context.info);
    }
    void *addr = (void *)rlt->_callout;
    char libraryName[2048];
    char functionName[2048];
    void *functionPtr = NULL;
    libraryName[0] = '?'; libraryName[1] = '\0';
    functionName[0] = '?'; functionName[1] = '\0';
    CFStringRef result = CFStringCreateWithFormat(kCFAllocatorSystemDefault, NULL,
                                                  CFSTR("<CFRunLoopTimer %p [%p]>{valid = %s, firing = %s, interval = %0.09g, tolerance = %0.09g, next fire date = %0.09g (%0.09g @ %lld), callout = %s (%p / %p) (%s), context = %@}"),
                                                  cf,
                                                  CFGetAllocator(rlt),
                                                  __CFIsValid(rlt) ? "Yes" : "No",
                                                  __CFRunLoopTimerIsFiring(rlt) ? "Yes" : "No",
                                                  rlt->_interval,
                                                  rlt->_tolerance,
                                                  rlt->_nextFireDate,
                                                  rlt->_nextFireDate - CFAbsoluteTimeGetCurrent(),
                                                  rlt->_fireTSR,
                                                  functionName,
                                                  addr,
                                                  functionPtr,
                                                  libraryName,
                                                  contextDesc);
    CFRelease(contextDesc);
    return result;
}
/// timer 析构
static void __CFRunLoopTimerDeallocate(CFTypeRef cf) {	/* DOES CALLOUT */
    //CFLog(6, CFSTR("__CFRunLoopTimerDeallocate(%p)"), cf);
    CFRunLoopTimerRef rlt = (CFRunLoopTimerRef)cf;
    __CFRunLoopTimerSetDeallocating(rlt);
    CFRunLoopTimerInvalidate(rlt);	/* DOES CALLOUT */
    CFRelease(rlt->_rlModes);
    rlt->_rlModes = NULL;
    pthread_mutex_destroy(&rlt->_lock);
}

static const CFRuntimeClass __CFRunLoopTimerClass = {
    0,
    "CFRunLoopTimer",
    NULL,      // init
    NULL,      // copy
    __CFRunLoopTimerDeallocate,
    NULL,	// equal
    NULL,
    NULL,      // 
    __CFRunLoopTimerCopyDescription
};
/// 取得timer type id
CFTypeID CFRunLoopTimerGetTypeID(void) {
    static dispatch_once_t initOnce;
    dispatch_once(&initOnce, ^{ __kCFRunLoopTimerTypeID = _CFRuntimeRegisterClass(&__CFRunLoopTimerClass); });
    return __kCFRunLoopTimerTypeID;
}
/// 创建 timer
CFRunLoopTimerRef CFRunLoopTimerCreate(CFAllocatorRef allocator, CFAbsoluteTime fireDate, CFTimeInterval interval, CFOptionFlags flags, CFIndex order, CFRunLoopTimerCallBack callout, CFRunLoopTimerContext *context) {
    CHECK_FOR_FORK();
    if (isnan(interval)) {
        CRSetCrashLogMessage("NaN was used as an interval for a CFRunLoopTimer");
        HALT;
    }
    CFRunLoopTimerRef memory;
    UInt32 size;
    size = sizeof(struct __CFRunLoopTimer) - sizeof(CFRuntimeBase);
    /// _CFRuntimeCreateInstance 创建对象
    memory = (CFRunLoopTimerRef)_CFRuntimeCreateInstance(allocator, CFRunLoopTimerGetTypeID(), size, NULL);
    if (NULL == memory) {
        return NULL;
    }
    __CFSetValid(memory);
    __CFRunLoopTimerUnsetFiring(memory);
    __CFRunLoopLockInit(&memory->_lock);
    memory->_runLoop = NULL;
    memory->_rlModes = CFSetCreateMutable(kCFAllocatorSystemDefault, 0, &kCFTypeSetCallBacks);
    memory->_order = order;
    if (interval < 0.0) interval = 0.0;
    memory->_interval = interval;
    memory->_tolerance = 0.0;
    if (TIMER_DATE_LIMIT < fireDate) fireDate = TIMER_DATE_LIMIT;
    memory->_nextFireDate = fireDate;
    memory->_fireTSR = 0ULL;
    uint64_t now2 = mach_absolute_time();
    CFAbsoluteTime now1 = CFAbsoluteTimeGetCurrent();
    if (fireDate < now1) {
        memory->_fireTSR = now2;
    } else if (TIMER_INTERVAL_LIMIT < fireDate - now1) {
        memory->_fireTSR = now2 + __CFTimeIntervalToTSR(TIMER_INTERVAL_LIMIT);
    } else {
        memory->_fireTSR = now2 + __CFTimeIntervalToTSR(fireDate - now1);
    }
    memory->_callout = callout;
    if (NULL != context) {
        if (context->retain) {
            memory->_context.info = (void *)context->retain(context->info);
        } else {
            memory->_context.info = context->info;
        }
        memory->_context.retain = context->retain;
        memory->_context.release = context->release;
        memory->_context.copyDescription = context->copyDescription;
    } else {
        memory->_context.info = 0;
        memory->_context.retain = 0;
        memory->_context.release = 0;
        memory->_context.copyDescription = 0;
    }
    return memory;
}
/// block方式创建
static void _runLoopTimerWithBlockContext(CFRunLoopTimerRef timer, void *opaqueBlock) {
    typedef void (^timer_block_t) (CFRunLoopTimerRef timer);
    timer_block_t block = (timer_block_t)opaqueBlock;
    block(timer);
}

CFRunLoopTimerRef CFRunLoopTimerCreateWithHandler(CFAllocatorRef allocator, CFAbsoluteTime fireDate, CFTimeInterval interval, CFOptionFlags flags, CFIndex order,
                                                  void (^block) (CFRunLoopTimerRef timer)) {
    
    CFRunLoopTimerContext blockContext;
    blockContext.version = 0;
    blockContext.info = (void *)block;
    blockContext.retain = (const void *(*)(const void *info))_Block_copy;
    blockContext.release = (void (*)(const void *info))_Block_release;
    blockContext.copyDescription = NULL;
    return CFRunLoopTimerCreate(allocator, fireDate, interval, flags, order, _runLoopTimerWithBlockContext, &blockContext);
}
/// 下次到来的时间
CFAbsoluteTime CFRunLoopTimerGetNextFireDate(CFRunLoopTimerRef rlt) {
    CHECK_FOR_FORK();
    CF_OBJC_FUNCDISPATCHV(CFRunLoopTimerGetTypeID(), CFAbsoluteTime, (NSTimer *)rlt, _cffireTime);
    __CFGenericValidateType(rlt, CFRunLoopTimerGetTypeID());
    CFAbsoluteTime at = 0.0;
    __CFRunLoopTimerLock(rlt);
    __CFRunLoopTimerFireTSRLock();
    if (__CFIsValid(rlt)) {
        at = rlt->_nextFireDate;
    }
    __CFRunLoopTimerFireTSRUnlock();
    __CFRunLoopTimerUnlock(rlt);
    return at;
}
/// 设置下次到来的时间
void CFRunLoopTimerSetNextFireDate(CFRunLoopTimerRef rlt, CFAbsoluteTime fireDate) {
    CHECK_FOR_FORK();
    if (!__CFIsValid(rlt)) return;
    if (TIMER_DATE_LIMIT < fireDate) fireDate = TIMER_DATE_LIMIT;
    uint64_t nextFireTSR = 0ULL;
    uint64_t now2 = mach_absolute_time();
    CFAbsoluteTime now1 = CFAbsoluteTimeGetCurrent();
    if (fireDate < now1) {
        nextFireTSR = now2;
    } else if (TIMER_INTERVAL_LIMIT < fireDate - now1) {
        nextFireTSR = now2 + __CFTimeIntervalToTSR(TIMER_INTERVAL_LIMIT);
    } else {
        nextFireTSR = now2 + __CFTimeIntervalToTSR(fireDate - now1);
    }
    __CFRunLoopTimerLock(rlt);
    if (NULL != rlt->_runLoop) {
        CFIndex cnt = CFSetGetCount(rlt->_rlModes);
        STACK_BUFFER_DECL(CFTypeRef, modes, cnt);
        CFSetGetValues(rlt->_rlModes, (const void **)modes);
        // To avoid A->B, B->A lock ordering issues when coming up
        // towards the run loop from a source, the timer has to be
        // unlocked, which means we have to protect from object
        // invalidation, although that's somewhat expensive.
        for (CFIndex idx = 0; idx < cnt; idx++) {
            CFRetain(modes[idx]);
        }
        CFRunLoopRef rl = (CFRunLoopRef)CFRetain(rlt->_runLoop);
        __CFRunLoopTimerUnlock(rlt);
        __CFRunLoopLock(rl);
        for (CFIndex idx = 0; idx < cnt; idx++) {
            CFStringRef name = (CFStringRef)modes[idx];
            modes[idx] = __CFRunLoopFindMode(rl, name, false);
            CFRelease(name);
        }
        __CFRunLoopTimerFireTSRLock();
        rlt->_fireTSR = nextFireTSR;
        rlt->_nextFireDate = fireDate;
        for (CFIndex idx = 0; idx < cnt; idx++) {
            CFRunLoopModeRef rlm = (CFRunLoopModeRef)modes[idx];
            if (rlm) {
                __CFRepositionTimerInMode(rlm, rlt, true);
            }
        }
        __CFRunLoopTimerFireTSRUnlock();
        for (CFIndex idx = 0; idx < cnt; idx++) {
            __CFRunLoopModeUnlock((CFRunLoopModeRef)modes[idx]);
        }
        __CFRunLoopUnlock(rl);
        // This is setting the date of a timer, not a direct
        // interaction with a run loop, so we'll do a wakeup
        // (which may be costly) for the caller, just in case.
        // (And useful for binary compatibility with older
        // code used to the older timer implementation.)
        if (rl != CFRunLoopGetCurrent()) CFRunLoopWakeUp(rl);
        CFRelease(rl);
    } else {
        __CFRunLoopTimerFireTSRLock();
        rlt->_fireTSR = nextFireTSR;
        rlt->_nextFireDate = fireDate;
        __CFRunLoopTimerFireTSRUnlock();
        __CFRunLoopTimerUnlock(rlt);
    }
}
/// 获取 timer时间间隔
CFTimeInterval CFRunLoopTimerGetInterval(CFRunLoopTimerRef rlt) {
    CHECK_FOR_FORK();
    CF_OBJC_FUNCDISPATCHV(CFRunLoopTimerGetTypeID(), CFTimeInterval, (NSTimer *)rlt, timeInterval);
    __CFGenericValidateType(rlt, CFRunLoopTimerGetTypeID());
    return rlt->_interval;
}
/// repeat
Boolean CFRunLoopTimerDoesRepeat(CFRunLoopTimerRef rlt) {
    CHECK_FOR_FORK();
    __CFGenericValidateType(rlt, CFRunLoopTimerGetTypeID());
    return (0.0 < rlt->_interval);
}
/// order
CFIndex CFRunLoopTimerGetOrder(CFRunLoopTimerRef rlt) {
    CHECK_FOR_FORK();
    __CFGenericValidateType(rlt, CFRunLoopTimerGetTypeID());
    return rlt->_order;
}
/// 设置无效
void CFRunLoopTimerInvalidate(CFRunLoopTimerRef rlt) {	/* DOES CALLOUT */
    CHECK_FOR_FORK();
    CF_OBJC_FUNCDISPATCHV(CFRunLoopTimerGetTypeID(), void, (NSTimer *)rlt, invalidate);
    __CFGenericValidateType(rlt, CFRunLoopTimerGetTypeID());
    __CFRunLoopTimerLock(rlt);
    if (!__CFRunLoopTimerIsDeallocating(rlt)) {
        CFRetain(rlt);
    }
    if (__CFIsValid(rlt)) {
        CFRunLoopRef rl = rlt->_runLoop;
        void *info = rlt->_context.info;
        rlt->_context.info = NULL;
        __CFUnsetValid(rlt);
        if (NULL != rl) {
            CFIndex cnt = CFSetGetCount(rlt->_rlModes);
            STACK_BUFFER_DECL(CFStringRef, modes, cnt);
            CFSetGetValues(rlt->_rlModes, (const void **)modes);
            // To avoid A->B, B->A lock ordering issues when coming up
            // towards the run loop from a source, the timer has to be
            // unlocked, which means we have to protect from object
            // invalidation, although that's somewhat expensive.
            for (CFIndex idx = 0; idx < cnt; idx++) {
                CFRetain(modes[idx]);
            }
            CFRetain(rl);
            __CFRunLoopTimerUnlock(rlt);
            // CFRunLoopRemoveTimer will lock the run loop while it
            // needs that, but we also lock it out here to keep
            // changes from occurring for this whole sequence.
            __CFRunLoopLock(rl);
            for (CFIndex idx = 0; idx < cnt; idx++) {
                CFRunLoopRemoveTimer(rl, rlt, modes[idx]);
            }
            CFRunLoopRemoveTimer(rl, rlt, kCFRunLoopCommonModes);
            __CFRunLoopUnlock(rl);
            for (CFIndex idx = 0; idx < cnt; idx++) {
                CFRelease(modes[idx]);
            }
            CFRelease(rl);
            __CFRunLoopTimerLock(rlt);
        }
        if (NULL != rlt->_context.release) {
            rlt->_context.release(info);	/* CALLOUT */
        }
    }
    __CFRunLoopTimerUnlock(rlt);
    if (!__CFRunLoopTimerIsDeallocating(rlt)) {
        CFRelease(rlt);
    }
}
/// 判断是否有效
Boolean CFRunLoopTimerIsValid(CFRunLoopTimerRef rlt) {
    CHECK_FOR_FORK();
    CF_OBJC_FUNCDISPATCHV(CFRunLoopTimerGetTypeID(), Boolean, (NSTimer *)rlt, isValid);
    __CFGenericValidateType(rlt, CFRunLoopTimerGetTypeID());
    return __CFIsValid(rlt);
}
/// 取得timer上下文
void CFRunLoopTimerGetContext(CFRunLoopTimerRef rlt, CFRunLoopTimerContext *context) {
    CHECK_FOR_FORK();
    __CFGenericValidateType(rlt, CFRunLoopTimerGetTypeID());
    CFAssert1(0 == context->version, __kCFLogAssertion, "%s(): context version not initialized to 0", __PRETTY_FUNCTION__);
    *context = rlt->_context;
}

CFTimeInterval CFRunLoopTimerGetTolerance(CFRunLoopTimerRef rlt) {
#if DEPLOYMENT_TARGET_MACOSX || DEPLOYMENT_TARGET_EMBEDDED || DEPLOYMENT_TARGET_EMBEDDED_MINI
    CHECK_FOR_FORK();
    CF_OBJC_FUNCDISPATCHV(CFRunLoopTimerGetTypeID(), CFTimeInterval, (NSTimer *)rlt, tolerance);
    __CFGenericValidateType(rlt, CFRunLoopTimerGetTypeID());
    return rlt->_tolerance;
#else
    return 0.0;
#endif
}

void CFRunLoopTimerSetTolerance(CFRunLoopTimerRef rlt, CFTimeInterval tolerance) {
#if DEPLOYMENT_TARGET_MACOSX || DEPLOYMENT_TARGET_EMBEDDED || DEPLOYMENT_TARGET_EMBEDDED_MINI
    CHECK_FOR_FORK();
    CF_OBJC_FUNCDISPATCHV(CFRunLoopTimerGetTypeID(), void, (NSTimer *)rlt, setTolerance:tolerance);
    __CFGenericValidateType(rlt, CFRunLoopTimerGetTypeID());
    /*
     * dispatch rules:
     *
     * For the initial timer fire at 'start', the upper limit to the allowable
     * delay is set to 'leeway' nanoseconds. For the subsequent timer fires at
     * 'start' + N * 'interval', the upper limit is MIN('leeway','interval'/2).
     */
    if (rlt->_interval > 0) {
        rlt->_tolerance = MIN(tolerance, rlt->_interval / 2);
    } else {
        // Tolerance must be a positive value or zero
        if (tolerance < 0) tolerance = 0.0;
        rlt->_tolerance = tolerance;
    }
#endif
}

