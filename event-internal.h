/*
 * Copyright (c) 2000-2007 Niels Provos <provos@citi.umich.edu>
 * Copyright (c) 2007-2012 Niels Provos and Nick Mathewson
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef EVENT_INTERNAL_H_INCLUDED_
#define EVENT_INTERNAL_H_INCLUDED_

#ifdef __cplusplus
extern "C" {
#endif

#include "event2/event-config.h"
#include "evconfig-private.h"

#include <time.h>
#include <sys/queue.h>
#include "event2/event_struct.h"
#include "minheap-internal.h"
#include "evsignal-internal.h"
#include "mm-internal.h"
#include "defer-internal.h"

/* map union members back */

/* mutually exclusive */
#define ev_signal_next	ev_.ev_signal.ev_signal_next
#define ev_io_next	ev_.ev_io.ev_io_next
#define ev_io_timeout	ev_.ev_io.ev_timeout

/* used only by signals */
// 回调函数执行次数
#define ev_ncalls	ev_.ev_signal.ev_ncalls
#define ev_pncalls	ev_.ev_signal.ev_pncalls

#define ev_pri ev_evcallback.evcb_pri

// 事件标志, 标记事件处理器的状态
#define ev_flags ev_evcallback.evcb_flags

// 指定event_base执行事件处理器的回调函数的行为
#define ev_closure ev_evcallback.evcb_closure
#define ev_callback ev_evcallback.evcb_cb_union.evcb_callback
#define ev_arg ev_evcallback.evcb_arg

//ev_closurev参数

// 默认行为
#define EV_CLOSURE_EVENT 0
// 执行信号事件处理器回调函数的时候 执行ev_ncalls次回调函数
#define EV_CLOSURE_EVENT_SIGNAL 1
// 执行完回调函数后, 再次将事件加入注册事件队列中
#define EV_CLOSURE_EVENT_PERSIST 2
/** A simple callback. Uses the evcb_selfcb callback. */
#define EV_CLOSURE_CB_SELF 3
/** A finalizing callback. Uses the evcb_cbfinalize callback. */
#define EV_CLOSURE_CB_FINALIZE 4
/** A finalizing event. Uses the evcb_evfinalize callback. */
#define EV_CLOSURE_EVENT_FINALIZE 5
/** A finalizing event that should get freed after. Uses the evcb_evfinalize
 * callback. */
#define EV_CLOSURE_EVENT_FINALIZE_FREE 6


// 为给定的event_base声明后端的结构体
struct eventop {
	// 后端IO复用技术的名称
	const char *name;

	// 初始化 函数需要初始化所有要用的属性
	// 返回的指针会被event_init存储在event_base.evbase
	// 失败后返回NULL
	void *(*init)(struct event_base *);
	/** Enable reading/writing on a given fd or signal.  'events' will be
	 * the events that we're trying to enable: one or more of EV_READ,
	 * EV_WRITE, EV_SIGNAL, and EV_ET.  'old' will be those events that
	 * were enabled on this fd previously.  'fdinfo' will be a structure
	 * associated with the fd by the evmap; its size is defined by the
	 * fdinfo field below.  It will be set to 0 the first time the fd is
	 * added.  The function should return 0 on success and -1 on error.
	 */
	// 注册事件
	int (*add)(struct event_base *, evutil_socket_t fd, short old, short events, void *fdinfo);
	/** As "add", except 'events' contains the events we mean to disable. */
	// 删除事件
	int (*del)(struct event_base *, evutil_socket_t fd, short old, short events, void *fdinfo);
	/** Function to implement the core of an event loop.  It must see which
	    added events are ready, and cause event_active to be called for each
	    active event (usually via event_io_active or such).  It should
	    return 0 on success and -1 on error.
	 */
	// 等待事件
	int (*dispatch)(struct event_base *, struct timeval *);
	// 释放IO复用机制使用的资源
	void (*dealloc)(struct event_base *);
	// 标记fork后是否需要重新初始化event_base的标志位
	int need_reinit;
	// 用于设定io复用技术支持的一些特性
	enum event_method_feature features;
	/** Length of the extra information we should record for each fd that
	    has one or more active events.  This information is recorded
	    as part of the evmap entry for each fd, and passed as an argument
	    to the add and del functions above.
	 */
	size_t fdinfo_len;
};

