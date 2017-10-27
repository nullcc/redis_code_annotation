/* Hash Tables Implementation.
 *
 * This file implements in memory hash tables with insert/del/replace/find/
 * get-random-element operations. Hash tables will auto resize if needed
 * tables of power of two in size are used, collisions are handled by
 * chaining. See the source code for more information... :)
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

/* 哈希表实现 */

#include "fmacros.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <limits.h>
#include <sys/time.h>
#include <ctype.h>

#include "dict.h"
#include "zmalloc.h"
#include "redisassert.h"

/* Using dictEnableResize() / dictDisableResize() we make possible to
 * enable/disable resizing of the hash table as needed. This is very important
 * for Redis, as we use copy-on-write and don't want to move too much memory
 * around when there is a child performing saving operations.
 *
 * Note that even when dict_can_resize is set to 0, not all resizes are
 * prevented: a hash table is still allowed to grow if the ratio between
 * the number of elements and the buckets > dict_force_resize_ratio. */

/* dictEnableResize()和dictDisableResize()函数允许我们在需要时启用/禁用哈希表的重新规划空间的
 * 功能。这对Redis来说非常重要，因为我们使用写时复制且不希望在有子进程进行保存操作时移动太多内存
 * 中的数据。
 * 
 * 需要注意的是即使dict_can_resize被设置为0，在某些情况下也会触发字典重新规划空间的操作：
 * 当一个哈希表中的元素个数和散列数组（桶）的比例大于dict_force_resize_ratio时，
 * 触发字典重新规划空间的操作。 */
static int dict_can_resize = 1;  // 字典重新规划空间开关
static unsigned int dict_force_resize_ratio = 5;  // 字典被强制进行重新规划空间时的（元素个数/桶大小）比例

/* -------------------------- private prototypes ---------------------------- */

static int _dictExpandIfNeeded(dict *ht);  // 判断字典是否需要扩容
static unsigned long _dictNextPower(unsigned long size);  // 字典扩容的大小（字典的容量都是2的整数次方大小），该函数返回大于或等于size的2的整数次方的数字最小的那个
static int _dictKeyIndex(dict *ht, const void *key);  // 返回指定key在散列数组中的索引值
static int _dictInit(dict *ht, dictType *type, void *privDataPtr);  // 初始化一个字典

/* -------------------------- hash functions -------------------------------- */

/* Thomas Wang's 32 bit Mix Function */
/* Thomas Wang's 32 bit Mix哈希算法，对一个无符号整型数进行一系列的移位运算，效率较高 */
unsigned int dictIntHashFunction(unsigned int key)
{
    key += ~(key << 15);
    key ^=  (key >> 10);
    key +=  (key << 3);
    key ^=  (key >> 6);
    key += ~(key << 11);
    key ^=  (key >> 16);
    return key;
}

static uint32_t dict_hash_function_seed = 5381;  // 哈希种子一枚

/* 设置新的哈希种子 */
void dictSetHashFunctionSeed(uint32_t seed) {
    dict_hash_function_seed = seed;
}

/* 获取当前哈希种子 */
uint32_t dictGetHashFunctionSeed(void) {
    return dict_hash_function_seed;
}

/* MurmurHash2, by Austin Appleby
 * Note - This code makes a few assumptions about how your machine behaves -
 * 1. We can read a 4-byte value from any address without crashing
 * 2. sizeof(int) == 4
 *
 * And it has a few limitations -
 *
 * 1. It will not work incrementally.
 * 2. It will not produce the same results on little-endian and big-endian
 *    machines.
 */

/* Austin Appleby的MurmurHash2算法
 * 注意：这段代码对你的机器的行为做了一些假设：
 * 1. 可以从任何内存地址读取一个4字节的数据而不会崩溃
 * 2. sizeof(int) == 4
 *
 * 还有一些限制：
 *
 * 1. 它无法增量地工作。
 * 2. 它在小端和大端机器上的结果不同。
 */
unsigned int dictGenHashFunction(const void *key, int len) {
    /* 'm' and 'r' are mixing constants generated offline.
     They're not really 'magic', they just happen to work well.  */
    uint32_t seed = dict_hash_function_seed;
    const uint32_t m = 0x5bd1e995;
    const int r = 24;

    /* Initialize the hash to a 'random' value */
    uint32_t h = seed ^ len;

    /* Mix 4 bytes at a time into the hash */
    const unsigned char *data = (const unsigned char *)key;

    while(len >= 4) {
        uint32_t k = *(uint32_t*)data;

        k *= m;
        k ^= k >> r;
        k *= m;

        h *= m;
        h ^= k;

        data += 4;
        len -= 4;
    }

    /* Handle the last few bytes of the input array  */
    switch(len) {
    case 3: h ^= data[2] << 16;
    case 2: h ^= data[1] << 8;
    case 1: h ^= data[0]; h *= m;
    };

    /* Do a few final mixes of the hash to ensure the last few
     * bytes are well-incorporated. */
    h ^= h >> 13;
    h *= m;
    h ^= h >> 15;

    return (unsigned int)h;
}

/* And a case insensitive hash function (based on djb hash) */

/* 一个对大小写不敏感的哈希函数（基于djb哈希算法） */
unsigned int dictGenCaseHashFunction(const unsigned char *buf, int len) {
    unsigned int hash = (unsigned int)dict_hash_function_seed;

    while (len--)
        hash = ((hash << 5) + hash) + (tolower(*buf++)); /* hash * 33 + c */
    return hash;
}

/* ----------------------------- API implementation ------------------------- */

/* Reset a hash table already initialized with ht_init().
 * NOTE: This function should only be called by ht_destroy(). */

/* 重置一个已经被ht_init()函数初始化过的哈希表
 * 注意：这个函数只应该被ht_destroy()函数调用。 */
static void _dictReset(dictht *ht)
{
    ht->table = NULL;
    ht->size = 0;
    ht->sizemask = 0;
    ht->used = 0;
}

/* Create a new hash table */

/* 创建一个哈希表 */
dict *dictCreate(dictType *type,
        void *privDataPtr)
{
    dict *d = zmalloc(sizeof(*d));  // 分配内存

    _dictInit(d,type,privDataPtr);  // 初始化哈希表
    return d;
}

