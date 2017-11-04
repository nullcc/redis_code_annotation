/* A simple event-driven programming library. Originally I wrote this code
 * for the Jim's event-loop (Jim is a Tcl interpreter) but later translated
 * it in form of a library for easy reuse.
 *
 * Copyright (c) 2006-2012, Salvatore Sanfilippo <antirez at gmail dot com>
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

/* 一个简单的事件驱动程序库。最初是用于Jim(一个Tcl解释器)的事件循环中，
 * 但后来把它改写成一个代码库以便复用。 */

#ifndef __AE_H__
#define __AE_H__

#include <time.h>

#define AE_OK 0
#define AE_ERR -1

// 事件状态为不可用
#define AE_NONE 0

// 事件可读
#define AE_READABLE 1

// 事件可写
#define AE_WRITABLE 2

// 文件事件
#define AE_FILE_EVENTS 1

// 时间事件
#define AE_TIME_EVENTS 2

// 所有事件类型
#define AE_ALL_EVENTS (AE_FILE_EVENTS|AE_TIME_EVENTS)

// 不等待事件
#define AE_DONT_WAIT 4

#define AE_NOMORE -1
#define AE_DELETED_EVENT_ID -1

/* Macros */
#define AE_NOTUSED(V) ((void) V)

struct aeEventLoop;

/* Types and data structures */
// 文件事件处理函数
typedef void aeFileProc(struct aeEventLoop *eventLoop, int fd, void *clientData, int mask);

// 时间事件处理函数，返回定时的时长
typedef int aeTimeProc(struct aeEventLoop *eventLoop, long long id, void *clientData);

// 事件处理结束时调用的函数
typedef void aeEventFinalizerProc(struct aeEventLoop *eventLoop, void *clientData);

typedef void aeBeforeSleepProc(struct aeEventLoop *eventLoop);

/* File event structure */
/* 文件事件结构体 */
typedef struct aeFileEvent {
    // 读事件或写事件
    int mask; /* one of AE_(READABLE|WRITABLE) */

    // 读事件处理函数
    aeFileProc *rfileProc;

    // 写事件处理函数
    aeFileProc *wfileProc;

    // 传递给上面两个处理函数的数据
    void *clientData;
} aeFileEvent;

/* Time event structure */
/* 时间事件结构体 */
typedef struct aeTimeEvent {
    // 时间事件标识符
    long long id; /* time event identifier. */

    // 超时秒数
    long when_sec; /* seconds */

    // 超时毫秒数
    long when_ms; /* milliseconds */

    // 时间事件处理程序
    aeTimeProc *timeProc;

    // 时间事件的最后处理程序，如果设置了该属性，会在时间事件被删除时调用
    aeEventFinalizerProc *finalizerProc;

    // 传递给时间事件处理程序的数据
    void *clientData;

    // 后置时间事件节点
    struct aeTimeEvent *next;
} aeTimeEvent;

/* A fired event */
/* 已被触发的事件结构体 */
typedef struct aeFiredEvent {
    int fd;
    int mask;
} aeFiredEvent;

/* State of an event based program */
/* 事件循环结构体 */
typedef struct aeEventLoop {
    // 当前已经注册的最大文件描述符的值
    int maxfd;   /* highest file descriptor currently registered */

    // 文件描述符的最大监听数
    int setsize; /* max number of file descriptors tracked */

    // 下一个时间事件id，用于生成时间事件
    long long timeEventNextId;

    // 用于检测系统时间是否发生变化
    time_t lastTime;     /* Used to detect system clock skew */

    // 已注册的文件事件数组
    aeFileEvent *events; /* Registered events */

    // 已被触发的事件数组
    aeFiredEvent *fired; /* Fired events */

    // 已注册的时间事件数组
    aeTimeEvent *timeEventHead;

    // 事件循环停止标志，0表示正在运行，1表示停止
    int stop;

    // 系统底层调用的API使用的数据
    void *apidata; /* This is used for polling API specific data */

    // 事件循环处理事件前调用该函数，如果没有事件则睡眠
    aeBeforeSleepProc *beforesleep;
} aeEventLoop;

/* Prototypes */
// 创建一个事件循环
aeEventLoop *aeCreateEventLoop(int setsize);

// 删除一个事件循环
void aeDeleteEventLoop(aeEventLoop *eventLoop);

// 停止一个事件循环
void aeStop(aeEventLoop *eventLoop);

// 创建文件事件
int aeCreateFileEvent(aeEventLoop *eventLoop, int fd, int mask,
        aeFileProc *proc, void *clientData);

// 删除文件事件
void aeDeleteFileEvent(aeEventLoop *eventLoop, int fd, int mask);

// 通过文件描述符获取文件事件，返回事件状态
int aeGetFileEvents(aeEventLoop *eventLoop, int fd);

// 创建时间事件
long long aeCreateTimeEvent(aeEventLoop *eventLoop, long long milliseconds,
        aeTimeProc *proc, void *clientData,
        aeEventFinalizerProc *finalizerProc);

// 删除时间事件
int aeDeleteTimeEvent(aeEventLoop *eventLoop, long long id);

// 在事件循环中处理事件，在aeMain中被调用
int aeProcessEvents(aeEventLoop *eventLoop, int flags);

// 在由fd标识的事件可读/可写或引发异常前等待指定的毫秒数
int aeWait(int fd, int mask, long long milliseconds);

// 事件循环主函数
void aeMain(aeEventLoop *eventLoop);

// 获取系统底层API的名称
char *aeGetApiName(void);

// 设置每次事件循环前调用的函数
void aeSetBeforeSleepProc(aeEventLoop *eventLoop, aeBeforeSleepProc *beforesleep);

// 获取事件循环的文件描述符的最大监听数
int aeGetSetSize(aeEventLoop *eventLoop);

// 调整事件循环的文件描述符的最大监听数
int aeResizeSetSize(aeEventLoop *eventLoop, int setsize);

#endif