#ifdef _WIN32
/* If we're on win32, then file descriptors are not nice low densely packed
   integers.  Instead, they are pointer-like windows handles, and we want to
   use a hashtable instead of an array to map fds to events.
*/
#define EVMAP_USE_HT
#endif

// 如果定义了EVMAP_USE_HT, 则将event_io_map定义为哈希表, 哈希表记录event_map_entry对象和
// IO事件队列之间的映射关系, 实际上存储了文件描述符和IO事件处理器之前的映射关系
#ifdef EVMAP_USE_HT
#define HT_NO_CACHE_HASH_VALUES
#include "ht-internal.h"
struct event_map_entry;
HT_HEAD(event_io_map, event_map_entry);
#else
#define event_io_map event_signal_map
#endif


// 使用信号作为下标得到对应的evmap_io或evmap_signal. 如果EVMAP_USE_HT没有被声明
// 这个结构体也同样被用为event_io_map 使用fd作为下标得到event?
struct event_signal_map {
	// 存放evmap_io*或evmap_signal*的数组
	void **entries;
	// 数组大小
	int nentries;
};

/* A list of events waiting on a given 'common' timeout value.  Ordinarily,
 * events waiting for a timeout wait on a minheap.  Sometimes, however, a
 * queue can be faster.
 **/
struct common_timeout_list {
	/* List of events currently waiting in the queue. */
	struct event_list events;
	/* 'magic' timeval used to indicate the duration of events in this
	 * queue. */
	struct timeval duration;
	/* Event that triggers whenever one of the events in the queue is
	 * ready to activate */
	struct event timeout_event;
	/* The event_base that this timeout list is part of */
	struct event_base *base;
};

/** Mask used to get the real tv_usec value from a common timeout. */
#define COMMON_TIMEOUT_MICROSECONDS_MASK       0x000fffff

struct event_change;

/* List of 'changes' since the last call to eventop.dispatch.  Only maintained
 * if the backend is using changesets. */
struct event_changelist {
	struct event_change *changes;
	int n_changes;
	int changes_size;
};

#ifndef EVENT__DISABLE_DEBUG_MODE
/* Global internal flag: set to one if debug mode is on. */
extern int event_debug_mode_on_;
#define EVENT_DEBUG_MODE_IS_ON() (event_debug_mode_on_)
#else
#define EVENT_DEBUG_MODE_IS_ON() (0)
#endif

TAILQ_HEAD(evcallback_list, event_callback);

/* Sets up an event for processing once */
struct event_once {
	LIST_ENTRY(event_once) next_once;
	struct event ev;

	void (*cb)(evutil_socket_t, short, void *);
	void *arg;
};

struct event_base {
	// 记录选择的I/O复用机制
	const struct eventop *evsel;
	// 指向IO复用机制真正存储的数据
	void *evbase;

	// 事件变换队列 如果一个文件描述符上注册的事件被多次修改, 则可以使用缓冲避免重复的系统调用
	// 比如epoll_ctl, 仅能用于时间复杂度O(1)的IO复用技术
	struct event_changelist changelist;

	// 信号的后端处理机制
	const struct eventop *evsigsel;
	// 信号事件处理器使用的数据结构, 其中封装了socketpair创建的管道. 用于信号处理函数和
	// 事件多路分发器之间的通信, 统一事件源的思路
	struct evsig_info sig;

	// 添加到event_base的虚拟(所有, 激活)事件数量, 虚拟(所有, 激活)事件最大数量
	int virtual_event_count;
	int virtual_event_count_max;
	int event_count;
	int event_count_max;
	int event_count_active;
	int event_count_active_max;

	// 处理完事件后 是否退出循环
	int event_gotterm;
	// 是否立即终止循环
	int event_break;
	// 是否启动一个新的事件循环
	int event_continue;

	// 当前正在处理的活动事件队列的优先级
	int event_running_priority;

	// 标记事件循环是否已经启动, 防止重入
	int running_loop;

