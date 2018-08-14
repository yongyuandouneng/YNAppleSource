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

/*	CFRunLoop.h
	Copyright (c) 1998-2014, Apple Inc. All rights reserved.

*/

#if !defined(__COREFOUNDATION_CFRUNLOOP__)
#define __COREFOUNDATION_CFRUNLOOP__ 1
#include <CoreFoundation/CFBase.h>
#include <CoreFoundation/CFArray.h>
#include <CoreFoundation/CFDate.h>
#include <CoreFoundation/CFString.h>
#if (TARGET_OS_MAC && !(TARGET_OS_EMBEDDED || TARGET_OS_IPHONE)) || (TARGET_OS_EMBEDDED || TARGET_OS_IPHONE)
#include <mach/port.h>
#endif

CF_IMPLICIT_BRIDGING_ENABLED
CF_EXTERN_C_BEGIN
// RunLoopREF
typedef struct __CFRunLoop * CFRunLoopRef;
// ref
typedef struct __CFRunLoopSource * CFRunLoopSourceRef;
// observer
typedef struct __CFRunLoopObserver * CFRunLoopObserverRef;
/// timer
typedef struct CF_BRIDGED_MUTABLE_TYPE(NSTimer) __CFRunLoopTimer * CFRunLoopTimerRef;

/* Reasons for CFRunLoopRunInMode() to Return */
enum {
    kCFRunLoopRunFinished = 1,
    kCFRunLoopRunStopped = 2,
    kCFRunLoopRunTimedOut = 3,
    kCFRunLoopRunHandledSource = 4
};
// RunLoop 观察者 活动
/* Run Loop Observer Activities */
typedef CF_OPTIONS(CFOptionFlags, CFRunLoopActivity) {
    /// 即将进入运行循环
    kCFRunLoopEntry = (1UL << 0),
    /// 当运行循环将要处理一个计时器时。
    kCFRunLoopBeforeTimers = (1UL << 1),
    /// 当运行循环将要处理一个输入源时。
    kCFRunLoopBeforeSources = (1UL << 2),
    /// 当运行循环将要休眠
    kCFRunLoopBeforeWaiting = (1UL << 5),
    /// 当运行循环被唤醒时,在被唤醒之前，处理唤醒事件
    kCFRunLoopAfterWaiting = (1UL << 6),
    /// 退出运行循环。
    kCFRunLoopExit = (1UL << 7),
    /// 观察所有的活动
    kCFRunLoopAllActivities = 0x0FFFFFFFU
};
/// kCFRunLoopDefaultMode 为默认的Mode
CF_EXPORT const CFStringRef kCFRunLoopDefaultMode;
/// kCFRunLoopCommonModes 为公共的Mode
CF_EXPORT const CFStringRef kCFRunLoopCommonModes;
/// 获取RunLoop
CF_EXPORT CFTypeID CFRunLoopGetTypeID(void);
/// 获取当前的RunLoop
CF_EXPORT CFRunLoopRef CFRunLoopGetCurrent(void);
/// 获取主线程的RunLoop
CF_EXPORT CFRunLoopRef CFRunLoopGetMain(void);
/// Copy传入RunLoop 的Mode
CF_EXPORT CFStringRef CFRunLoopCopyCurrentMode(CFRunLoopRef rl);
/// Copy 传入RunLoop 的所有Mode
CF_EXPORT CFArrayRef CFRunLoopCopyAllModes(CFRunLoopRef rl);
/// 为RunLoop 添加 Mode
CF_EXPORT void CFRunLoopAddCommonMode(CFRunLoopRef rl, CFStringRef mode);
/// 返回的时间是下一个就绪计时器
CF_EXPORT CFAbsoluteTime CFRunLoopGetNextTimerFireDate(CFRunLoopRef rl, CFStringRef mode);
/// 启动RunLoop
CF_EXPORT void CFRunLoopRun(void);
/// 在指定Mode中启动RunLoop
CF_EXPORT SInt32 CFRunLoopRunInMode(CFStringRef mode, CFTimeInterval seconds, Boolean returnAfterSourceHandled);
/// 判断RunLoop是否正在等待
CF_EXPORT Boolean CFRunLoopIsWaiting(CFRunLoopRef rl);
/// 唤醒RunLoop
CF_EXPORT void CFRunLoopWakeUp(CFRunLoopRef rl);
/// 停止RunLoop
CF_EXPORT void CFRunLoopStop(CFRunLoopRef rl);

#if __BLOCKS__
CF_EXPORT void CFRunLoopPerformBlock(CFRunLoopRef rl, CFTypeRef mode, void (^block)(void)) CF_AVAILABLE(10_6, 4_0); 
#endif
/// 判断RunLoop 中的指定Mode 是否包含某个Source
CF_EXPORT Boolean CFRunLoopContainsSource(CFRunLoopRef rl, CFRunLoopSourceRef source, CFStringRef mode);
/// 为RunLoop 添加Source 到Mode中
CF_EXPORT void CFRunLoopAddSource(CFRunLoopRef rl, CFRunLoopSourceRef source, CFStringRef mode);
/// 从RunLoop 指定的Mode 中移除 Source
CF_EXPORT void CFRunLoopRemoveSource(CFRunLoopRef rl, CFRunLoopSourceRef source, CFStringRef mode);

