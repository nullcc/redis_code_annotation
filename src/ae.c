/* A simple event-driven programming library. Originally I wrote this code
 * for the Jim's event-loop (Jim is a Tcl interpreter) but later translated
 * it in form of a library for easy reuse.
 *
 * Copyright (c) 2006-2010, Salvatore Sanfilippo <antirez at gmail dot com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Redis nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <poll.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#include "ae.h"
#include "zmalloc.h"
#include "config.h"

/* Include the best multiplexing layer supported by this system.
 * The following should be ordered by performances, descending. */
/* 包含了系统支持的最佳的I/O多路复用机制。下面的异步I/O按照性能顺序降序排列。 */
#ifdef HAVE_EVPORT
#include "ae_evport.c"
#else
    #ifdef HAVE_EPOLL
    #include "ae_epoll.c"
    #else
        #ifdef HAVE_KQUEUE
        #include "ae_kqueue.c"
        #else
        #include "ae_select.c"
        #endif
    #endif
#endif

/* 创建一个事件循环 */
aeEventLoop *aeCreateEventLoop(int setsize) {
    aeEventLoop *eventLoop;
    int i;

    if ((eventLoop = zmalloc(sizeof(*eventLoop))) == NULL) goto err;
    eventLoop->events = zmalloc(sizeof(aeFileEvent)*setsize);  // 分配已注册的文件事件数组空间
    eventLoop->fired = zmalloc(sizeof(aeFiredEvent)*setsize);  // 分配已触发的事件数组空间
    if (eventLoop->events == NULL || eventLoop->fired == NULL) goto err;
    eventLoop->setsize = setsize;  // 设置文件描述符的最大监听数
    eventLoop->lastTime = time(NULL);  // 上一次检测的系统时间
    eventLoop->timeEventHead = NULL;  // 已注册的定时器事件数组
    eventLoop->timeEventNextId = 0;  // 下一个生成的定时器事件id
    eventLoop->stop = 0;  // 事件循环停止标志，0表示正在运行
    eventLoop->maxfd = -1;  // 当前已经注册的最大文件描述符的值，初始为-1
    eventLoop->beforesleep = NULL;  // 事件循环每次处理事件前要调用的函数初始为NULL
    if (aeApiCreate(eventLoop) == -1) goto err;  // 用系统底层API创建事件循环
    /* Events with mask == AE_NONE are not set. So let's initialize the
     * vector with it. */
    /* 初始化已注册的文件事件数组中元素的mask为 AE_NONE */
    for (i = 0; i < setsize; i++)
        eventLoop->events[i].mask = AE_NONE;
    return eventLoop;

/* 创建事件循环出错，释放分配的空间 */
err:
    if (eventLoop) {
        zfree(eventLoop->events);
        zfree(eventLoop->fired);
        zfree(eventLoop);
    }
    return NULL;
}

/* Return the current set size. */
/* 返回事件循环的文件描述符的最大监听数 */
int aeGetSetSize(aeEventLoop *eventLoop) {
    return eventLoop->setsize;
}

/* Resize the maximum set size of the event loop.
 * If the requested set size is smaller than the current set size, but
 * there is already a file descriptor in use that is >= the requested
 * set size minus one, AE_ERR is returned and the operation is not
 * performed at all.
 *
 * Otherwise AE_OK is returned and the operation is successful. */
/* 调整事件循环的文件描述符的最大监听数。
 * 如果传入的参数setsize小于当前setsize，且当前存在一个大于传入的setsize的文件描述符
 * ，函数返回AE_ERR且不会执行任何操作。
 * 
 * 否则返回AE_OK且调整操作执行成功。 */