	/** Set to the number of deferred_cbs we've made 'active' in the
	 * loop.  This is a hack to prevent starvation; it would be smarter
	 * to just use event_config_set_max_dispatch_interval's max_callbacks
	 * feature */
	int n_deferreds_queued;

	// 活动事件队列数组. 索引值越小的队列优先级越高. 高优先级的活动事件队列中的事件处理器被优先处理
	struct evcallback_list *activequeues;
	// 活动事件队列数组的大小 说明有nactivequeues个不同优先级的活动事件队列
	int nactivequeues;
	/** A list of event_callbacks that should become active the next time
	 * we process events, but not this time. */
	struct evcallback_list active_later_queue;

	// 共同超时逻辑

	// 管理通用定时器队列 实体数量 总数
	struct common_timeout_list **common_timeout_queues;
	int n_common_timeouts;
	int n_common_timeouts_allocated;

	// 文件描述符和IO事件之间的映射关系表
	struct event_io_map io;
	// 信号值和信号事件之间的映射关系表
	struct event_signal_map sigmap;
	// 时间堆
	struct min_heap timeheap;

	// 管理系统时间的成员
	struct timeval tv_cache;
	struct evutil_monotonic_timer monotonic_timer;
	struct timeval tv_clock_diff;
	time_t last_updated_clock_diff;

#ifndef EVENT__DISABLE_THREAD_SUPPORT
	// 多线程支持
	
	// 当前运行该event_base的事件循环的线程
	unsigned long th_owner_id;
	// 独占锁
	void *th_base_lock;
	// 当前事件循环正在执行哪个事件处理器的回调函数
	void *current_event_cond;
	// 等待的线程数
	int current_event_waiters;
#endif
	// 正在处理的事件处理器的回调函数
	struct event_callback *current_event;

#ifdef _WIN32
	/** IOCP support structure, if IOCP is enabled. */
	struct event_iocp_port *iocp;
#endif

	/** Flags that this base was configured with */
	enum event_base_config_flag flags;

	struct timeval max_dispatch_time;
	int max_dispatch_callbacks;
	int limit_callbacks_after_prio;

	/* Notify main thread to wake up break, etc. */
	/** True if the base already has a pending notify, and we don't need
	 * to add any more. */
	int is_notify_pending;
	/** A socketpair used by some th_notify functions to wake up the main
	 * thread. */
	evutil_socket_t th_notify_fd[2];
	/** An event used by some th_notify functions to wake up the main
	 * thread. */
	struct event th_notify;
	/** A function used to wake up the main thread from another thread. */
	int (*th_notify_fn)(struct event_base *base);

	/** Saved seed for weak random number generator. Some backends use
	 * this to produce fairness among sockets. Protected by th_base_lock. */
	struct evutil_weakrand_state weakrand_seed;

	/** List of event_onces that have not yet fired. */
	LIST_HEAD(once_event_list, event_once) once_events;

};

struct event_config_entry {
	TAILQ_ENTRY(event_config_entry) next;

	const char *avoid_method;
};

/** Internal structure: describes the configuration we want for an event_base
 * that we're about to allocate. */
struct event_config {
	TAILQ_HEAD(event_configq, event_config_entry) entries;

	int n_cpus_hint;
	struct timeval max_dispatch_interval;
	int max_dispatch_callbacks;
	int limit_callbacks_after_prio;
	enum event_method_feature require_features;
	enum event_base_config_flag flags;
};

/* Internal use only: Functions that might be missing from <sys/queue.h> */
#ifndef LIST_END
#define LIST_END(head)			NULL
#endif

#ifndef TAILQ_FIRST
#define	TAILQ_FIRST(head)		((head)->tqh_first)
#endif
#ifndef TAILQ_END
#define	TAILQ_END(head)			NULL
#endif
#ifndef TAILQ_NEXT
#define	TAILQ_NEXT(elm, field)		((elm)->field.tqe_next)
#endif

#ifndef TAILQ_FOREACH
#define TAILQ_FOREACH(var, head, field)					\
	for ((var) = TAILQ_FIRST(head);					\
	     (var) != TAILQ_END(head);					\
	     (var) = TAILQ_NEXT(var, field))
#endif