/* Initialize the hash table */

/* 初始化哈希表 */
int _dictInit(dict *d, dictType *type,
        void *privDataPtr)
{
    _dictReset(&d->ht[0]);  // 初始化第一个哈希表
    _dictReset(&d->ht[1]);  // 初始化第二个哈希表
    d->type = type;         // 初始化字典类型
    d->privdata = privDataPtr;  // 初始化私有数据
    d->rehashidx = -1;  // 初始化rehash索引
    d->iterators = 0;  // 初始化字典迭代器
    return DICT_OK;
}

/* Resize the table to the minimal size that contains all the elements,
 * but with the invariant of a USED/BUCKETS ratio near to <= 1 */

/* 重新计算并设置字典的哈希数组大小，调整到能包含所有元素的最小大小，
 * 保持已使用节点数量/桶大小的比率接近<=1 */
int dictResize(dict *d)
{
    int minimal;

    if (!dict_can_resize || dictIsRehashing(d)) return DICT_ERR;  // 禁用字典resize或当前字典正在rehash时返回错误
    minimal = d->ht[0].used;  // 已使用节点的数量
    if (minimal < DICT_HT_INITIAL_SIZE)  // 已使用节点数量小于散列数组的初始大小时，新空间大小设置为散列数组的初始大小
        minimal = DICT_HT_INITIAL_SIZE;
    return dictExpand(d, minimal);  // 扩充字典大小
}

/* Expand or create the hash table */

/* 扩充或创建哈希表 */
int dictExpand(dict *d, unsigned long size)
{
    dictht n; /* the new hash table */
    unsigned long realsize = _dictNextPower(size);  // 计算一个合适的哈希表大小，大小为2的整数次方

    /* the size is invalid if it is smaller than the number of
     * elements already inside the hash table */
    /* 当字典正在进行rehash或字典哈希表中已使用节点数量大于size都返回错误 */
    if (dictIsRehashing(d) || d->ht[0].used > size)
        return DICT_ERR;

    /* Rehashing to the same table size is not useful. */
    /* 新的空间大小和当前的相同，没必要进行rehash */
    if (realsize == d->ht[0].size) return DICT_ERR;

    /* Allocate the new hash table and initialize all pointers to NULL */
    /* 为新的哈希表分配空间然后初始化它的所有指针为NULL */
    n.size = realsize;  // 新哈希表散列数组长度
    n.sizemask = realsize-1;  // 新哈希表散列数组长度掩码
    n.table = zcalloc(realsize*sizeof(dictEntry*));  // 新哈希表散列数组空间分配
    n.used = 0;  // 新哈希表已使用节点数量

    /* Is this the first initialization? If so it's not really a rehashing
     * we just set the first hash table so that it can accept keys. */
    /* 如果d还未被初始化，就不需要rehash，直接把n赋值给字典的第一个哈希表。 */
    if (d->ht[0].table == NULL) {
        d->ht[0] = n;
        return DICT_OK;
    }

    /* Prepare a second hash table for incremental rehashing */
    /* 准备第二个哈希表用来进行增量rehash */
    d->ht[1] = n;  // 1号哈希表现在是被扩展了，数据会从0号哈希表被移动到1号哈希表
    d->rehashidx = 0;
    return DICT_OK;
}

/* Performs N steps of incremental rehashing. Returns 1 if there are still
 * keys to move from the old to the new hash table, otherwise 0 is returned.
 *
 * Note that a rehashing step consists in moving a bucket (that may have more
 * than one key as we use chaining) from the old to the new hash table, however
 * since part of the hash table may be composed of empty spaces, it is not
 * guaranteed that this function will rehash even a single bucket, since it
 * will visit at max N*10 empty buckets in total, otherwise the amount of
 * work it does would be unbound and the function may block for a long time. */

/* 分N步进行增量rehash。当旧哈希表中还有key没移动到新哈希表时，函数返回1，否则返回0。
 *
 * 一次rehash过程包含把一个桶从旧哈希表移动到新哈希表（由于我们在同一个桶中使用链表形式保存key-value对，
 * 所以一个桶中可能有一个以上的key需要移动）。然而由于哈希表中可能有一部分是空的，并不能保证
 * 每一步能对至少一个桶进行rehash，因此我们规定一步中最多只能访问N*10个空桶，否则这么大量的工作
 * 可能会造成一段长时间的阻塞。 */
int dictRehash(dict *d, int n) {
    int empty_visits = n*10; /* Max number of empty buckets to visit. */  // 一步rehash中最多访问的空桶的次数
    if (!dictIsRehashing(d)) return 0;

    while(n-- && d->ht[0].used != 0) {  // 分n步进行rehash
        dictEntry *de, *nextde;

        /* Note that rehashidx can't overflow as we are sure there are more
         * elements because ht[0].used != 0 */
        /* 注意rehashidx不能越界，因为由于ht[0].used != 0，我们知道还有元素没有被rehash */
        assert(d->ht[0].size > (unsigned long)d->rehashidx);
        while(d->ht[0].table[d->rehashidx] == NULL) {  // 遇到空桶了
            d->rehashidx++;  // rehashidx移动到下一个桶
            if (--empty_visits == 0) return 1;  // 当前一次rehash过程遇到的空桶数量等于n*10则直接结束
        }
        de = d->ht[0].table[d->rehashidx];  // 获得当前桶中第一个key-value对的指针
        /* Move all the keys in this bucket from the old to the new hash HT */
        /* 把当前桶中所有的key从旧哈希表移动到新哈希表 */
        while(de) {  // 遍历桶中的key-value对链表
            unsigned int h;

            nextde = de->next;  // 链表中下一个key-value对的指针
            /* Get the index in the new hash table */
            /* 获取key的哈希值并计算其在新哈希表中桶的索引值 */
            h = dictHashKey(d, de->key) & d->ht[1].sizemask;
            de->next = d->ht[1].table[h];  // 设置当前key-value对的next指针指向1号哈希表相应桶得地址
            d->ht[1].table[h] = de;  // 将key-value对移动到1号哈希表中（rehash后的新表不会出现一个桶中有多个元素的情况）
            d->ht[0].used--;  // 扣减0号哈希表已使用节点的数量
            d->ht[1].used++;  // 增加1号哈希表已使用节点的数量
            de = nextde;  // 移动当前key-value对得指针到链表的下一个元素
        }
        d->ht[0].table[d->rehashidx] = NULL;  // 当把一个桶中所有得key-value对都rehash以后，设置当前桶指向NULL
        d->rehashidx++;
    }

    /* Check if we already rehashed the whole table... */
    /* 检查我们已经对表中所有元素完成rehash操作 */
    if (d->ht[0].used == 0) {
        zfree(d->ht[0].table);  // 释放0号哈希表的哈希数组
        d->ht[0] = d->ht[1];  // 把1号哈希表置为0号
        _dictReset(&d->ht[1]);  // 重置1号哈希表
        d->rehashidx = -1;
        return 0;  // 完成整个增量式rehash
    }

    /* More to rehash... */
    return 1;  // 还有元素没有被rehash
}