int aeResizeSetSize(aeEventLoop *eventLoop, int setsize) {
    int i;

    // 入参setsize和当前setsize相同，返回AE_OK但什么也不做
    if (setsize == eventLoop->setsize) return AE_OK;

    // 事件循环当前已经注册的最大文件描述符的值大于入参setsize，返回失败
    if (eventLoop->maxfd >= setsize) return AE_ERR;

    // 调用系统底层API来设置事件循环setsize，失败即返回AE_ERR
    if (aeApiResize(eventLoop,setsize) == -1) return AE_ERR;

    // 重新分配事件循环的已注册的文件事件数组和已被触发的事件数组的空间，并更新setsize
    eventLoop->events = zrealloc(eventLoop->events,sizeof(aeFileEvent)*setsize);
    eventLoop->fired = zrealloc(eventLoop->fired,sizeof(aeFiredEvent)*setsize);
    eventLoop->setsize = setsize;

    /* Make sure that if we created new slots, they are initialized with
     * an AE_NONE mask. */
    /* 把新增加的事件元素的mask初始化为AE_NONE */
    for (i = eventLoop->maxfd+1; i < setsize; i++)
        eventLoop->events[i].mask = AE_NONE;
    return AE_OK;
}

/* 删除一个事件循环 */
void aeDeleteEventLoop(aeEventLoop *eventLoop) {
    aeApiFree(eventLoop);  // 调用相应的系统底层API的释放函数
    zfree(eventLoop->events);
    zfree(eventLoop->fired);
    zfree(eventLoop);
}

/* 停止一个事件循环 */
void aeStop(aeEventLoop *eventLoop) {
    eventLoop->stop = 1;
}

/* 创建文件事件 */
int aeCreateFileEvent(aeEventLoop *eventLoop, int fd, int mask,
        aeFileProc *proc, void *clientData)
{
    // 入参fd大于事件循环支持的文件描述符的最大监听数，返回失败
    if (fd >= eventLoop->setsize) {
        errno = ERANGE;
        return AE_ERR;
    }
    aeFileEvent *fe = &eventLoop->events[fd];  // 从文件事件数组中以fd为下标获取一个文件事件

    // 调用系统底层API增加一个事件，失败即返回AE_ERR
    if (aeApiAddEvent(eventLoop, fd, mask) == -1)
        return AE_ERR;
    fe->mask |= mask;
    if (mask & AE_READABLE) fe->rfileProc = proc;  // 设置读事件处理函数
    if (mask & AE_WRITABLE) fe->wfileProc = proc;  // 设置写事件处理函数
    fe->clientData = clientData;  // 设置传递给读/写事件处理函数的数据
    if (fd > eventLoop->maxfd)  // 更新当前已经注册的最大文件描述符的值
        eventLoop->maxfd = fd;
    return AE_OK;
}

/* 删除文件事件 */
void aeDeleteFileEvent(aeEventLoop *eventLoop, int fd, int mask)
{
    if (fd >= eventLoop->setsize) return;  // 不存在fd大于setsize的文件事件，什么也不做
    aeFileEvent *fe = &eventLoop->events[fd];  // 获取这个文件事件元素
    if (fe->mask == AE_NONE) return;  // 发现这个文件事件元素没有绑定任何文件事件，无需删除

    aeApiDelEvent(eventLoop, fd, mask);  // 调用系统底层API删除事件
    fe->mask = fe->mask & (~mask);
    // 如果fd等于事件循环的maxfd且文件事件元素未被注册时，更新maxfd
    if (fd == eventLoop->maxfd && fe->mask == AE_NONE) {
        /* Update the max fd */
        int j;
        
        // 从后向前遍历文件事件数组，找到最大的已注册的最大文件描述符的值，用它更新maxfd
        for (j = eventLoop->maxfd-1; j >= 0; j--)
            if (eventLoop->events[j].mask != AE_NONE) break;
        eventLoop->maxfd = j;
    }
}

/* 通过文件描述符获取文件事件，返回事件状态 */
int aeGetFileEvents(aeEventLoop *eventLoop, int fd) {
    // 入参fd大于事件循环支持的文件描述符的最大监听数，返回0
    if (fd >= eventLoop->setsize) return 0;
    aeFileEvent *fe = &eventLoop->events[fd];  // 获取这个文件事件元素

    return fe->mask;  // AE_READABLE或AE_WRITABLE
}

/* 获取当前的精确时间，将秒数保存在*seconds，毫秒数保存在*milliseconds */
static void aeGetTime(long *seconds, long *milliseconds)
{
    struct timeval tv;

    gettimeofday(&tv, NULL);  // 获取当前的精确时间
    *seconds = tv.tv_sec;  // 获取秒数
    *milliseconds = tv.tv_usec/1000;  // 获取毫秒数
}