#ifndef TAILQ_INSERT_BEFORE
#define	TAILQ_INSERT_BEFORE(listelm, elm, field) do {			\
	(elm)->field.tqe_prev = (listelm)->field.tqe_prev;		\
	(elm)->field.tqe_next = (listelm);				\
	*(listelm)->field.tqe_prev = (elm);				\
	(listelm)->field.tqe_prev = &(elm)->field.tqe_next;		\
} while (0)
#endif

#define N_ACTIVE_CALLBACKS(base)					\
	((base)->event_count_active)

int evsig_set_handler_(struct event_base *base, int evsignal,
			  void (*fn)(int));
int evsig_restore_handler_(struct event_base *base, int evsignal);

int event_add_nolock_(struct event *ev,
    const struct timeval *tv, int tv_is_absolute);
/** Argument for event_del_nolock_. Tells event_del not to block on the event
 * if it's running in another thread. */
#define EVENT_DEL_NOBLOCK 0
/** Argument for event_del_nolock_. Tells event_del to block on the event
 * if it's running in another thread, regardless of its value for EV_FINALIZE
 */
#define EVENT_DEL_BLOCK 1
/** Argument for event_del_nolock_. Tells event_del to block on the event
 * if it is running in another thread and it doesn't have EV_FINALIZE set.
 */
#define EVENT_DEL_AUTOBLOCK 2
/** Argument for event_del_nolock_. Tells event_del to procede even if the
 * event is set up for finalization rather for regular use.*/
#define EVENT_DEL_EVEN_IF_FINALIZING 3
int event_del_nolock_(struct event *ev, int blocking);
int event_remove_timer_nolock_(struct event *ev);

void event_active_nolock_(struct event *ev, int res, short count);
EVENT2_EXPORT_SYMBOL
int event_callback_activate_(struct event_base *, struct event_callback *);
int event_callback_activate_nolock_(struct event_base *, struct event_callback *);
int event_callback_cancel_(struct event_base *base,
    struct event_callback *evcb);

void event_callback_finalize_nolock_(struct event_base *base, unsigned flags, struct event_callback *evcb, void (*cb)(struct event_callback *, void *));
EVENT2_EXPORT_SYMBOL
void event_callback_finalize_(struct event_base *base, unsigned flags, struct event_callback *evcb, void (*cb)(struct event_callback *, void *));
int event_callback_finalize_many_(struct event_base *base, int n_cbs, struct event_callback **evcb, void (*cb)(struct event_callback *, void *));


EVENT2_EXPORT_SYMBOL
void event_active_later_(struct event *ev, int res);
void event_active_later_nolock_(struct event *ev, int res);
int event_callback_activate_later_nolock_(struct event_base *base,
    struct event_callback *evcb);
int event_callback_cancel_nolock_(struct event_base *base,
    struct event_callback *evcb, int even_if_finalizing);
void event_callback_init_(struct event_base *base,
    struct event_callback *cb);

/* FIXME document. */
EVENT2_EXPORT_SYMBOL
void event_base_add_virtual_(struct event_base *base);
void event_base_del_virtual_(struct event_base *base);

/** For debugging: unless assertions are disabled, verify the referential
    integrity of the internal data structures of 'base'.  This operation can
    be expensive.

    Returns on success; aborts on failure.
*/
EVENT2_EXPORT_SYMBOL
void event_base_assert_ok_(struct event_base *base);
void event_base_assert_ok_nolock_(struct event_base *base);


/* Helper function: Call 'fn' exactly once every inserted or active event in
 * the event_base 'base'.
 *
 * If fn returns 0, continue on to the next event. Otherwise, return the same
 * value that fn returned.
 *
 * Requires that 'base' be locked.
 */
int event_base_foreach_event_nolock_(struct event_base *base,
    event_base_foreach_event_cb cb, void *arg);

/* Cleanup function to reset debug mode during shutdown.
 *
 * Calling this function doesn't mean it'll be possible to re-enable
 * debug mode if any events were added.
 */
void event_disable_debug_mode(void);

#ifdef __cplusplus
}
#endif

#endif /* EVENT_INTERNAL_H_INCLUDED_ */