/* 获取当前时间戳，单位毫秒 */
long long timeInMilliseconds(void) {
    struct timeval tv;

    gettimeofday(&tv,NULL);
    return (((long long)tv.tv_sec)*1000)+(tv.tv_usec/1000);
}

/* Rehash for an amount of time between ms milliseconds and ms+1 milliseconds */

/*  在ms时间内rehash，超过则停止 */
int dictRehashMilliseconds(dict *d, int ms) {
    long long start = timeInMilliseconds();  // 起始时间
    int rehashes = 0; // rehash次数

    while(dictRehash(d,100)) {  // 分100rehash
        rehashes += 100;
        if (timeInMilliseconds()-start > ms) break;  // 超过规定时间则停止rehash
    }
    return rehashes;
}

/* This function performs just a step of rehashing, and only if there are
 * no safe iterators bound to our hash table. When we have iterators in the
 * middle of a rehashing we can't mess with the two hash tables otherwise
 * some element can be missed or duplicated.
 *
 * This function is called by common lookup or update operations in the
 * dictionary so that the hash table automatically migrates from H1 to H2
 * while it is actively used. */

/* 这个函数会执行一步的rehash操作，只有在哈希表没有安全迭代器时才会使用。
 * 当在rehash过程中使用迭代器时，我们不能操作两个哈希表，否则有些元素会被遗漏或者被重复rehash。
 *
 * 在字典的键查找或更新操作过程中，如果符合rehash条件，就会触发一次rehash，每次执行一步。 */
static void _dictRehashStep(dict *d) {
    if (d->iterators == 0) dictRehash(d,1);  // 没有迭代器在使用时，执行一次一步的rehash
}

/* Add an element to the target hash table */

/* 向目标哈希表添加一个key-value对*/
int dictAdd(dict *d, void *key, void *val)
{
    dictEntry *entry = dictAddRaw(d,key);  // 先只添加key

    if (!entry) return DICT_ERR;
    dictSetVal(d, entry, val);  // 设置value
    return DICT_OK;
}

/* Low level add. This function adds the entry but instead of setting
 * a value returns the dictEntry structure to the user, that will make
 * sure to fill the value field as he wishes.
 *
 * This function is also directly exposed to the user API to be called
 * mainly in order to store non-pointers inside the hash value, example:
 *
 * entry = dictAddRaw(dict,mykey);
 * if (entry != NULL) dictSetSignedIntegerVal(entry,1000);
 *
 * Return values:
 *
 * If key already exists NULL is returned.
 * If key was added, the hash entry is returned to be manipulated by the caller.
 */

/* 低级别的字典添加操作。此函数添加一个ket-value结构但并不设置value，然后返回这个结构给用户，
 * 这可以确保用户按照自己的意愿设置value。
 *
 * 此函数还作为用户级别的API直接暴露出来，这主要是为了在散列值内存储非指针类型的数据，比如：
 *
 * entry = dictAddRaw(dict,mykey);
 * if (entry != NULL) dictSetSignedIntegerVal(entry,1000);
 *
 * 返回值：
 *
 * 如果key已经存在返回NULL。
 * 如果成功添加了key，函数返回hash结构供用户操作。
 */
dictEntry *dictAddRaw(dict *d, void *key)
{
    int index;
    dictEntry *entry;
    dictht *ht;

    if (dictIsRehashing(d)) _dictRehashStep(d);  // 字典正在进行rehash时，执行一步增量式rehash过程

    /* Get the index of the new element, or -1 if
     * the element already exists. */
    /* 获取key对应的索引值，当key已经存在时_dictKeyIndex函数返回-1，添加失败 */
    if ((index = _dictKeyIndex(d, key)) == -1)
        return NULL;

    /* Allocate the memory and store the new entry.
     * Insert the element in top, with the assumption that in a database
     * system it is more likely that recently added entries are accessed
     * more frequently. */
    /* 为新的key-value对分配内存
     * 把新添加的元素放在顶部，这很类似数据库的做法：最近添加的元素有更高的访问频率。 */
    ht = dictIsRehashing(d) ? &d->ht[1] : &d->ht[0];  // 如果字典正在rehash，直接把新元素添加到1号哈希表中
    entry = zmalloc(sizeof(*entry));  // 分配内存
    entry->next = ht->table[index];  
    ht->table[index] = entry;  // 把新元素插入哈希表相应索引下链表的头部
    ht->used++;  // 增加哈希表已使用元素数量

    /* Set the hash entry fields. */
    dictSetKey(d, entry, key);  // 设置key
    return entry;
}

/* Add an element, discarding the old if the key already exists.
 * Return 1 if the key was added from scratch, 0 if there was already an
 * element with such key and dictReplace() just performed a value update
 * operation. */

/* 向字典添加一个元素，不管指定的key是否存在。
 * key不存在时，添加后函数返回1，否则返回0，dictReplace()函数此时只更新相应的value。 */
