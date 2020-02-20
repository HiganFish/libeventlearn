# I/O框架库概述
## Reactor模式
![](https://lsmg-img.oss-cn-beijing.aliyuncs.com/%E6%A1%86%E6%9E%B6%E5%AD%A6%E4%B9%A0/%E5%9B%BE12-1IO%E6%A1%86%E6%9E%B6%E7%BB%84%E7%BB%84%E4%BB%B6.png)
句柄(Handler)
由于统一了事件源, 一个事件一般跟一个句柄绑定在一起, 事件就绪之后 会通过句柄通知这一个事件.
在Linux中 I/O 事件对应的句柄->文件描述符, 信号事件->信号值

事件多路分发器(EventDemultiplexer)
事件的到来是随机的, 异步的. 所以只能通过一个循环一直等待事件并进行处理 --- 事件循环
一般通过IO复用实现 select poll epoll_wait

事件处理器(EventHandle)
具体事件处理器(ConcreteEventHandler)
事件处理器执行事件对应的业务逻辑, 通常包含一个或多个handler_event回调函数, 这些回调函数在事件循环中被执行

Reactor
handler_events: 执行事件循环 重复等待事件, 然后依次调用对应的事件处理器
register_handler: 向事件多路分发器中注册事件
remove_handler: 从中删除一个事件

![](https://lsmg-img.oss-cn-beijing.aliyuncs.com/%E6%A1%86%E6%9E%B6%E5%AD%A6%E4%B9%A0/%E5%9B%BE12-2%20IO%E6%A1%86%E6%9E%B6%E5%BA%93%E7%9A%84%E5%B7%A5%E4%BD%9C%E6%97%B6%E5%BA%8F%E5%9B%BE.png)

## Libevent源码分析
```c++
#include <sys/signal.h>
#include <event.h>
#include <cstdio>

void signal_cb(int fd, short event, void* argc)
{
    event_base* base = (event_base*)argc;
    timeval delay = {2, 0};

    printf("Caught an interrupt signal\n");
    event_base_loopexit(base, &delay);
}

void timeout_cb(int fd, short event, void* argc)
{
    printf("timeout\n");
}
int main()
{
    // 相当于创建一个Reactor实例
    event_base* base = event_init();
    
    event* signal_event = evsignal_new(base, SIGINT, signal_cb, base);
    event_add(signal_event, nullptr);
    
    event* timeout_event = evtimer_new(base, timeout_cb, nullptr);
    timeval tv{1, 0};
    event_add(timeout_event, &tv);
    
    event_base_dispatch(base);
    event_free(signal_event);
    event_free(timeout_event);
    event_base_free(base);
}
```
创建一个事件处理器 然后为绑定上相应的回调函数.
然后把这个事件处理器注册到事件队列中中,

然后事件多路分发器依靠循环一直等待事件的到来, 事件到来后通知相应的事件处理器
Reactor则管理这些

首先要去了解下 `事件处理器` 对应的就是event这个结构体
```c++
struct event {
	struct event_callback ev_evcallback;

	// 事件处理器从属的 event_base
	struct event_base *ev_base;
	// 信号值 或者 文件描述符
	evutil_socket_t ev_fd;
	// 定时器的超时时间
	struct timeval ev_timeout;
	
	// 仅用于定时事件
	union {
		// 队列--指出在通用定时器中的位置
		TAILQ_ENTRY(event) ev_next_with_common_timeout;
		// 时间堆--指出了在时间堆中的位置
		int min_heap_idx;
	} ev_timeout_pos;

	union {
		struct {
			// 通过这个成员 将具有相同文件描述符的IO事件处理器串联起来
			LIST_ENTRY (event) ev_io_next;
			struct timeval ev_timeout;
		} ev_io;
		struct {
			// 相同信号的串联起来
			LIST_ENTRY (event) ev_signal_next;
			short ev_ncalls;
			/* Allows deletes in callback */
			short *ev_pncalls;
		} ev_signal;
	} ev_;

	// 事件类型, 可以通过位处理设置非互斥事件
	short ev_events;
	// 当前激活事件的类型, 说明被激活的原因
	short ev_res;
};
```
可以看到其中有很多的属性, 三种事件对应的不同的属性.

这些属性的填充函数
`evsignal_new``evtimer_new`是宏 统一调用`event_new`
`event_new`调用`event_assign`来进行主要的填充


```
//@通过宏封装注册函数

一个事件生成函数 经过宏的封装(可以自动填充某些此事件用不到的参数)可以更方便的对应不同事件的生成, 既统一了注册, 又方便用户调用
```
属性之一便是回调函数, 事件回调函数有自己的规定
```
//@统一事件回调函数

void (*callback)(evutil_socket_t, short, void *)
这样能够统一回调函数的格式, 同时方便管理
```
事件处理器创建完毕, 该把事件处理器添加到事件注册队列. 样例代码中通过的`event_add`函数来实现将事件处理器添加到事件注册队列
`event_add`实际由`event_add_nolock_`实现 所以接下来是`event_add_nolock_`函数的说明

```
//@事件处理器的分发实现
将传入的event按照不同类型的事件处理器 分别处理
(因为event_new已经填充了ev_events说明事件类型)

IO事件 添加绑定
信号事件 绑定相应的信号
定时器 放入相关的的时间管理数据结构中
```
使用`event_queue_insert_inserted`进行注册
这里的代码2.1.11 与书上的差别较大, 少了多一半的功能, 也没有被抽成函数, 暂不知道对应的功能代码去了哪里
照书上来说`event_queue_insert_inserted`实现的是将事件处理器加入到`event_base`的某个事件队列中. 对于新添加的IO和信号事件处理器, 还需要让事件多路分发器来监听对应的事件, 然后建立相应的映射关系. 分别使用`evmap_io_add_`和`evmap_signal_add_`(相当于图中的`register_event`)建立映射. 

`evmap_io_add_`中有一个结构体`event_io_map`
`event_io_map`会根据不同的平台最终对应不同的数据结构

`evmap_io_add_`函数
函数中用到的东西, 我目前吸收不了....... 总之是为将IO事件处理器加入到`event_base`的事件队列中实现的

`eventop`结构体 是`event_base`中封装IO复用机制的结构体, 提供了统一的接口
```c++
// 为给定的event_base声明后端的结构体
struct eventop {
	// 后端IO复用技术的名称
	const char *name;

	// 初始化 函数需要初始化所有要用的属性
	// 返回的指针会被event_init存储在event_base.evbase
	// 失败后返回NULL
	void *(*init)(struct event_base *);
	// 注册事件
	int (*add)(struct event_base *, evutil_socket_t fd, short old, short events, void *fdinfo);
	// 删除事件
	int (*del)(struct event_base *, evutil_socket_t fd, short old, short events, void *fdinfo);
	// 等待事件
	int (*dispatch)(struct event_base *, struct timeval *);
	// 释放IO复用机制使用的资源
	void (*dealloc)(struct event_base *);
	// 标记fork后是否需要重新初始化event_base的标志位
	int need_reinit;
	// 用于设定io复用技术支持的一些特性
	enum event_method_feature features;
	// 额外内存的分配
	size_t fdinfo_len;
};
```

`event_base`是Libevent的Reactor. 超长结构体 删除了我不理解的部分
```c++
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
};
```

事件循环, libevent的动力, 即事件循环