/// 判断RunLoop 中的指定Mode 是否包含某个Observer
CF_EXPORT Boolean CFRunLoopContainsObserver(CFRunLoopRef rl, CFRunLoopObserverRef observer, CFStringRef mode);
/// 为RunLoop 添加Observer 到Mode中
CF_EXPORT void CFRunLoopAddObserver(CFRunLoopRef rl, CFRunLoopObserverRef observer, CFStringRef mode);
/// 从RunLoop 指定的Mode 中移除 Observer
CF_EXPORT void CFRunLoopRemoveObserver(CFRunLoopRef rl, CFRunLoopObserverRef observer, CFStringRef mode);

/// 判断RunLoop 中的指定Mode 是否包含某个Timer
CF_EXPORT Boolean CFRunLoopContainsTimer(CFRunLoopRef rl, CFRunLoopTimerRef timer, CFStringRef mode);
/// 为RunLoop 添加Timer 到Mode中
CF_EXPORT void CFRunLoopAddTimer(CFRunLoopRef rl, CFRunLoopTimerRef timer, CFStringRef mode);
/// 从RunLoop 指定的Mode 中移除 Timer
CF_EXPORT void CFRunLoopRemoveTimer(CFRunLoopRef rl, CFRunLoopTimerRef timer, CFStringRef mode);

/// SourceContext 结构体
typedef struct {
    CFIndex	version;
    void *	info;
    const void *(*retain)(const void *info);
    void	(*release)(const void *info);
    CFStringRef	(*copyDescription)(const void *info);
    Boolean	(*equal)(const void *info1, const void *info2);
    CFHashCode	(*hash)(const void *info);
    void	(*schedule)(void *info, CFRunLoopRef rl, CFStringRef mode);
    void	(*cancel)(void *info, CFRunLoopRef rl, CFStringRef mode);
    void	(*perform)(void *info);
} CFRunLoopSourceContext;

/// SourceContext1 结构体
typedef struct {
    CFIndex	version;
    void *	info;
    const void *(*retain)(const void *info);
    void	(*release)(const void *info);
    CFStringRef	(*copyDescription)(const void *info);
    Boolean	(*equal)(const void *info1, const void *info2);
    CFHashCode	(*hash)(const void *info);
#if (TARGET_OS_MAC && !(TARGET_OS_EMBEDDED || TARGET_OS_IPHONE)) || (TARGET_OS_EMBEDDED || TARGET_OS_IPHONE)
    mach_port_t	(*getPort)(void *info);
    void *	(*perform)(void *msg, CFIndex size, CFAllocatorRef allocator, void *info);
#else
    void *	(*getPort)(void *info);
    void	(*perform)(void *info);
#endif
} CFRunLoopSourceContext1;

/// 获取Source 类型 ID
CF_EXPORT CFTypeID CFRunLoopSourceGetTypeID(void);

/// 创建事件源
CF_EXPORT CFRunLoopSourceRef CFRunLoopSourceCreate(CFAllocatorRef allocator, CFIndex order, CFRunLoopSourceContext *context);

CF_EXPORT CFIndex CFRunLoopSourceGetOrder(CFRunLoopSourceRef source);
/// 设置Source成无效Source
CF_EXPORT void CFRunLoopSourceInvalidate(CFRunLoopSourceRef source);
/// 判断Source是否有效
CF_EXPORT Boolean CFRunLoopSourceIsValid(CFRunLoopSourceRef source);
/// 取得Source的context
CF_EXPORT void CFRunLoopSourceGetContext(CFRunLoopSourceRef source, CFRunLoopSourceContext *context);
/// 发送信号
CF_EXPORT void CFRunLoopSourceSignal(CFRunLoopSourceRef source);

/// Observer观察者的Context结构体
typedef struct {
    CFIndex	version;
    void *	info;
    const void *(*retain)(const void *info);
    void	(*release)(const void *info);
    CFStringRef	(*copyDescription)(const void *info);
} CFRunLoopObserverContext;

/// observer call back block
typedef void (*CFRunLoopObserverCallBack)(CFRunLoopObserverRef observer, CFRunLoopActivity activity, void *info);

/// 取得Observer的 类型 ID
CF_EXPORT CFTypeID CFRunLoopObserverGetTypeID(void);

/// 创建Observer
CF_EXPORT CFRunLoopObserverRef CFRunLoopObserverCreate(CFAllocatorRef allocator, CFOptionFlags activities, Boolean repeats, CFIndex order, CFRunLoopObserverCallBack callout, CFRunLoopObserverContext *context);