int dictReplace(dict *d, void *key, void *val)
{
    dictEntry *entry, auxentry;

    /* Try to add the element. If the key
     * does not exists dictAdd will suceed. */
    /* 尝试添加元素，如果key不存在dictAdd()函数调用成功，并返回1。 */
    if (dictAdd(d, key, val) == DICT_OK)
        return 1;
    /* It already exists, get the entry */
    /* key已经存在，获取key-value对 */
    entry = dictFind(d, key);
    /* Set the new value and free the old one. Note that it is important
     * to do that in this order, as the value may just be exactly the same
     * as the previous one. In this context, think to reference counting,
     * you want to increment (set), and then decrement (free), and not the
     * reverse. */
    /* 对key-value对设置新value并释放旧value的内存。需要注意的是这个先设置再释放的顺序很重要，
     * 因为新value很有可能和旧value完全是同一个东西。考虑引用记数的情况，你应该先增加引用记数（设置新value），
     * 再减少引用记数（释放旧value），这个顺序不能被颠倒。 */
    auxentry = *entry;
    dictSetVal(d, entry, val);
    dictFreeVal(d, &auxentry);
    return 0;
}

/* dictReplaceRaw() is simply a version of dictAddRaw() that always
 * returns the hash entry of the specified key, even if the key already
 * exists and can't be added (in that case the entry of the already
 * existing key is returned.)
 *
 * See dictAddRaw() for more information. */

/* dictReplaceRaw()是dictAddRaw()的简化版本，它总是返回指定key的key-value对结构，
 * 即使key已经存在不能被添加时（这种情况下会直接返回这个已经存在的key的key-value对结构）。
 *
 * 查看dictAddRaw()获得更多信息。 */
dictEntry *dictReplaceRaw(dict *d, void *key) {
    dictEntry *entry = dictFind(d,key);

    return entry ? entry : dictAddRaw(d,key);  // key存在时返回它的key-value对结构，否则调用dictAddRaw
}

/* Search and remove an element */

/* 查找并移除一个元素 */
static int dictGenericDelete(dict *d, const void *key, int nofree)
{
    unsigned int h, idx;
    dictEntry *he, *prevHe;
    int table;

    if (d->ht[0].size == 0) return DICT_ERR; /* d->ht[0].table is NULL */  // 字典0号哈希表大小为0时直接返回错误
    if (dictIsRehashing(d)) _dictRehashStep(d);  // 如果字典d正在rehash，执行一步的rehash过程
    h = dictHashKey(d, key);  // 计算key的hash值

    for (table = 0; table <= 1; table++) {  // 遍历0号和1号哈希表移除元素
        idx = h & d->ht[table].sizemask;  // 获取key所在的哈希数组索引值
        he = d->ht[table].table[idx];  // 获取idx索引位置指向的第一个entry
        prevHe = NULL;
        while(he) {  // 遍历idx索引位置上的entry链表，移除key为指定值的元素
            if (key==he->key || dictCompareKeys(d, key, he->key)) {  // 找到该entry
                /* Unlink the element from the list */
                if (prevHe)
                    prevHe->next = he->next;
                else
                    d->ht[table].table[idx] = he->next;
                if (!nofree) {  // nofree标志表示是否需要释放这个entry的key和value
                    dictFreeKey(d, he);  // 释放key
                    dictFreeVal(d, he);  // 释放value
                }
                zfree(he);  // 释放enrty
                d->ht[table].used--;  // 减少已存在的key数量
                return DICT_OK;
            }
            prevHe = he;  // 没找到则向后查找
            he = he->next;
        }
        /* 如果字典不是正在进行rehash，直接跳过对1号哈希表的搜索，因为只有在rehash过程中，
         * 添加的key-value才会直接写到1号哈希表中，其他时候都是直接写0号哈希表。 */
        if (!dictIsRehashing(d)) break;  
    }
    return DICT_ERR; /* not found */
}

/* 移除字典中的指定key，并释放相应的key和value */
int dictDelete(dict *ht, const void *key) {
    return dictGenericDelete(ht,key,0);
}

/* 移除字典中的指定key，不释放相应的key和value */
int dictDeleteNoFree(dict *ht, const void *key) {
    return dictGenericDelete(ht,key,1);
}

/* Destroy an entire dictionary */

/* 销毁整个字典 */
int _dictClear(dict *d, dictht *ht, void(callback)(void *)) {
    unsigned long i;

    /* Free all the elements */
    for (i = 0; i < ht->size && ht->used > 0; i++) {  // 遍历整个哈希表
        dictEntry *he, *nextHe;

        if (callback && (i & 65535) == 0) callback(d->privdata);  // 销毁私有数据

        if ((he = ht->table[i]) == NULL) continue;  // 跳过没有数据的桶
        while(he) {  // 遍历桶中的entry销毁数据
            nextHe = he->next;
            dictFreeKey(d, he);
            dictFreeVal(d, he);
            zfree(he);
            ht->used--;  // 递减哈希表中的元素数量
            he = nextHe;
        }
    }
    /* Free the table and the allocated cache structure */
    /* 释放哈希表的哈希数组 */
    zfree(ht->table);
    /* Re-initialize the table */
    /* 重置整个哈希表 */
    _dictReset(ht);
    return DICT_OK; /* never fails */
}

/* Clear & Release the hash table */

/* 清空并释放字典 */
void dictRelease(dict *d)
{
    _dictClear(d,&d->ht[0],NULL);
    _dictClear(d,&d->ht[1],NULL);
    zfree(d);
}

/* 查找字典key */
dictEntry *dictFind(dict *d, const void *key)
{
    dictEntry *he;
    unsigned int h, idx, table;

    if (d->ht[0].used + d->ht[1].used == 0) return NULL; /* dict is empty */  // 0号和1号哈希表都没有元素，返回NULL
    if (dictIsRehashing(d)) _dictRehashStep(d);  // 如果字典正在rehash，执行一次一步rehash
    h = dictHashKey(d, key);  // 计算key的哈希值
    for (table = 0; table <= 1; table++) {  // 在0号和1号哈希表种查找
        idx = h & d->ht[table].sizemask;  // 计算索引值
        he = d->ht[table].table[idx];  // 获取哈希数组相应索引的第一个元素
        while(he) {  // 遍历元素链表，查找key
            if (key==he->key || dictCompareKeys(d, key, he->key))
                return he;
            he = he->next;
        }
        if (!dictIsRehashing(d)) return NULL;  // 如果字典不是正在进行rehash，直接跳过对1号哈希表的搜索，并返回NULL
    }
    return NULL;
}