/* 在当前时间基础上加上指定的毫秒数，返回这个新的时间（秒和毫秒） */
static void aeAddMillisecondsToNow(long long milliseconds, long *sec, long *ms) {
    long cur_sec, cur_ms, when_sec, when_ms;

    aeGetTime(&cur_sec, &cur_ms);  // 获取当前时间
    when_sec = cur_sec + milliseconds/1000;  // 设置超时秒数
    when_ms = cur_ms + milliseconds%1000;  // 设置超时毫秒数
    if (when_ms >= 1000) {  // 超时毫秒数进位
        when_sec ++;
        when_ms -= 1000;
    }
    *sec = when_sec;
    *ms = when_ms;
}

/* 创建定时器事件 */
long long aeCreateTimeEvent(aeEventLoop *eventLoop, long long milliseconds,
        aeTimeProc *proc, void *clientData,
        aeEventFinalizerProc *finalizerProc)
{
    long long id = eventLoop->timeEventNextId++;  // 获取事件循环的下一个定时器事件id
    aeTimeEvent *te;

    te = zmalloc(sizeof(*te));  // 为定时器事件分配空间
    if (te == NULL) return AE_ERR;
    te->id = id;  // 设置定时器事件id
    aeAddMillisecondsToNow(milliseconds,&te->when_sec,&te->when_ms);  //设置定时器事件触发时间秒数和毫秒数
    te->timeProc = proc;  // 设置定时器事件处理程序
    te->finalizerProc = finalizerProc;  // 设置时定时器事件的最后处理程序，如果设置了该属性，会在定时器事件被删除时调用
    te->clientData = clientData;  // 传递给定时器事件处理程序的数据
    te->next = eventLoop->timeEventHead;  // 将当前定时器事件头插到已注册的定时器事件数组
    eventLoop->timeEventHead = te;  // 更新已注册的定时器事件数组头部
    return id;  // 返回值为创建的定时器事件id
}

/* 删除指定的定时器事件 */
int aeDeleteTimeEvent(aeEventLoop *eventLoop, long long id)
{
    aeTimeEvent *te = eventLoop->timeEventHead;  // 获取定时器事件数组头节点
    while(te) {  // 遍历定时器事件数组
        if (te->id == id) {
            // 找到指定的节点就设置该定时器事件的id为AE_DELETED_EVENT_ID，表示该事件已被删除
            te->id = AE_DELETED_EVENT_ID;
            return AE_OK;
        }
        te = te->next;
    }
    return AE_ERR; /* NO event with the specified ID found */  // 没有找到指定的定时器事件
}

/* Search the first timer to fire.
 * This operation is useful to know how many time the select can be
 * put in sleep without to delay any event.
 * If there are no timers NULL is returned.
 *
 * Note that's O(N) since time events are unsorted.
 * Possible optimizations (not needed by Redis so far, but...):
 * 1) Insert the event in order, so that the nearest is just the head.
 *    Much better but still insertion or deletion of timers is O(N).
 * 2) Use a skiplist to have this operation as O(1) and insertion as O(log(N)).
 */
/* 查找最先被触发的定时器事件。
 * 通过这个函数的耗时我们可以知道select操作在没有事件需要处理的时候需要多长时间能进入
 * 睡眠状态。
 * 如果没有任何需要触发的定时器事件则返回NUL。
 * 
 * 注意，如果定时器事件是无序的，这个函数的时间复杂度是O(N)。
 * 可以考虑如下优化手段：
 * 1) 保证定时器事件顺序插入，则最近的一个定时器事件就是数组头节点。
 *    这对查找一个定时器事件的开销会小很多，但是对插入和删除的时间复杂度还是O(N)。
 * 2) 使用skiplist来让查找操作的时间复杂度变成O(1)，插入操作为O(log(N))。 */
static aeTimeEvent *aeSearchNearestTimer(aeEventLoop *eventLoop)
{
    aeTimeEvent *te = eventLoop->timeEventHead;  // 获取已注册的定时器事件数组头节点
    aeTimeEvent *nearest = NULL;

    while(te) {  // 遍历已注册的定时器事件数组寻找触发事件距当前时间最近的定时器事件
        if (!nearest || te->when_sec < nearest->when_sec ||
                (te->when_sec == nearest->when_sec &&
                 te->when_ms < nearest->when_ms))
            nearest = te;
        te = te->next;
    }
    return nearest;
}

