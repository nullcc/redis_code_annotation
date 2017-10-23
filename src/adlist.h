/* adlist.h - A generic doubly linked list implementation
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

/*
  adlist.h - 一个通用的双端链表的实现
*/

#ifndef __ADLIST_H__
#define __ADLIST_H__

/* Node, List, and Iterator are the only data structures used currently. */
/* 双端链表节点数据结构 */
typedef struct listNode {
    struct listNode *prev; // 前一个节点指针
    struct listNode *next; // 后一个节点指针
    void *value;           // 节点的数据域
} listNode;

/* 双端链表迭代器数据结构 */
typedef struct listIter {
    listNode *next;  // 下一个节点指针
    int direction;   // 迭代方向
} listIter;

/* 双端链表数据结构 */
typedef struct list {
    listNode *head;  // 链表头节点指针
    listNode *tail;  // 链表尾节点指针
    void *(*dup)(void *ptr);  // 链表节点复制函数
    void (*free)(void *ptr);  // 链表节点释放函数
    int (*match)(void *ptr, void *key);  // 链表节点比较函数
    unsigned long len;  // 链表长度
} list;

/* Functions implemented as macros */
#define listLength(l) ((l)->len)  //  获取链表长度
#define listFirst(l) ((l)->head)  //  获取链表头节点
#define listLast(l) ((l)->tail)   //  获取链表尾节点
#define listPrevNode(n) ((n)->prev)  //  获取当前节点的前一个节点
#define listNextNode(n) ((n)->next)  //  获取当前节点的后一个节点
#define listNodeValue(n) ((n)->value)  //  获取当前节点的数据

#define listSetDupMethod(l,m) ((l)->dup = (m))  // 设置链表的节点复制函数
#define listSetFreeMethod(l,m) ((l)->free = (m))  // 设置链表的节点数据域释放函数
#define listSetMatchMethod(l,m) ((l)->match = (m))  // 设置链表的节点比较函数

#define listGetDupMethod(l) ((l)->dup)  // 获取链表的节点复制函数
#define listGetFree(l) ((l)->free)  // 获取链表的节点释放函数
#define listGetMatchMethod(l) ((l)->match)  // 获取链表的节点比较函数

/* Prototypes */
list *listCreate(void);  //  创建一个空的链表
void listRelease(list *list);  //  释放一个链表
list *listAddNodeHead(list *list, void *value);  //  为链表添加头节点
list *listAddNodeTail(list *list, void *value);  //  为链表添加尾节点
list *listInsertNode(list *list, listNode *old_node, void *value, int after);  // 为链表插入节点
void listDelNode(list *list, listNode *node);  // 删除指定节点
listIter *listGetIterator(list *list, int direction);  // 获取指定迭代方向的链表迭代器
listNode *listNext(listIter *iter);  // 使用迭代器获取下一个节点
void listReleaseIterator(listIter *iter);  // 释放链表迭代器
list *listDup(list *orig);  // 复制一个链表
listNode *listSearchKey(list *list, void *key);  // 在链表中查找数据域等于key的节点
listNode *listIndex(list *list, long index); // 在链表中查找指定索引的节点
void listRewind(list *list, listIter *li);  // 使迭代器的当前位置回到链表头，正向迭代
void listRewindTail(list *list, listIter *li);  // 使迭代器的当前位置回到链表尾，反向迭代
void listRotate(list *list);  // 移除链表当前的尾节点，并把它设置为头节点

/* Directions for iterators */
/* 链表迭代器方向 */
#define AL_START_HEAD 0  // 从头节点开始迭代
#define AL_START_TAIL 1  // 从尾节点开始迭代

#endif /* __ADLIST_H__ */