/* 获取字典中指定key的value */
void *dictFetchValue(dict *d, const void *key) {
    dictEntry *he;

    he = dictFind(d,key);  // 用key找到key-value entry
    return he ? dictGetVal(he) : NULL;
}

/* A fingerprint is a 64 bit number that represents the state of the dictionary
 * at a given time, it's just a few dict properties xored together.
 * When an unsafe iterator is initialized, we get the dict fingerprint, and check
 * the fingerprint again when the iterator is released.
 * If the two fingerprints are different it means that the user of the iterator
 * performed forbidden operations against the dictionary while iterating. */

/* 字典的指纹是一个64位的数字，它表示字典在一个给定时间点的状态，其实就是一些字典熟悉的异或结果。
 * 当初始化了一个不安全的迭代器时，我们可以拿到字典的指纹，并且在迭代器被释放时检查这个指纹。
 * 如果两个指纹不同就表示迭代器的所有者在迭代过程中进行了被禁止的操作。 */
long long dictFingerprint(dict *d) {
    long long integers[6], hash = 0;
    int j;

    integers[0] = (long) d->ht[0].table;  // 0号哈希表
    integers[1] = d->ht[0].size;          // 0号哈希表的大小
    integers[2] = d->ht[0].used;          // 0号哈希表中元素数量
    integers[3] = (long) d->ht[1].table;  // 1号哈希表
    integers[4] = d->ht[1].size;          // 1号哈希表的大小
    integers[5] = d->ht[1].used;          // 1号哈希表中元素数量

    /* We hash N integers by summing every successive integer with the integer
     * hashing of the previous sum. Basically:
     *
     * Result = hash(hash(hash(int1)+int2)+int3) ...
     *
     * This way the same set of integers in a different order will (likely) hash
     * to a different number. */

    /* 我们对N个整形数计算hash值的方法是连续地把上一个数字的hash值和下一个数相加，形成一个新值，
     * 再对这个新值计算hash值，以此类推。像这样：
     *
     * Result = hash(hash(hash(int1)+int2)+int3) ...
     *
     * 用这种方式计算一组整型的hash值时，不同的计算顺序会有不同的结果。 */
    for (j = 0; j < 6; j++) {
        hash += integers[j];
        /* For the hashing step we use Tomas Wang's 64 bit integer hash. */
        /* 使用Tomas Wang's 64 bit integer哈希算法 */
        hash = (~hash) + (hash << 21); // hash = (hash << 21) - hash - 1;
        hash = hash ^ (hash >> 24);
        hash = (hash + (hash << 3)) + (hash << 8); // hash * 265
        hash = hash ^ (hash >> 14);
        hash = (hash + (hash << 2)) + (hash << 4); // hash * 21
        hash = hash ^ (hash >> 28);
        hash = hash + (hash << 31);
    }
    return hash;
}

/* 获取一个字典的不安全迭代器 */
dictIterator *dictGetIterator(dict *d)
{
    dictIterator *iter = zmalloc(sizeof(*iter));  // 为迭代器分配空间

    iter->d = d;
    iter->table = 0;  // 迭代的是0号哈希表
    iter->index = -1;
    iter->safe = 0;  // 0表示不安全
    iter->entry = NULL;
    iter->nextEntry = NULL;
    return iter;
}

/* 获取一个字典的安全迭代器 */
dictIterator *dictGetSafeIterator(dict *d) {
    dictIterator *i = dictGetIterator(d);

    i->safe = 1;  // 1表示安全
    return i;
}

/* 获取迭代器的下一个元素 */
dictEntry *dictNext(dictIterator *iter)
{
    while (1) {
        if (iter->entry == NULL) {  // 当前桶的entry链表已经迭代完毕
            dictht *ht = &iter->d->ht[iter->table];  // 获取迭代器的哈希表指针
            if (iter->index == -1 && iter->table == 0) {  // 刚开始迭代0号哈希表时
                if (iter->safe)
                    iter->d->iterators++;  // 如果是安全的迭代器，就将当前使用的迭代器数量+1
                else
                    iter->fingerprint = dictFingerprint(iter->d);  // 不安全迭代器需要设置字典指纹
            }
            iter->index++;  // 移动到下一个桶
            if (iter->index >= (long) ht->size) {  // 迭代器的当前索引值超过哈希表大小
                if (dictIsRehashing(iter->d) && iter->table == 0) {  // 字典正在rehash且当前是0号哈希表时
                    iter->table++;  // 开始迭代1号哈希表
                    iter->index = 0;  // 设置开始迭代索引为0
                    ht = &iter->d->ht[1];  // 更新哈希表指针
                } else {
                    break;  // 如果字典不在rehash且迭代结束，就跳出并返回NULL，表示没有下一个元素了
                }
            }
            iter->entry = ht->table[iter->index];  // 获取当前桶上的第一个元素
        } else {
            iter->entry = iter->nextEntry;  // 获取当前桶中entry的下一个entry
        }
        if (iter->entry) {
            /* We need to save the 'next' here, the iterator user
             * may delete the entry we are returning. */
            /* 保存nextEntry指针，因为迭代器用户有可能会删除当前entry */
            iter->nextEntry = iter->entry->next;
            return iter->entry;
        }
    }
    return NULL;
}

/* 释放字典迭代器 */
void dictReleaseIterator(dictIterator *iter)
{
    if (!(iter->index == -1 && iter->table == 0)) {  // 如果当前迭代器时初始化状态且是0号哈希表
        if (iter->safe)  // 释放安全迭代器时需要递减当前使用的迭代器数量（安全迭代器只能有一个）
            iter->d->iterators--;
        else
            assert(iter->fingerprint == dictFingerprint(iter->d));  // 迭代器的字典指纹和实时的字典指纹不符时报错
    }
    zfree(iter);
}

/* Return a random entry from the hash table. Useful to
 * implement randomized algorithms */