/* Process time events */
/* 处理定时器事件，返回值为处理的定时器事件总数 */
static int processTimeEvents(aeEventLoop *eventLoop) {
    int processed = 0;  // 一次定时器事件处理过程中所处理的定时器事件总数
    aeTimeEvent *te, *prev;
    long long maxId;
    time_t now = time(NULL);  // 获取系统当前时间

    /* If the system clock is moved to the future, and then set back to the
     * right value, time events may be delayed in a random way. Often this
     * means that scheduled operations will not be performed soon enough.
     *
     * Here we try to detect system clock skews, and force all the time
     * events to be processed ASAP when this happens: the idea is that
     * processing events earlier is less dangerous than delaying them
     * indefinitely, and practice suggests it is. */
    /* 如果系统时钟被改变到了一个未来的时间（比真实的当前时间更超前），需要把它重新设为
     * 正确的值，此时定时器事件有可能被延迟触发。这通常意味着定时任务操作无法被及时处理。
     * 
     * 这里我们尝试检测系统时钟的偏差，如果系统当前时间被设置成比上一次检测到的时间更早，
     * 就强制执行所有时间事：所遵循的原则是尽早处理定时器事件要比延后处理好。 */
    if (now < eventLoop->lastTime) {  // 
        te = eventLoop->timeEventHead;  // 获取定时器事件数组的第一个节点
        while(te) {  // 遍历定时器事件数组，把每个节点的触发秒数都设置为0，相当于强制触发
            te->when_sec = 0;
            te = te->next;
        }
    }
    eventLoop->lastTime = now;  // 更新上一次获取的系统时间

    prev = NULL;
    te = eventLoop->timeEventHead;  // 获取定时器事件数组的第一个节点
    maxId = eventLoop->timeEventNextId-1;  // 获取当前已经注册的最大文件描述符的值
    while(te) {  // 遍历定时器事件数组，检查每个定时器事件是否应该被触发
        long now_sec, now_ms;
        long long id;

        /* Remove events scheduled for deletion. */
        /* 如果某个定时器事件已经被移除 */
        if (te->id == AE_DELETED_EVENT_ID) {
            aeTimeEvent *next = te->next;  // 获取当前节点的后置节点
            if (prev == NULL)
                eventLoop->timeEventHead = te->next;  // 设置事件循环定时器事件数组的头节点为当前节点的后置节点
            else
                prev->next = te->next;  // 更新已注册定时器事件节点的关系（因为移除了中间一个节点）
            if (te->finalizerProc)  // 移除定时器事件前，执行它的finalizerProc（如果有）
                te->finalizerProc(eventLoop, te->clientData);
            zfree(te);  // 释放定时器事件节点
            te = next;  // te指向下一个已注册的定时器事件
            continue;
        }

        /* Make sure we don't process time events created by time events in
         * this iteration. Note that this check is currently useless: we always
         * add new timers on the head, however if we change the implementation
         * detail, this check may be useful again: we keep it here for future
         * defense. */
        /* 确保我们不会处理在这次迭代期间创建的定时器事件。注意这个检查目前意义不大：因为
         * 我们总是将新的定时器事件头插到数组中，然而如果我们改变了实现细节，这个检查就很有用了：
         * 这里我们是考虑到未来的改变而做的防御性编程。 */
        if (te->id > maxId) {  // 如果迭代到的定时器事件是在本次迭代期间被新加入的，直接跳过它
            te = te->next;
            continue;
        }
        aeGetTime(&now_sec, &now_ms);  // 获取当前系统时间
        // 如果当前时间已经超过定时器事件的触发事件，就执行处理
        if (now_sec > te->when_sec ||
            (now_sec == te->when_sec && now_ms >= te->when_ms))
        {
            int retval;

            id = te->id;
            retval = te->timeProc(eventLoop, id, te->clientData);  // 触发定时器事件，执行处理函数
            processed++;
            /* 定时器处理函数如果返回AE_NOMORE(-1)，表示这是一个一次性定时器，执行一次处理程序后即可移除
             * 如果返回一个正数，表示这是一个循环定时器，且这个返回的正数是这个定时器的定时时间（以毫秒表示），
             * 所以下面代码的意思是，如果是一个循环定时器，需要更新它的定时事件，以便下一次能够被触发。 */
            if (retval != AE_NOMORE) {
                aeAddMillisecondsToNow(retval,&te->when_sec,&te->when_ms);
            } else {
                te->id = AE_DELETED_EVENT_ID;
            }
        }
        prev = te;
        te = te->next;
    }
    return processed;
}