/// Block 方法创建Observer
#if __BLOCKS__
CF_EXPORT CFRunLoopObserverRef CFRunLoopObserverCreateWithHandler(CFAllocatorRef allocator, CFOptionFlags activities, Boolean repeats, CFIndex order, void (^block) (CFRunLoopObserverRef observer, CFRunLoopActivity activity)) CF_AVAILABLE(10_7, 5_0);
#endif

/// 取得观察者正在活动的flag
CF_EXPORT CFOptionFlags CFRunLoopObserverGetActivities(CFRunLoopObserverRef observer);
/// 判断观察者是否重复
CF_EXPORT Boolean CFRunLoopObserverDoesRepeat(CFRunLoopObserverRef observer);
/// 获取观察者的Index
CF_EXPORT CFIndex CFRunLoopObserverGetOrder(CFRunLoopObserverRef observer);
/// 设置观察者无效
CF_EXPORT void CFRunLoopObserverInvalidate(CFRunLoopObserverRef observer);
/// 判断观察者是否有效
CF_EXPORT Boolean CFRunLoopObserverIsValid(CFRunLoopObserverRef observer);
/// 取得观察者的Context
CF_EXPORT void CFRunLoopObserverGetContext(CFRunLoopObserverRef observer, CFRunLoopObserverContext *context);

/// Timer上下文的结构体
typedef struct {
    CFIndex	version;
    void *	info;
    const void *(*retain)(const void *info);
    void	(*release)(const void *info);
    CFStringRef	(*copyDescription)(const void *info);
} CFRunLoopTimerContext;

/// Timer call back block
typedef void (*CFRunLoopTimerCallBack)(CFRunLoopTimerRef timer, void *info);

/// 取得Timer的类型ID
CF_EXPORT CFTypeID CFRunLoopTimerGetTypeID(void);
/// 创建Timer
CF_EXPORT CFRunLoopTimerRef CFRunLoopTimerCreate(CFAllocatorRef allocator, CFAbsoluteTime fireDate, CFTimeInterval interval, CFOptionFlags flags, CFIndex order, CFRunLoopTimerCallBack callout, CFRunLoopTimerContext *context);
/// block 方式创建Timer
#if __BLOCKS__
CF_EXPORT CFRunLoopTimerRef CFRunLoopTimerCreateWithHandler(CFAllocatorRef allocator, CFAbsoluteTime fireDate, CFTimeInterval interval, CFOptionFlags flags, CFIndex order, void (^block) (CFRunLoopTimerRef timer)) CF_AVAILABLE(10_7, 5_0);
#endif

/// 取得下次Timer要启动的时间
CF_EXPORT CFAbsoluteTime CFRunLoopTimerGetNextFireDate(CFRunLoopTimerRef timer);
/// 设置Timer启动时间
CF_EXPORT void CFRunLoopTimerSetNextFireDate(CFRunLoopTimerRef timer, CFAbsoluteTime fireDate);
/// 取得Timer的时间间隔
CF_EXPORT CFTimeInterval CFRunLoopTimerGetInterval(CFRunLoopTimerRef timer);
/// 判断Timer是否Repeat
CF_EXPORT Boolean CFRunLoopTimerDoesRepeat(CFRunLoopTimerRef timer);
/// 获取Timer的执行循序下标
CF_EXPORT CFIndex CFRunLoopTimerGetOrder(CFRunLoopTimerRef timer);
/// 设置Timer无效
CF_EXPORT void CFRunLoopTimerInvalidate(CFRunLoopTimerRef timer);
/// 判断Timer是否有效
CF_EXPORT Boolean CFRunLoopTimerIsValid(CFRunLoopTimerRef timer);
/// 取得Timer的上下文
CF_EXPORT void CFRunLoopTimerGetContext(CFRunLoopTimerRef timer, CFRunLoopTimerContext *context);

// Setting a tolerance for a timer allows it to fire later than the scheduled fire date, improving the ability of the system to optimize for increased power savings and responsiveness. The timer may fire at any time between its scheduled fire date and the scheduled fire date plus the tolerance. The timer will not fire before the scheduled fire date. For repeating timers, the next fire date is calculated from the original fire date regardless of tolerance applied at individual fire times, to avoid drift. The default value is zero, which means no additional tolerance is applied. The system reserves the right to apply a small amount of tolerance to certain timers regardless of the value of this property.
// As the user of the timer, you will have the best idea of what an appropriate tolerance for a timer may be. A general rule of thumb, though, is to set the tolerance to at least 10% of the interval, for a repeating timer. Even a small amount of tolerance will have a significant positive impact on the power usage of your application. The system may put a maximum value of the tolerance.
CF_EXPORT CFTimeInterval CFRunLoopTimerGetTolerance(CFRunLoopTimerRef timer) CF_AVAILABLE(10_9, 7_0);
CF_EXPORT void CFRunLoopTimerSetTolerance(CFRunLoopTimerRef timer, CFTimeInterval tolerance) CF_AVAILABLE(10_9, 7_0);

CF_EXTERN_C_END
CF_IMPLICIT_BRIDGING_DISABLED

#endif /* ! __COREFOUNDATION_CFRUNLOOP__ */