/* 从哈希表中随机返回一个entry。适用于实现随机算法。 */
dictEntry *dictGetRandomKey(dict *d)
{
    dictEntry *he, *orighe;
    unsigned int h;
    int listlen, listele;

    if (dictSize(d) == 0) return NULL;  // 字典没有元素时直接返回NULL
    if (dictIsRehashing(d)) _dictRehashStep(d);  // 字典在rehash过程中，执行一次一步的rehash
    if (dictIsRehashing(d)) {  // 字典正在rehash
        do {
            /* We are sure there are no elements in indexes from 0
             * to rehashidx-1 */
            /* 我们知道0-rehashidx-1之间的索引范围内没有元素 */
            h = d->rehashidx + (random() % (d->ht[0].size +
                                            d->ht[1].size -
                                            d->rehashidx));
            he = (h >= d->ht[0].size) ? d->ht[1].table[h - d->ht[0].size] :
                                      d->ht[0].table[h];
        } while(he == NULL);
    } else {  // 字典不在rehash时，随机生成一个索引值，直到此索引值上有entry
        do {
            h = random() & d->ht[0].sizemask;
            he = d->ht[0].table[h];
        } while(he == NULL);
    }

    /* Now we found a non empty bucket, but it is a linked
     * list and we need to get a random element from the list.
     * The only sane way to do so is counting the elements and
     * select a random index. */
    /* 我们找到了一个非空的桶，但它是一个链表结构，所以我们要从链表中随机获取一个元素。
     * 唯一明智的方式是计算链表长度并随机选择一个索引值。*/
    listlen = 0;
    orighe = he;
    while(he) {  // 计算链表长度
        he = he->next;
        listlen++;
    }
    listele = random() % listlen;
    he = orighe;
    while(listele--) he = he->next;
    return he;
}

/* This function samples the dictionary to return a few keys from random
 * locations.
 *
 * It does not guarantee to return all the keys specified in 'count', nor
 * it does guarantee to return non-duplicated elements, however it will make
 * some effort to do both things.
 *
 * Returned pointers to hash table entries are stored into 'des' that
 * points to an array of dictEntry pointers. The array must have room for
 * at least 'count' elements, that is the argument we pass to the function
 * to tell how many random elements we need.
 *
 * The function returns the number of items stored into 'des', that may
 * be less than 'count' if the hash table has less than 'count' elements
 * inside, or if not enough elements were found in a reasonable amount of
 * steps.
 *
 * Note that this function is not suitable when you need a good distribution
 * of the returned items, but only when you need to "sample" a given number
 * of continuous elements to run some kind of algorithm or to produce
 * statistics. However the function is much faster than dictGetRandomKey()
 * at producing N elements. */

/* 此函数对字典进行随机采样，从一些随机位置返回一些key
 *
 * 并不保证返回'count'中指定个数的key，并且也不保证不会返回重复的元素，不过函数会尽力做到
 * 返回'count'个key和尽量返回重复的key。
 *
 * 函数返回指向dictEntry数组的指针。这个数组的大小至少能容纳'count'个元素。
 *
 * 函数返回保存在'des'中entry的数量，如果哈希表中的元素小于'count'个，
 * 或者在一个合理的时间内没有找到指定个数的元素，这个数字可能会比入参'count'要小。
 *
 * 需要注意的是，此函数并不适合当你需要一个数量刚好的采样集合的情况，但当你仅仅需要进行“采样”
 * 时来进行一些统计计算时，还是适用的。用函数来获取N个随机key要比执行N次dictGetRandomKey()要快。 */
unsigned int dictGetSomeKeys(dict *d, dictEntry **des, unsigned int count) {
    unsigned long j; /* internal hash table id, 0 or 1. */  // 字典内部的哈希表编号，0或1
    unsigned long tables; /* 1 or 2 tables? */ // 哈希表数量
    unsigned long stored = 0, maxsizemask;  // 获取到的随机key数量，掩码
    unsigned long maxsteps;  // 最大步骤数，考虑到开销问题，超过这个值就放弃继续获取随机key

    if (dictSize(d) < count) count = dictSize(d);
    maxsteps = count*10;  // 最大步骤数为需要获取的key的数量的10倍

    /* Try to do a rehashing work proportional to 'count'. */
    /* 运行count次一步rehash操作 */
    for (j = 0; j < count; j++) {
        if (dictIsRehashing(d))
            _dictRehashStep(d);
        else
            break;
    }

    tables = dictIsRehashing(d) ? 2 : 1;  // 字典rehash过程中就有两个哈希表要采样，正常情况下是1个
    maxsizemask = d->ht[0].sizemask;
    if (tables > 1 && maxsizemask < d->ht[1].sizemask)  // rehash过程中如果1号哈希表比0号哈希表搭则使用1号哈希表的掩码
        maxsizemask = d->ht[1].sizemask;  // 

    /* Pick a random point inside the larger table. */
    /* 获取一个随机索引值 */
    unsigned long i = random() & maxsizemask;
    unsigned long emptylen = 0; /* Continuous empty entries so far. */  // 迄今为止的连续空entry数量
    while(stored < count && maxsteps--) {
        for (j = 0; j < tables; j++) {
            /* Invariant of the dict.c rehashing: up to the indexes already
             * visited in ht[0] during the rehashing, there are no populated
             * buckets, so we can skip ht[0] for indexes between 0 and idx-1. */
            /* 和dict.c中的rehash一样： 由于ht[0]正在进行rehash，那里并没有密集的有元素的桶
             * 需要访问，我们可以跳过ht[0]中位于0到idx-1之间的桶，idx是字典的数据rehash的当前索引位置
             * 这个位置以前的桶中的数据都已经被移动到ht[1]了。 */
            if (tables == 2 && j == 0 && i < (unsigned long) d->rehashidx) {
                /* Moreover, if we are currently out of range in the second
                 * table, there will be no elements in both tables up to
                 * the current rehashing index, so we jump if possible.
                 * (this happens when going from big to small table). */
                /* 此外，在rehash过程中，如果我们获取的随机索引值i大于ht[1]的大小，则ht[0]
                 * 和ht[1]都已经没有可用元素让我们获取，此时我们可以直接跳过。
                 * （这一版发生在字典空间从大表小的情况下）。 */
                if (i >= d->ht[1].size) i = d->rehashidx;
                continue;
            }
            if (i >= d->ht[j].size) continue; /* Out of range for this table. */  //获取的随机索引值i超出范围，直接开始下一次循环
            dictEntry *he = d->ht[j].table[i];  // 获取到一个entry

            /* Count contiguous empty buckets, and jump to other
             * locations if they reach 'count' (with a minimum of 5). */
            /* 计算连续遇到的空桶的数量，如果到达'count'就跳到其他位置去获取（'count'最小值为5） */
            if (he == NULL) {
                emptylen++;
                if (emptylen >= 5 && emptylen > count) {
                    i = random() & maxsizemask;  // 重新获取随机值i
                    emptylen = 0;  // 重置连续遇到的空桶的数量
                }
            } else {  // 遇到了非空桶
                emptylen = 0;  // 重置连续遇到的空桶的数量
                while (he) {
                    /* Collect all the elements of the buckets found non
                     * empty while iterating. */
                    /* 把桶中entry链表中的所有元素加入到结果数组中 */
                    *des = he;
                    des++;
                    he = he->next;
                    stored++;
                    if (stored == count) return stored;
                }
            }
        }
        i = (i+1) & maxsizemask;
    }
    return stored;
}