/* Process every pending time event, then every pending file event
 * (that may be registered by time event callbacks just processed).
 * Without special flags the function sleeps until some file event
 * fires, or when the next time event occurs (if any).
 *
 * If flags is 0, the function does nothing and returns.
 * if flags has AE_ALL_EVENTS set, all the kind of events are processed.
 * if flags has AE_FILE_EVENTS set, file events are processed.
 * if flags has AE_TIME_EVENTS set, time events are processed.
 * if flags has AE_DONT_WAIT set the function returns ASAP until all
 * the events that's possible to process without to wait are processed.
 *
 * The function returns the number of events processed. */
/* 先处理定时器事件，然后再处理文件事件（可能是被定时器事件回调刚刚注册的）。
 * 如果没有特殊标志的话，这个函数会进入睡眠，直到某些文件事件被触发，或者在下一次
 * 处理定时器事件时（如果有）。 
 * 
 * 如果flags为0，此函数什么也不做并返回。
 * 如果flags被设置为AE_ALL_EVENTS，则所有类型的事件都会被处理。
 * 如果flags被设置为AE_FILE_EVENTS，则文件事件会被处理。
 * 如果flags被设置为AE_TIME_EVENTS，则定时器事件会被处理。
 * 如果flags被设置为AE_DONT_WAIT，则函数会尽快返回，那些无须等待就可以被处理的的事件会被处理。
 * 
 * 函数的返回值为被处理的事件总数（定时器事件数+文件事件数）。 */
int aeProcessEvents(aeEventLoop *eventLoop, int flags)
{
    int processed = 0, numevents;

    /* Nothing to do? return ASAP */
    /* 未设置AE_TIME_EVENTS、AE_FILE_EVENTS或AE_ALL_EVENTS，什么也不做直接返回。  */
    if (!(flags & AE_TIME_EVENTS) && !(flags & AE_FILE_EVENTS)) return 0;

    /* Note that we want call select() even if there are no
     * file events to process as long as we want to process time
     * events, in order to sleep until the next time event is ready
     * to fire. */
    /* 注意，只要我们想处理定时器事件的话，即使没有文件事件需要被处理我们也会调用select()函数。
     * 这是为了让函数睡眠直到下一次定时器事件就绪。 */
    if (eventLoop->maxfd != -1 ||
        ((flags & AE_TIME_EVENTS) && !(flags & AE_DONT_WAIT))) {
        int j;
        aeTimeEvent *shortest = NULL;  // 最先被触发的定时器事件指针
        struct timeval tv, *tvp;
        
        // 处理定时器事件，获取第一个要被触发的定时器事件
        if (flags & AE_TIME_EVENTS && !(flags & AE_DONT_WAIT))
            shortest = aeSearchNearestTimer(eventLoop);
        if (shortest) {
            long now_sec, now_ms;

            aeGetTime(&now_sec, &now_ms);  // 获取当前系统事件
            tvp = &tv;

            /* How many milliseconds we need to wait for the next
             * time event to fire? */
            /* 计算这个最先被触发的定时器事件的触发事件距离当前时间的毫秒数tvp */
            long long ms =
                (shortest->when_sec - now_sec)*1000 +
                shortest->when_ms - now_ms;
            
            /* 判断是否需要立即触发这个定时器事件 */
            if (ms > 0) {
                tvp->tv_sec = ms/1000;
                tvp->tv_usec = (ms % 1000)*1000;
            } else {
                tvp->tv_sec = 0;
                tvp->tv_usec = 0;
            }
        } else {
            /* If we have to check for events but need to return
             * ASAP because of AE_DONT_WAIT we need to set the timeout
             * to zero */
            /* 如果我们因为flags为AE_DONT_WAIT而不得不检查事件，
             * 则需要把超时等待事件设置为0。 */
            if (flags & AE_DONT_WAIT) {  // 
                tv.tv_sec = tv.tv_usec = 0;
                tvp = &tv;
            } else {
                /* Otherwise we can block */
                /* 否则一直等待下去 */
                tvp = NULL; /* wait forever */
            }
        }

        /* 计算出tvp，我们就知道距离下一个定时器事件的触发距当前时间的事件间隔，
         * 在这个间隔事件内，我们可以检测其他事件，比如文件事件。 
         * 
         * 开始监听，将发生状态变化的事件的(fd,mask)复制到
         * eventLoop->fired中，下标从0开始，最后返回状态发生变化的时间的个数。
         * 文件事件的轮询，时间间隔为最近一个定时器的超时时间差tvp。
         * aeApiPoll获取就绪的文件事件数量，有三种方式：
         * 1) select
         * 2) epoll
         * 3) kqueue
         * */ 
        numevents = aeApiPoll(eventLoop, tvp);

        // 对所有已触发的文件事件进行处理
        for (j = 0; j < numevents; j++) {
            aeFileEvent *fe = &eventLoop->events[eventLoop->fired[j].fd];
            int mask = eventLoop->fired[j].mask;
            int fd = eventLoop->fired[j].fd;
            int rfired = 0;

	        /* note the fe->mask & mask & ... code: maybe an already processed
             * event removed an element that fired and we still didn't
             * processed, so we check if the event is still valid. */
            /* 注意fe->mask & mask & ...这样的代码：也许一个已经被处理的事件
             * 移除了一个已经触发但我们还未处理的事件，所以这里需要检查事件是否还是合法的。 */
            if (fe->mask & mask & AE_READABLE) {
                // 读就绪的文件事件，调用它的rfileProc处理
                rfired = 1;
                fe->rfileProc(eventLoop,fd,fe->clientData,mask);
            }
            if (fe->mask & mask & AE_WRITABLE) {
                // 写就绪的文件事件，调用它的wfileProc处理
                if (!rfired || fe->wfileProc != fe->rfileProc)
                    fe->wfileProc(eventLoop,fd,fe->clientData,mask);
            }
            processed++;
        }
    }
    /* Check time events */
    /* 检查定时器事件 */
    if (flags & AE_TIME_EVENTS)
        processed += processTimeEvents(eventLoop);

    return processed; /* return the number of processed file/time events */
}

