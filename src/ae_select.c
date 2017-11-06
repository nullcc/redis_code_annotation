/* Select()-based ae.c module.
 *
 * Copyright (c) 2009-2012, Salvatore Sanfilippo <antirez at gmail dot com>
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

/* select()的抽象层，在系统的select()上包装了一层以便可以使用一致的接口调用 */

#include <sys/select.h>
#include <string.h>

typedef struct aeApiState {
    // rfds: 可读文件描述符集，wfds: 可写文件描述符集
    fd_set rfds, wfds;
    /* We need to have a copy of the fd sets as it's not safe to reuse
     * FD sets after select(). */
    /* 由于select()不是线程安全的，调用select()时，文件描述符集可能被其他线程改变，
     * 所以我们必须保存一份文件描述符的拷贝。 */
    fd_set _rfds, _wfds;
} aeApiState;

/* 底层事件API的统一接口 */
static int aeApiCreate(aeEventLoop *eventLoop) {
    aeApiState *state = zmalloc(sizeof(aeApiState));

    if (!state) return -1;
    FD_ZERO(&state->rfds);  // 初始化可读文件描述符集
    FD_ZERO(&state->wfds);  // 初始化可写文件描述符集
    eventLoop->apidata = state;  // 初始化系统底层调用的API使用的数据
    return 0;
}

/* 调整事件循环文件描述符的最大监听数 */
static int aeApiResize(aeEventLoop *eventLoop, int setsize) {
    /* Just ensure we have enough room in the fd_set type. */
    /* select中默认定义的FD_SETSIZE为1024（可以通过修改宏重新编译内核来修改这个值）
     * 实际上这个函数在这里只是告诉你当前系统能不能支持入参setsize数量的文件描述符监听数。
     * 能支持则返回0，否则返回-1。 */
    if (setsize >= FD_SETSIZE) return -1;
    return 0;
}

/* 释放事件循环系统底层调用的API使用的数据 */
static void aeApiFree(aeEventLoop *eventLoop) {
    zfree(eventLoop->apidata);
}

/* 在事件循环中增加一个文件描述符监听 */
static int aeApiAddEvent(aeEventLoop *eventLoop, int fd, int mask) {
    aeApiState *state = eventLoop->apidata;  // 获取select的状态数据

    // 判断这个文件描述符是读就绪事件还是写就绪事件，并在select的状态数据中设置相应的fd和事件类型
    if (mask & AE_READABLE) FD_SET(fd,&state->rfds);
    if (mask & AE_WRITABLE) FD_SET(fd,&state->wfds);
    return 0;
}

/* 在事件循环中删除一个文件描述符监听 */
static void aeApiDelEvent(aeEventLoop *eventLoop, int fd, int mask) {
    aeApiState *state = eventLoop->apidata;  // 获取select的状态数据

    // 判断这个文件描述符是读就绪事件还是写就绪事件，并做清除处理
    if (mask & AE_READABLE) FD_CLR(fd,&state->rfds);
    if (mask & AE_WRITABLE) FD_CLR(fd,&state->wfds);
}

/* 调用底层API */
static int aeApiPoll(aeEventLoop *eventLoop, struct timeval *tvp) {
    aeApiState *state = eventLoop->apidata;  // 获取select的状态数据
    int retval, j, numevents = 0;

    // 先将可读文件描述符集和可写文件描述符集做一次拷贝
    memcpy(&state->_rfds,&state->rfds,sizeof(fd_set));
    memcpy(&state->_wfds,&state->wfds,sizeof(fd_set));

    /* select()的原型：
     * int select (int n, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout);
     * 参数说明：
     * 1) n: 是指集合中所有文件描述符的范围，即所有文件描述符的最大值加1
     * 2) readfds: 可读文件描述符集
     * 3) writefds: 可写文件描述符集
     * 4) exceptfds: 异常文件描述符集
     * 5) timeout: select超时时间，超过该时间如果还没有事件发生就返回，如果是同步阻塞则设为NULL
     * 返回值：
     * 1) 超时情况下返回0，说明在指定事件内没有I/O事件就绪
     * 2) 返回处于就绪状态且已经包含在fd_set结构中的文件描述符总数
     * 3) 返回SOCKET_ERROR，表示需要将所有文件描述符清零
     * */
    retval = select(eventLoop->maxfd+1,
                &state->_rfds,&state->_wfds,NULL,tvp);
    if (retval > 0) {  // 表示有I/O事件就绪
        /* 遍历整个文件事件数组，依次检查每个文件事件是否处于就绪状态，
         * 如果是就绪状态，将其加入到事件循环的就绪事件数组中。 */
        for (j = 0; j <= eventLoop->maxfd; j++) {
            int mask = 0;
            aeFileEvent *fe = &eventLoop->events[j];  // 获取每个文件事件

            /* 这里判断文件描述符是否已经包含在fd_set中需要使用之前的文件描述符集拷贝
             * 来判断：FD_ISSET(j,&state->_rfds)，这是为了防止在select函数执行
             * 期间其他线程操作了文件描述符集。 */
            if (fe->mask == AE_NONE) continue;
            if (fe->mask & AE_READABLE && FD_ISSET(j,&state->_rfds))
                mask |= AE_READABLE;
            if (fe->mask & AE_WRITABLE && FD_ISSET(j,&state->_wfds))
                mask |= AE_WRITABLE;
            eventLoop->fired[numevents].fd = j;
            eventLoop->fired[numevents].mask = mask;
            numevents++;
        }
    }
    return numevents;
}

/* 获取底层I/O具体使用的API名称，这里是select */
static char *aeApiName(void) {
    return "select";
}