/* Function to reverse bits. Algorithm from:
 * http://graphics.stanford.edu/~seander/bithacks.html#ReverseParallel */

/* 反转bits的算法：
 * http://graphics.stanford.edu/~seander/bithacks.html#ReverseParallel */
static unsigned long rev(unsigned long v) {
    unsigned long s = 8 * sizeof(v); // bit size; must be power of 2
    unsigned long mask = ~0;
    while ((s >>= 1) > 0) {
        mask ^= (mask << s);
        v = ((v >> s) & mask) | ((v << s) & ~mask);
    }
    return v;
}

/* dictScan() is used to iterate over the elements of a dictionary.
 *
 * Iterating works the following way:
 *
 * 1) Initially you call the function using a cursor (v) value of 0.
 * 2) The function performs one step of the iteration, and returns the
 *    new cursor value you must use in the next call.
 * 3) When the returned cursor is 0, the iteration is complete.
 *
 * The function guarantees all elements present in the
 * dictionary get returned between the start and end of the iteration.
 * However it is possible some elements get returned multiple times.
 *
 * For every element returned, the callback argument 'fn' is
 * called with 'privdata' as first argument and the dictionary entry
 * 'de' as second argument.
 *
 * HOW IT WORKS.
 *
 * The iteration algorithm was designed by Pieter Noordhuis.
 * The main idea is to increment a cursor starting from the higher order
 * bits. That is, instead of incrementing the cursor normally, the bits
 * of the cursor are reversed, then the cursor is incremented, and finally
 * the bits are reversed again.
 *
 * This strategy is needed because the hash table may be resized between
 * iteration calls.
 *
 * dict.c hash tables are always power of two in size, and they
 * use chaining, so the position of an element in a given table is given
 * by computing the bitwise AND between Hash(key) and SIZE-1
 * (where SIZE-1 is always the mask that is equivalent to taking the rest
 *  of the division between the Hash of the key and SIZE).
 *
 * For example if the current hash table size is 16, the mask is
 * (in binary) 1111. The position of a key in the hash table will always be
 * the last four bits of the hash output, and so forth.
 *
 * WHAT HAPPENS IF THE TABLE CHANGES IN SIZE?
 *
 * If the hash table grows, elements can go anywhere in one multiple of
 * the old bucket: for example let's say we already iterated with
 * a 4 bit cursor 1100 (the mask is 1111 because hash table size = 16).
 *
 * If the hash table will be resized to 64 elements, then the new mask will
 * be 111111. The new buckets you obtain by substituting in ??1100
 * with either 0 or 1 can be targeted only by keys we already visited
 * when scanning the bucket 1100 in the smaller hash table.
 *
 * By iterating the higher bits first, because of the inverted counter, the
 * cursor does not need to restart if the table size gets bigger. It will
 * continue iterating using cursors without '1100' at the end, and also
 * without any other combination of the final 4 bits already explored.
 *
 * Similarly when the table size shrinks over time, for example going from
 * 16 to 8, if a combination of the lower three bits (the mask for size 8
 * is 111) were already completely explored, it would not be visited again
 * because we are sure we tried, for example, both 0111 and 1111 (all the
 * variations of the higher bit) so we don't need to test it again.
 *
 * WAIT... YOU HAVE *TWO* TABLES DURING REHASHING!
 *
 * Yes, this is true, but we always iterate the smaller table first, then
 * we test all the expansions of the current cursor into the larger
 * table. For example if the current cursor is 101 and we also have a
 * larger table of size 16, we also test (0)101 and (1)101 inside the larger
 * table. This reduces the problem back to having only one table, where
 * the larger one, if it exists, is just an expansion of the smaller one.
 *
 * LIMITATIONS
 *
 * This iterator is completely stateless, and this is a huge advantage,
 * including no additional memory used.
 *
 * The disadvantages resulting from this design are:
 *
 * 1) It is possible we return elements more than once. However this is usually
 *    easy to deal with in the application level.
 * 2) The iterator must return multiple elements per call, as it needs to always
 *    return all the keys chained in a given bucket, and all the expansions, so
 *    we are sure we don't miss keys moving during rehashing.
 * 3) The reverse cursor is somewhat hard to understand at first, but this
 *    comment is supposed to help.
 */
unsigned long dictScan(dict *d,
                       unsigned long v,
                       dictScanFunction *fn,
                       void *privdata)
{
    dictht *t0, *t1;
    const dictEntry *de;
    unsigned long m0, m1;

    if (dictSize(d) == 0) return 0;

    if (!dictIsRehashing(d)) {
        t0 = &(d->ht[0]);
        m0 = t0->sizemask;

        /* Emit entries at cursor */
        de = t0->table[v & m0];
        while (de) {
            fn(privdata, de);
            de = de->next;
        }

    } else {
        t0 = &d->ht[0];
        t1 = &d->ht[1];

        /* Make sure t0 is the smaller and t1 is the bigger table */
        if (t0->size > t1->size) {
            t0 = &d->ht[1];
            t1 = &d->ht[0];
        }

        m0 = t0->sizemask;
        m1 = t1->sizemask;

        /* Emit entries at cursor */
        de = t0->table[v & m0];
        while (de) {
            fn(privdata, de);
            de = de->next;
        }

        /* Iterate over indices in larger table that are the expansion
         * of the index pointed to by the cursor in the smaller table */
        do {
            /* Emit entries at cursor */
            de = t1->table[v & m1];
            while (de) {
                fn(privdata, de);
                de = de->next;
            }

            /* Increment bits not covered by the smaller mask */
            v = (((v | m0) + 1) & ~m0) | (v & m0);

            /* Continue while bits covered by mask difference is non-zero */
        } while (v & (m0 ^ m1));
    }

    /* Set unmasked bits so incrementing the reversed cursor
     * operates on the masked bits of the smaller table */
    v |= ~m0;

    /* Increment the reverse cursor */
    v = rev(v);
    v++;
    v = rev(v);

    return v;
}