/* Wait for milliseconds until the given file descriptor becomes
 * writable/readable/exception */
/* 在由fd标识的事件变为可读/可写或引发异常前等待指定的毫秒数，用于同步读/写 */
int aeWait(int fd, int mask, long long milliseconds) {
    struct pollfd pfd;
    int retmask = 0, retval;

    memset(&pfd, 0, sizeof(pfd));
    pfd.fd = fd;
    if (mask & AE_READABLE) pfd.events |= POLLIN;
    if (mask & AE_WRITABLE) pfd.events |= POLLOUT;

    if ((retval = poll(&pfd, 1, milliseconds))== 1) {
        if (pfd.revents & POLLIN) retmask |= AE_READABLE;
        if (pfd.revents & POLLOUT) retmask |= AE_WRITABLE;
	if (pfd.revents & POLLERR) retmask |= AE_WRITABLE;
        if (pfd.revents & POLLHUP) retmask |= AE_WRITABLE;
        return retmask;
    } else {
        return retval;
    }
}

/* 主事件循环函数 */
void aeMain(aeEventLoop *eventLoop) {
    eventLoop->stop = 0;  // 设置事件循环为运行状态
    while (!eventLoop->stop) {
        if (eventLoop->beforesleep != NULL)
            eventLoop->beforesleep(eventLoop);  // 每次进入事件处理过程之前，都要调用事件循环的beforesleep函数
        aeProcessEvents(eventLoop, AE_ALL_EVENTS);  // 进入事件处理过程，默认处理所有事件
    }
}

/* 获取底层I/O具体使用的API名称 */
char *aeGetApiName(void) {
    return aeApiName();
}

/* 设置事件循环中每次进入事件处理过程之前前调用的函数 */
void aeSetBeforeSleepProc(aeEventLoop *eventLoop, aeBeforeSleepProc *beforesleep) {
    eventLoop->beforesleep = beforesleep;
}