/* ------------------------- private functions ------------------------------ */

/* Expand the hash table if needed */
static int _dictExpandIfNeeded(dict *d)
{
    /* Incremental rehashing already in progress. Return. */
    if (dictIsRehashing(d)) return DICT_OK;

    /* If the hash table is empty expand it to the initial size. */
    if (d->ht[0].size == 0) return dictExpand(d, DICT_HT_INITIAL_SIZE);

    /* If we reached the 1:1 ratio, and we are allowed to resize the hash
     * table (global setting) or we should avoid it but the ratio between
     * elements/buckets is over the "safe" threshold, we resize doubling
     * the number of buckets. */
    if (d->ht[0].used >= d->ht[0].size &&
        (dict_can_resize ||
         d->ht[0].used/d->ht[0].size > dict_force_resize_ratio))
    {
        return dictExpand(d, d->ht[0].used*2);
    }
    return DICT_OK;
}

/* Our hash table capability is a power of two */
static unsigned long _dictNextPower(unsigned long size)
{
    unsigned long i = DICT_HT_INITIAL_SIZE;

    if (size >= LONG_MAX) return LONG_MAX;
    while(1) {
        if (i >= size)
            return i;
        i *= 2;
    }
}

/* Returns the index of a free slot that can be populated with
 * a hash entry for the given 'key'.
 * If the key already exists, -1 is returned.
 *
 * Note that if we are in the process of rehashing the hash table, the
 * index is always returned in the context of the second (new) hash table. */
static int _dictKeyIndex(dict *d, const void *key)
{
    unsigned int h, idx, table;
    dictEntry *he;

    /* Expand the hash table if needed */
    if (_dictExpandIfNeeded(d) == DICT_ERR)
        return -1;
    /* Compute the key hash value */
    h = dictHashKey(d, key);
    for (table = 0; table <= 1; table++) {
        idx = h & d->ht[table].sizemask;
        /* Search if this slot does not already contain the given key */
        he = d->ht[table].table[idx];
        while(he) {
            if (key==he->key || dictCompareKeys(d, key, he->key))
                return -1;
            he = he->next;
        }
        if (!dictIsRehashing(d)) break;
    }
    return idx;
}

void dictEmpty(dict *d, void(callback)(void*)) {
    _dictClear(d,&d->ht[0],callback);
    _dictClear(d,&d->ht[1],callback);
    d->rehashidx = -1;
    d->iterators = 0;
}

void dictEnableResize(void) {
    dict_can_resize = 1;
}

void dictDisableResize(void) {
    dict_can_resize = 0;
}

/* ------------------------------- Debugging ---------------------------------*/

#define DICT_STATS_VECTLEN 50
size_t _dictGetStatsHt(char *buf, size_t bufsize, dictht *ht, int tableid) {
    unsigned long i, slots = 0, chainlen, maxchainlen = 0;
    unsigned long totchainlen = 0;
    unsigned long clvector[DICT_STATS_VECTLEN];
    size_t l = 0;

    if (ht->used == 0) {
        return snprintf(buf,bufsize,
            "No stats available for empty dictionaries\n");
    }

    /* Compute stats. */
    for (i = 0; i < DICT_STATS_VECTLEN; i++) clvector[i] = 0;
    for (i = 0; i < ht->size; i++) {
        dictEntry *he;

        if (ht->table[i] == NULL) {
            clvector[0]++;
            continue;
        }
        slots++;
        /* For each hash entry on this slot... */
        chainlen = 0;
        he = ht->table[i];
        while(he) {
            chainlen++;
            he = he->next;
        }
        clvector[(chainlen < DICT_STATS_VECTLEN) ? chainlen : (DICT_STATS_VECTLEN-1)]++;
        if (chainlen > maxchainlen) maxchainlen = chainlen;
        totchainlen += chainlen;
    }

    /* Generate human readable stats. */
    l += snprintf(buf+l,bufsize-l,
        "Hash table %d stats (%s):\n"
        " table size: %ld\n"
        " number of elements: %ld\n"
        " different slots: %ld\n"
        " max chain length: %ld\n"
        " avg chain length (counted): %.02f\n"
        " avg chain length (computed): %.02f\n"
        " Chain length distribution:\n",
        tableid, (tableid == 0) ? "main hash table" : "rehashing target",
        ht->size, ht->used, slots, maxchainlen,
        (float)totchainlen/slots, (float)ht->used/slots);

    for (i = 0; i < DICT_STATS_VECTLEN-1; i++) {
        if (clvector[i] == 0) continue;
        if (l >= bufsize) break;
        l += snprintf(buf+l,bufsize-l,
            "   %s%ld: %ld (%.02f%%)\n",
            (i == DICT_STATS_VECTLEN-1)?">= ":"",
            i, clvector[i], ((float)clvector[i]/ht->size)*100);
    }

    /* Unlike snprintf(), teturn the number of characters actually written. */
    if (bufsize) buf[bufsize-1] = '\0';
    return strlen(buf);
}

void dictGetStats(char *buf, size_t bufsize, dict *d) {
    size_t l;
    char *orig_buf = buf;
    size_t orig_bufsize = bufsize;

    l = _dictGetStatsHt(buf,bufsize,&d->ht[0],0);
    buf += l;
    bufsize -= l;
    if (dictIsRehashing(d) && bufsize > 0) {
        _dictGetStatsHt(buf,bufsize,&d->ht[1],1);
    }
    /* Make sure there is a NULL term at the end. */
    if (orig_bufsize) orig_buf[orig_bufsize-1] = '\0';
}
