/* SDSLib 2.0 -- A C dynamic strings library
 *
 * Copyright (c) 2006-2015, Salvatore Sanfilippo <antirez at gmail dot com>
 * Copyright (c) 2015, Oran Agra
 * Copyright (c) 2015, Redis Labs, Inc
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
  简单动态字符串
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include "sds.h"
#include "sdsalloc.h"

/* 获取sds header的大小 */
static inline int sdsHdrSize(char type) {
    switch(type&SDS_TYPE_MASK) {  // 获取sds类型
        case SDS_TYPE_5:
            return sizeof(struct sdshdr5);
        case SDS_TYPE_8:
            return sizeof(struct sdshdr8);
        case SDS_TYPE_16:
            return sizeof(struct sdshdr16);
        case SDS_TYPE_32:
            return sizeof(struct sdshdr32);
        case SDS_TYPE_64:
            return sizeof(struct sdshdr64);
    }
    return 0;
}

/* 根据字符串大小判断sds类型 */
static inline char sdsReqType(size_t string_size) {
    if (string_size < 1<<5)
        return SDS_TYPE_5;
    if (string_size < 1<<8)
        return SDS_TYPE_8;
    if (string_size < 1<<16)
        return SDS_TYPE_16;
    if (string_size < 1ll<<32)
        return SDS_TYPE_32;
    return SDS_TYPE_64;
}

/* Create a new sds string with the content specified by the 'init' pointer
 * and 'initlen'.
 * If NULL is used for 'init' the string is initialized with zero bytes.
 *
 * The string is always null-termined (all the sds strings are, always) so
 * even if you create an sds string with:
 *
 * mystring = sdsnewlen("abc",3);
 *
 * You can print the string with printf() as there is an implicit \0 at the
 * end of the string. However the string is binary safe and can contain
 * \0 characters in the middle, as the length is stored in the sds header. */

/* 使用init指针指向的数据和initlen的长度创建一个新的sds字符串。
 * 如果init指针是NULL，字符串会被初始化为长度为initlen，内容全为0字节。
 * 
 * sds字符串总是以'\0'字符结尾的，所以即使你创建了如下的sds字符串：
 * 
 * mystring = sdsnewlen("abc",3);
 *
 * 由于这个字符串在结尾隐式包含了一个'\0'，所以你可以使用printf()函数打印它。
 * 然而，sds字符串是二进制安全的，并且可以在中间包含'\0'字符，因为在sds字符串header中
 * 保存了字符串长度。
*/
sds sdsnewlen(const void *init, size_t initlen) {
    void *sh;
    sds s;
    char type = sdsReqType(initlen);  // 使用初始长度判断该创建哪种类型的sds字符串
    /* Empty strings are usually created in order to append. Use type 8
     * since type 5 is not good at this. */
    /* 空字符串一般在创建后都会追加数据进去（完全可能大于32个字节），使用type 8的字符串类型要优于type 5 */
    if (type == SDS_TYPE_5 && initlen == 0) type = SDS_TYPE_8;
    int hdrlen = sdsHdrSize(type);  // 获取header长度
    unsigned char *fp; /* flags pointer. */

    sh = s_malloc(hdrlen+initlen+1);  // 为sds字符串header申请内存空间，大小为：头部大小+初始化长度大小+1（其中1是为'\0'留的）
    if (!init)  // 初始数据指针为NULL
        memset(sh, 0, hdrlen+initlen+1);  // 把整个sds的内容都设置为0
    if (sh == NULL) return NULL;  // 申请内存失败返回NULL
    s = (char*)sh+hdrlen;  // 字符串指针
    fp = ((unsigned char*)s)-1;  // flags指针
    switch(type) {  // 根据sds类型设置header中的数据
        case SDS_TYPE_5: {
            *fp = type | (initlen << SDS_TYPE_BITS);
            break;
        }
        case SDS_TYPE_8: {
            SDS_HDR_VAR(8,s);
            sh->len = initlen;
            sh->alloc = initlen;
            *fp = type;
            break;
        }
        case SDS_TYPE_16: {
            SDS_HDR_VAR(16,s);
            sh->len = initlen;
            sh->alloc = initlen;
            *fp = type;
            break;
        }
        case SDS_TYPE_32: {
            SDS_HDR_VAR(32,s);
            sh->len = initlen;
            sh->alloc = initlen;
            *fp = type;
            break;
        }
        case SDS_TYPE_64: {
            SDS_HDR_VAR(64,s);
            sh->len = initlen;
            sh->alloc = initlen;
            *fp = type;
            break;
        }
    }
    if (initlen && init)
        memcpy(s, init, initlen);  // 将初始化数据指针init指向的数据拷贝到字符串中
    s[initlen] = '\0';  // 设置最后一个字节为'\0'
    return s;
}

/* Create an empty (zero length) sds string. Even in this case the string
 * always has an implicit null term. */

/* 创建一个空sds(字符串长度为0)字符串。即使在这种情况下，字符串也总是有一个隐式的'\0'结束符 */
sds sdsempty(void) {
    return sdsnewlen("",0);
}

/* Create a new sds string starting from a null terminated C string. */

/* 使用一个以'\0'为结束符的C字符串创建一个新的sds字符串 */
sds sdsnew(const char *init) {
    size_t initlen = (init == NULL) ? 0 : strlen(init);  // 初始化数据指针为NULL时，字符串长度为0
    return sdsnewlen(init, initlen);
}

/* Duplicate an sds string. */

/* 复制一个sds字符串 */
sds sdsdup(const sds s) {
    return sdsnewlen(s, sdslen(s));
}

/* Free an sds string. No operation is performed if 's' is NULL. */

/* 释放一个sds字符串，如果该字符串是NULL则什么都不做。 */
void sdsfree(sds s) {
    if (s == NULL) return;
    s_free((char*)s-sdsHdrSize(s[-1]));
}

/* Set the sds string length to the length as obtained with strlen(), so
 * considering as content only up to the first null term character.
 *
 * This function is useful when the sds string is hacked manually in some
 * way, like in the following example:
 *
 * s = sdsnew("foobar");
 * s[2] = '\0';
 * sdsupdatelen(s);
 * printf("%d\n", sdslen(s));
 *
 * The output will be "2", but if we comment out the call to sdsupdatelen()
 * the output will be "6" as the string was modified but the logical length
 * remains 6 bytes. */

/* 使用通过strlen()获取的sds字符串长度来设置sds字符串的长度，
 * 所以只考虑到第一个空字符前的字符串长度。
 * 
 * 当sds字符串被手动修改的时候这个函数很有用，比如下面的例子：
 * 
 * s = sdsnew("foobar");
 * s[2] = '\0';
 * sdsupdatelen(s);
 * printf("%d\n", sdslen(s));
 * 
 * 上面的代码输出是"2"，但是如果我们注释掉调用sdsupdatelen()的那行代码，输出则是'6'，
 * 因为字符串被强行修改了，但字符串的逻辑长度还是6个字节。
*/
void sdsupdatelen(sds s) {
    int reallen = strlen(s);  // 获取字符串的真实长度（会取第一个终止符'\0'之前的字符串长度）
    sdssetlen(s, reallen);  // 重新设置sds的字符串长度
}

/* Modify an sds string in-place to make it empty (zero length).
 * However all the existing buffer is not discarded but set as free space
 * so that next append operations will not require allocations up to the
 * number of bytes previously available. */

/* 就地修改一个sds字符串为空（长度为0）。
 * 然而，所有当前的缓冲区都不会被释放，而是设置成空闲空间，
 * 所以下一次追加操作可以使用原来的空闲空间而不需要分配空间。 */
void sdsclear(sds s) {
    sdssetlen(s, 0);  // 设置sds字符串的长度为0
    s[0] = '\0';  // 设置字符串首地址为终止符'\0'
}

/* Enlarge the free space at the end of the sds string so that the caller
 * is sure that after calling this function can overwrite up to addlen
 * bytes after the end of the string, plus one more byte for nul term.
 *
 * Note: this does not change the *length* of the sds string as returned
 * by sdslen(), but only the free buffer space we have. */

/* 扩充sds字符串的空闲空间，调用此函数后，可以保证在原sds字符串后面扩充了addlen个字节的
 * 空间，外加1个字节的终止符。
 * 
 * 注意：这个函数不会改变调用sdslen()返回的字符串长度，仅仅改变了空闲空间的大小。 */
sds sdsMakeRoomFor(sds s, size_t addlen) {
    void *sh, *newsh;
    size_t avail = sdsavail(s);  // 获取sds字符串的空闲空间大小
    size_t len, newlen;
    char type, oldtype = s[-1] & SDS_TYPE_MASK;  // 获取sds字符串类型
    int hdrlen;

    /* Return ASAP if there is enough space left. */
    /* 如果当前空闲空间大于addlen，就不做扩充操作，直接返回 */
    if (avail >= addlen) return s;

    len = sdslen(s);  // sds字符串当前长度
    sh = (char*)s-sdsHdrSize(oldtype);  // sds字符串header指针
    newlen = (len+addlen);  // 扩充后的新长度
    if (newlen < SDS_MAX_PREALLOC)  // 扩充后的长度小于sds最大预分配长度时，把newlen加倍以防止短期内再扩充
        newlen *= 2;
    else  // 否则直接加上sds最大预分配长度
        newlen += SDS_MAX_PREALLOC;

    type = sdsReqType(newlen);  // 获取新长度下的sds字符串类型

    /* Don't use type 5: the user is appending to the string and type 5 is
     * not able to remember empty space, so sdsMakeRoomFor() must be called
     * at every appending operation. */
    /* 不要使用type 5：由于用户向字符串追加数据时，type 5的字符串无法保存空闲空间，所以
     * 每次追加数据时都要调用sdsMakeRoomFor() */
    if (type == SDS_TYPE_5) type = SDS_TYPE_8;  // 比较短的字符串一律用type 8

    hdrlen = sdsHdrSize(type);  // 计算sds字符串header长度
    if (oldtype==type) {  // 字符串类型不变的情况下
        newsh = s_realloc(sh, hdrlen+newlen+1);  // 在原header指针上重新分配新的大小
        if (newsh == NULL) return NULL;
        s = (char*)newsh+hdrlen;  // 更新字符串指针
    } else {
        /* Since the header size changes, need to move the string forward,
         * and can't use realloc */
        /* 一旦header大小变化，需要把字符串前移，并且不能使用realloc */
        newsh = s_malloc(hdrlen+newlen+1);  // 新开辟一块内存
        if (newsh == NULL) return NULL;
        memcpy((char*)newsh+hdrlen, s, len+1);  // 把原始sds字符串的内容复制到新的内存区域
        s_free(sh);  // 释放原始sds字符串的头指针指向的内存
        s = (char*)newsh+hdrlen;  // 更新sds字符串指针
        s[-1] = type;  // 更新flags字节信息
        sdssetlen(s, len);  // 更新sds字符串header中的len
    }
    sdssetalloc(s, newlen);  // 更新sds字符串header中的alloc
    return s;
}

/* Reallocate the sds string so that it has no free space at the end. The
 * contained string remains not altered, but next concatenation operations
 * will require a reallocation.
 *
 * After the call, the passed sds string is no longer valid and all the
 * references must be substituted with the new pointer returned by the call. */

/* 重新分配sds字符串的空间，保证结尾没有空闲空间。其中包含的字符串不变，
 * 但下一次进行字符串连接操作时需要一次空间重新分配。
 * 
 * 调用此函数后，原来作为参数传入的sds字符串的指针不再是有效的，
 * 所有引用必须被替换为函数返回的新指针。 */
sds sdsRemoveFreeSpace(sds s) {
    void *sh, *newsh;
    char type, oldtype = s[-1] & SDS_TYPE_MASK;
    int hdrlen;
    size_t len = sdslen(s);  // 字符串真正的长度
    sh = (char*)s-sdsHdrSize(oldtype);  // 获取sds字符串header指针

    type = sdsReqType(len);  // 计算字符串的新type
    hdrlen = sdsHdrSize(type);  // 计算字符串的新header大小
    if (oldtype==type) {  // 字符串类型不变
        newsh = s_realloc(sh, hdrlen+len+1);  // realloc，大小更新为：header大小+真实字符串大小+1
        if (newsh == NULL) return NULL;
        s = (char*)newsh+hdrlen;  // 更新sds字符串指针
    } else {  // 字符串类型改变
        newsh = s_malloc(hdrlen+len+1);  // 新开辟一块内存
        if (newsh == NULL) return NULL;
        memcpy((char*)newsh+hdrlen, s, len+1);  // 复制数据到新内存中
        s_free(sh);  // 释放原始的sds字符串内存
        s = (char*)newsh+hdrlen; // 更新sds字符串指针
        s[-1] = type;  // 更新flags
        sdssetlen(s, len);  // 更新sds字符串header中的len
    }
    sdssetalloc(s, len);  // 更新sds字符串header中的alloc
    return s;
}

/* Return the total size of the allocation of the specifed sds string,
 * including:
 * 1) The sds header before the pointer.
 * 2) The string.
 * 3) The free buffer at the end if any.
 * 4) The implicit null term.
 */

/* 返回指定sds字符串的分配空间大小，
 * 包括:
 * 1) sds header大小。
 * 2) 字符串本身的大小。
 * 3) 末尾的空闲空间大小（如果有的话）。
 * 4) 隐式包含的终止符。
 */
size_t sdsAllocSize(sds s) {
    size_t alloc = sdsalloc(s);  // 获取sds header的alloc
    return sdsHdrSize(s[-1])+alloc+1;  // header大小+alloc（字符串大小+空闲空间大小）+1
}

/* Return the pointer of the actual SDS allocation (normally SDS strings
 * are referenced by the start of the string buffer). */

/* 返回sds分配空间的首地址（一般来说sds字符串的指针是其字符串缓冲区的首地址） */
void *sdsAllocPtr(sds s) {
    return (void*) (s-sdsHdrSize(s[-1]));  // 字符串缓冲区的首地址减去header大小即可
}

/* Increment the sds length and decrements the left free space at the
 * end of the string according to 'incr'. Also set the null term
 * in the new end of the string.
 *
 * This function is used in order to fix the string length after the
 * user calls sdsMakeRoomFor(), writes something after the end of
 * the current string, and finally needs to set the new length.
 *
 * Note: it is possible to use a negative increment in order to
 * right-trim the string.
 *
 * Usage example:
 *
 * Using sdsIncrLen() and sdsMakeRoomFor() it is possible to mount the
 * following schema, to cat bytes coming from the kernel to the end of an
 * sds string without copying into an intermediate buffer:
 *
 * oldlen = sdslen(s);
 * s = sdsMakeRoomFor(s, BUFFER_SIZE);
 * nread = read(fd, s+oldlen, BUFFER_SIZE);
 * ... check for nread <= 0 and handle it ...
 * sdsIncrLen(s, nread);
 */

/* 取决于'incr'参数，此函数增加sds字符串的长度或减少剩余空闲空间的大小。
 * 同时也将在新字符串的末尾设置终止符。
 *
 * 此函数用来修正调用sdsMakeRoomFor()函数之后字符串的长度，在当前字符串后追加数据
 * 这些需要设置字符串新长度的操作之后。
 *
 * 注意：可以使用一个负的增量值来右对齐字符串。
 *
 * 用例:
 *
 * 使用sdsIncrLen()和sdsMakeRoomFor()函数可以用来满足如下模式，
 * 从内核中直接复制一部分字节到一个sds字符串的末尾，且无须把数据先复制到一个中间缓冲区中：
 *
 * oldlen = sdslen(s);
 * s = sdsMakeRoomFor(s, BUFFER_SIZE);
 * nread = read(fd, s+oldlen, BUFFER_SIZE);
 * ... check for nread <= 0 and handle it ...
 * sdsIncrLen(s, nread);
 */
void sdsIncrLen(sds s, int incr) {
    unsigned char flags = s[-1];
    size_t len;
    switch(flags&SDS_TYPE_MASK) {  // 判断sds字符串类型
        case SDS_TYPE_5: {
            unsigned char *fp = ((unsigned char*)s)-1;  // flags指针
            unsigned char oldlen = SDS_TYPE_5_LEN(flags); // 原始字符串大小
            assert((incr > 0 && oldlen+incr < 32) || (incr < 0 && oldlen >= (unsigned int)(-incr)));
            *fp = SDS_TYPE_5 | ((oldlen+incr) << SDS_TYPE_BITS);  // 更新flags中字符串大小的比特位
            len = oldlen+incr;  // 更新header的len
            break;
        }
        case SDS_TYPE_8: {
            SDS_HDR_VAR(8,s);  // 获取sds字符串的header指针
            assert((incr >= 0 && sh->alloc-sh->len >= incr) || (incr < 0 && sh->len >= (unsigned int)(-incr)));
            len = (sh->len += incr);  // 更新header的len
            break;
        }
        case SDS_TYPE_16: {
            SDS_HDR_VAR(16,s);
            assert((incr >= 0 && sh->alloc-sh->len >= incr) || (incr < 0 && sh->len >= (unsigned int)(-incr)));
            len = (sh->len += incr);
            break;
        }
        case SDS_TYPE_32: {
            SDS_HDR_VAR(32,s);
            assert((incr >= 0 && sh->alloc-sh->len >= (unsigned int)incr) || (incr < 0 && sh->len >= (unsigned int)(-incr)));
            len = (sh->len += incr);
            break;
        }
        case SDS_TYPE_64: {
            SDS_HDR_VAR(64,s);
            assert((incr >= 0 && sh->alloc-sh->len >= (uint64_t)incr) || (incr < 0 && sh->len >= (uint64_t)(-incr)));
            len = (sh->len += incr);
            break;
        }
        default: len = 0; /* Just to avoid compilation warnings. */
    }
    s[len] = '\0';  // 设置终止符
}

/* Grow the sds to have the specified length. Bytes that were not part of
 * the original length of the sds will be set to zero.
 *
 * if the specified length is smaller than the current length, no operation
 * is performed. */

/* 增长一个sds字符串到一个指定长度。扩充出来的不是原来字符串的空间会被设置为0。
 *
 * 如果指定的长度比当前长度小，不做任何操作。*/
sds sdsgrowzero(sds s, size_t len) {
    size_t curlen = sdslen(s);  // 当前字符串长度

    if (len <= curlen) return s;  // 设置的长度小于当前长度，直接返回原始sds字符串指针
    s = sdsMakeRoomFor(s,len-curlen);  // 扩充sds
    if (s == NULL) return NULL;

    /* Make sure added region doesn't contain garbage */
    /* 确保新增的区域不包含垃圾数据 */
    memset(s+curlen,0,(len-curlen+1)); /* also set trailing \0 byte */
    sdssetlen(s, len);  // 更新sds字符串header中的len
    return s;
}

/* Append the specified binary-safe string pointed by 't' of 'len' bytes to the
 * end of the specified sds string 's'.
 *
 * After the call, the passed sds string is no longer valid and all the
 * references must be substituted with the new pointer returned by the call. */

/* 向指定的sds字符串's'尾部追加由't'指向的二进制安全的字符串，长度'len'字节。
 *
 * 调用此函数后，原来作为参数传入的sds字符串的指针不再是有效的，
 * 所有引用必须被替换为函数返回的新指针。 */
sds sdscatlen(sds s, const void *t, size_t len) {
    size_t curlen = sdslen(s);  // 当前字符串长度

    s = sdsMakeRoomFor(s,len);  // 扩充len字节
    if (s == NULL) return NULL;
    memcpy(s+curlen, t, len);  // 追加数据到原字符串末尾
    sdssetlen(s, curlen+len);  // 更新sds字符串header中的len
    s[curlen+len] = '\0';  // 设置终止符
    return s;
}

/* Append the specified null termianted C string to the sds string 's'.
 *
 * After the call, the passed sds string is no longer valid and all the
 * references must be substituted with the new pointer returned by the call. */

/* 追加指定的C字符串到sds字符串's'的尾部。
 *
 * 调用此函数后，原来作为参数传入的sds字符串的指针不再是有效的，
 * 所有引用必须被替换为函数返回的新指针。 */
sds sdscat(sds s, const char *t) {
    return sdscatlen(s, t, strlen(t));
}

/* Append the specified sds 't' to the existing sds 's'.
 *
 * After the call, the modified sds string is no longer valid and all the
 * references must be substituted with the new pointer returned by the call. */

/* 追加指定的sds字符串't'到已经存在的sds字符串's'末尾。
 *
 * 调用此函数后，原来作为参数传入的sds字符串的指针不再是有效的，
 * 所有引用必须被替换为函数返回的新指针。 */
sds sdscatsds(sds s, const sds t) {
    return sdscatlen(s, t, sdslen(t));
}

/* Destructively modify the sds string 's' to hold the specified binary
 * safe string pointed by 't' of length 'len' bytes. */

/* 把由't'指向的二进制安全的字符串复制到sds字符串's'的内存空间中，长度为'len'，覆盖原来的数据 */
sds sdscpylen(sds s, const char *t, size_t len) {
    if (sdsalloc(s) < len) {
        s = sdsMakeRoomFor(s,len-sdslen(s));  // 原sds总空间不足就扩充
        if (s == NULL) return NULL;
    }
    memcpy(s, t, len);  // 将t指向的数据直接覆盖s
    s[len] = '\0';  // 设置终止符
    sdssetlen(s, len);  // 更新sds字符串header中的len
    return s;
}

/* Like sdscpylen() but 't' must be a null-termined string so that the length
 * of the string is obtained with strlen(). */

/* 和sdscpylen()函数类似，但是't'指向的必须是一个以'\0'结尾的字符串，
 * 所以可以用strlen()获取该字符串长度。 */
sds sdscpy(sds s, const char *t) {
    return sdscpylen(s, t, strlen(t));
}

/* Helper for sdscatlonglong() doing the actual number -> string
 * conversion. 's' must point to a string with room for at least
 * SDS_LLSTR_SIZE bytes.
 *
 * The function returns the length of the null-terminated string
 * representation stored at 's'. */

/* sdscatlonglong()的帮助函数，负责将一个数字转换成一个字符串。
 * 's'必须指向一个大小至少为SDS_LLSTR_SIZE字节的字符串。
 *
 * 此函数返回's'指向的以'\0'结束的字符串的长度。 */
#define SDS_LLSTR_SIZE 21
int sdsll2str(char *s, long long value) {
    char *p, aux;
    unsigned long long v;
    size_t l;

    /* Generate the string representation, this method produces
     * an reversed string. */
    /* 此方法生成一个字符串的逆序字符串。 */
    v = (value < 0) ? -value : value;  // 使用传入数字的绝对值
    p = s;
    do {  // 循环取数字的个位数复制到s的相应位置
        *p++ = '0'+(v%10);  
        v /= 10;  // 
    } while(v);
    if (value < 0) *p++ = '-';  // 负数则在在最后一个数字上加上-

    /* Compute length and add null term. */
    /* 计算字符串长度并在结尾加上'\0' */
    l = p-s;
    *p = '\0';

    /* Reverse the string. */
    /* 逆序排列字符串 */
    p--;
    while(s < p) {
        aux = *s;
        *s = *p;
        *p = aux;
        s++;
        p--;
    }
    return l;
}

/* Identical sdsll2str(), but for unsigned long long type. */

/* 和sdsll2str()相同, 但是针对无符号的long long 类型。
 * 代码中去掉了对负数情况的处理。 */
int sdsull2str(char *s, unsigned long long v) {
    char *p, aux;
    size_t l;

    /* Generate the string representation, this method produces
     * an reversed string. */
    p = s;
    do {
        *p++ = '0'+(v%10);
        v /= 10;
    } while(v);

    /* Compute length and add null term. */
    l = p-s;
    *p = '\0';

    /* Reverse the string. */
    p--;
    while(s < p) {
        aux = *s;
        *s = *p;
        *p = aux;
        s++;
        p--;
    }
    return l;
}

/* Create an sds string from a long long value. It is much faster than:
 *
 * sdscatprintf(sdsempty(),"%lld\n", value);
 */

/* 从一个long long类型的数字创建sds字符串，这个函数比如下调用要快：
 *
 * sdscatprintf(sdsempty(),"%lld\n", value);
 */
sds sdsfromlonglong(long long value) {
    char buf[SDS_LLSTR_SIZE];  // 定义一个字符串数组
    int len = sdsll2str(buf,value);  // 从数字创建字符串

    return sdsnewlen(buf,len);  // 更新sds字符串header的len
}

/* Like sdscatprintf() but gets va_list instead of being variadic. */

/* 和sdscatprintf()类似，但使用va_list而不是变长参数列表 */
sds sdscatvprintf(sds s, const char *fmt, va_list ap) {
    va_list cpy;
    char staticbuf[1024], *buf = staticbuf, *t;
    size_t buflen = strlen(fmt)*2;  // buflen设置为格式化字符串长度的两倍

    /* We try to start using a static buffer for speed.
     * If not possible we revert to heap allocation. */
    /* 若需要的缓冲区大小大于静态缓冲区大小（1024字节），则使用静态缓冲区高效处理，
     * 如果行不通则转为在堆上申请内存。 */
    if (buflen > sizeof(staticbuf)) {
        buf = s_malloc(buflen);
        if (buf == NULL) return NULL;
    } else {
        buflen = sizeof(staticbuf);
    }

    /* Try with buffers two times bigger every time we fail to
     * fit the string in the current buffer size. */
    /* 当前缓冲区大小不足以容纳字符串时，尝试扩大缓冲区大小为原来的2倍。 */
    while(1) {
        buf[buflen-2] = '\0';
        va_copy(cpy,ap);  // 指向可变参数的指针
        vsnprintf(buf, buflen, fmt, cpy);  // 把生成的格式化字符串放到buf中
        va_end(cpy);
        if (buf[buflen-2] != '\0') {
            if (buf != staticbuf) s_free(buf);
            buflen *= 2;
            buf = s_malloc(buflen);
            if (buf == NULL) return NULL;
            continue;
        }
        break;
    }

    /* Finally concat the obtained string to the SDS string and return it. */
    /* 最后把获得的字符串连接到sds字符串末尾并返回。 */
    t = sdscat(s, buf);
    if (buf != staticbuf) s_free(buf);
    return t;
}

/* Append to the sds string 's' a string obtained using printf-alike format
 * specifier.
 *
 * After the call, the modified sds string is no longer valid and all the
 * references must be substituted with the new pointer returned by the call.
 *
 * Example:
 *
 * s = sdsnew("Sum is: ");
 * s = sdscatprintf(s,"%d+%d = %d",a,b,a+b).
 *
 * Often you need to create a string from scratch with the printf-alike
 * format. When this is the need, just use sdsempty() as the target string:
 *
 * s = sdscatprintf(sdsempty(), "... your format ...", args);
 */

/* 将一个由类似printf函数生成的格式化字符串追加到sds字符串's'末尾。
 *
 * 调用此函数后，原来作为参数传入的sds字符串的指针不再是有效的，
 * 所有引用必须被替换为函数返回的新指针。
 *
 * 例子：
 *
 * s = sdsnew("Sum is: ");
 * s = sdscatprintf(s,"%d+%d = %d",a,b,a+b).
 *
 * 你经常需要从一个类似printf函数生成的格式化字符串中创建字符串。
 * 当你需要这么做时，址要使用sdsempty()的结果作为目标字符串即可：
 *
 * s = sdscatprintf(sdsempty(), "... your format ...", args);
 */
sds sdscatprintf(sds s, const char *fmt, ...) {
    va_list ap;
    char *t;
    va_start(ap, fmt);
    t = sdscatvprintf(s,fmt,ap);
    va_end(ap);
    return t;
}

/* This function is similar to sdscatprintf, but much faster as it does
 * not rely on sprintf() family functions implemented by the libc that
 * are often very slow. Moreover directly handling the sds string as
 * new data is concatenated provides a performance improvement.
 *
 * However this function only handles an incompatible subset of printf-alike
 * format specifiers:
 *
 * %s - C String
 * %S - SDS string
 * %i - signed int
 * %I - 64 bit signed integer (long long, int64_t)
 * %u - unsigned int
 * %U - 64 bit unsigned integer (unsigned long long, uint64_t)
 * %% - Verbatim "%" character.
 */

/* 该函数和sdscatprintf类似，但比它更快，因为它不依赖于由libc实现的sprintf()族的函数，
 * 这些函数经常很慢。此外直接将sds字符串作为新数据进行连接的的性能更好。
 *
 * 然而这个函数只能处理类printf函数的格式化说明符的子集：
 *
 * %s - C字符串
 * %S - sds字符串
 * %i - 有符号整型
 * %I - 64位有符号整型(long long, int64_t)
 * %u - 无符号整型
 * %U - 64位无符号整型(unsigned long long, uint64_t)
 * %% - 逐个处理%字符
 */
sds sdscatfmt(sds s, char const *fmt, ...) {
    size_t initlen = sdslen(s);
    const char *f = fmt;
    int i;
    va_list ap;

    va_start(ap,fmt);
    f = fmt;    /* Next format specifier byte to process. */
    i = initlen; /* Position of the next byte to write to dest str. */
    while(*f) {
        char next, *str;
        size_t l;
        long long num;
        unsigned long long unum;

        /* Make sure there is always space for at least 1 char. */
        if (sdsavail(s)==0) {
            s = sdsMakeRoomFor(s,1);
        }

        switch(*f) {
        case '%':
            next = *(f+1);
            f++;
            switch(next) {
            case 's':
            case 'S':
                str = va_arg(ap,char*);
                l = (next == 's') ? strlen(str) : sdslen(str);
                if (sdsavail(s) < l) {
                    s = sdsMakeRoomFor(s,l);
                }
                memcpy(s+i,str,l);
                sdsinclen(s,l);
                i += l;
                break;
            case 'i':
            case 'I':
                if (next == 'i')
                    num = va_arg(ap,int);
                else
                    num = va_arg(ap,long long);
                {
                    char buf[SDS_LLSTR_SIZE];
                    l = sdsll2str(buf,num);
                    if (sdsavail(s) < l) {
                        s = sdsMakeRoomFor(s,l);
                    }
                    memcpy(s+i,buf,l);
                    sdsinclen(s,l);
                    i += l;
                }
                break;
            case 'u':
            case 'U':
                if (next == 'u')
                    unum = va_arg(ap,unsigned int);
                else
                    unum = va_arg(ap,unsigned long long);
                {
                    char buf[SDS_LLSTR_SIZE];
                    l = sdsull2str(buf,unum);
                    if (sdsavail(s) < l) {
                        s = sdsMakeRoomFor(s,l);
                    }
                    memcpy(s+i,buf,l);
                    sdsinclen(s,l);
                    i += l;
                }
                break;
            default: /* Handle %% and generally %<unknown>. */
                s[i++] = next;
                sdsinclen(s,1);
                break;
            }
            break;
        default:
            s[i++] = *f;
            sdsinclen(s,1);
            break;
        }
        f++;
    }
    va_end(ap);

    /* Add null-term */
    s[i] = '\0';
    return s;
}

/* Remove the part of the string from left and from right composed just of
 * contiguous characters found in 'cset', that is a null terminted C string.
 *
 * After the call, the modified sds string is no longer valid and all the
 * references must be substituted with the new pointer returned by the call.
 *
 * Example:
 *
 * s = sdsnew("AA...AA.a.aa.aHelloWorld     :::");
 * s = sdstrim(s,"Aa. :");
 * printf("%s\n", s);
 *
 * Output will be just "Hello World".
 */

/* 移除字符串从左到右或从右到左出现的、连续的组合字符串，
 * 字符串中的字符都是出现在'cset'中的字符。
 *
 * 调用此函数后，原来作为参数传入的sds字符串的指针不再是有效的，
 * 所有引用必须被替换为函数返回的新指针。
 *
 * 例子：
 *
 * s = sdsnew("AA...AA.a.aa.aHelloWorld     :::");
 * s = sdstrim(s,"Aa. :");
 * printf("%s\n", s);
 *
 * 输出为 "Hello World".
 */
sds sdstrim(sds s, const char *cset) {
    char *start, *end, *sp, *ep;
    size_t len;

    sp = start = s;  // 字符串首地址指针
    ep = end = s+sdslen(s)-1;  // 字符串尾地址指针
    while(sp <= end && strchr(cset, *sp)) sp++;  // sp最终会停留在字符串左侧第一个不在cset中的字符地址上
    while(ep > sp && strchr(cset, *ep)) ep--;  // ep最终会停留在字符串右侧第一个不在cset中的字符地址上
    len = (sp > ep) ? 0 : ((ep-sp)+1);  // 过滤后的字符串长度
    if (s != sp) memmove(s, sp, len);  // 更新字符串内容
    s[len] = '\0';
    sdssetlen(s,len);  // 更新len
    return s;
}

/* Turn the string into a smaller (or equal) string containing only the
 * substring specified by the 'start' and 'end' indexes.
 *
 * start and end can be negative, where -1 means the last character of the
 * string, -2 the penultimate character, and so forth.
 *
 * The interval is inclusive, so the start and end characters will be part
 * of the resulting string.
 *
 * The string is modified in-place.
 *
 * Example:
 *
 * s = sdsnew("Hello World");
 * sdsrange(s,1,-1); => "ello World"
 */

/* 将字符串截取为由'start'和'end'标示的索引区域的子串。 
 * 
 * start和end参数可以是负数，-1表示字符串的最后一个字符，-2是倒数第二个字符，以此类推。
 *
 * 最终结果会包含start和end位置的字符（闭区间）。
 *
 * 字符串会被就地修改，不会返回一个新的字符串。
 *
 * 例子：
 *
 * s = sdsnew("Hello World");
 * sdsrange(s,1,-1); => "ello World"
 */
void sdsrange(sds s, int start, int end) {
    size_t newlen, len = sdslen(s);

    if (len == 0) return;
    if (start < 0) {
        start = len+start;
        if (start < 0) start = 0;
    }
    if (end < 0) {
        end = len+end;
        if (end < 0) end = 0;
    }
    newlen = (start > end) ? 0 : (end-start)+1;
    if (newlen != 0) {
        if (start >= (signed)len) {
            newlen = 0;
        } else if (end >= (signed)len) {
            end = len-1;
            newlen = (start > end) ? 0 : (end-start)+1;
        }
    } else {
        start = 0;
    }
    if (start && newlen) memmove(s, s+start, newlen);  // 将截取的字符串覆盖原字符串
    s[newlen] = 0;
    sdssetlen(s,newlen);  // 更新len
}

/* Apply tolower() to every character of the sds string 's'. */

/* 将sds字符串's'的所有字符转换为小写。 */
void sdstolower(sds s) {
    int len = sdslen(s), j;

    for (j = 0; j < len; j++) s[j] = tolower(s[j]);
}

/* Apply toupper() to every character of the sds string 's'. */

/* 将sds字符串's'的所有字符转换为大写。 */
void sdstoupper(sds s) {
    int len = sdslen(s), j;

    for (j = 0; j < len; j++) s[j] = toupper(s[j]);
}

/* Compare two sds strings s1 and s2 with memcmp().
 *
 * Return value:
 *
 *     positive if s1 > s2.
 *     negative if s1 < s2.
 *     0 if s1 and s2 are exactly the same binary string.
 *
 * If two strings share exactly the same prefix, but one of the two has
 * additional characters, the longer string is considered to be greater than
 * the smaller one. */

/* 使用memcmp()比较两个sds字符串s1和s2。
 *
 * 返回值含义：
 *
 *     s1 > s2时返回正数。
 *     s1 < s2时返回负数。
 *     当s1和s2的二进制串完全相等时返回0。
 *
 * 如果两个字符串有相同的前缀，但其中一个有额外的字符，长度更长的字符串会被认为更大。 */
int sdscmp(const sds s1, const sds s2) {
    size_t l1, l2, minlen;
    int cmp;

    l1 = sdslen(s1);
    l2 = sdslen(s2);
    minlen = (l1 < l2) ? l1 : l2;  // 两个字符串的最小长度
    cmp = memcmp(s1,s2,minlen);
    if (cmp == 0) return l1-l2;
    return cmp;
}

/* Split 's' with separator in 'sep'. An array
 * of sds strings is returned. *count will be set
 * by reference to the number of tokens returned.
 *
 * On out of memory, zero length string, zero length
 * separator, NULL is returned.
 *
 * Note that 'sep' is able to split a string using
 * a multi-character separator. For example
 * sdssplit("foo_-_bar","_-_"); will return two
 * elements "foo" and "bar".
 *
 * This version of the function is binary-safe but
 * requires length arguments. sdssplit() is just the
 * same function but for zero-terminated strings.
 */

/* 使用分割符'sep'分割sds字符串's'。返回一个sds字符串的数组。
 * *count指向一个整型，表示分割成了多少个token。
 *
 * 内存不足、空字符串或空分隔符都将返回NULL。
 *
 * 注意'sep'分割符可以是多字符的。比如sdssplit("foo_-_bar","_-_");
 * 将会返回"foo"和"bar"两个元素。
 *
 * 这个版本的函数是二进制安全的，但需要参数的长度。
 * sdssplit()函数和它类似，但是是专门为以'\0'结束的字符串设计的。
 */
sds *sdssplitlen(const char *s, int len, const char *sep, int seplen, int *count) {
    int elements = 0, slots = 5, start = 0, j;  // 预分配槽数为5
    sds *tokens;  // 结果是一个sds数组

    if (seplen < 1 || len < 0) return NULL;  // 空字符串或空分隔符都返回NULL

    tokens = s_malloc(sizeof(sds)*slots);
    if (tokens == NULL) return NULL;

    if (len == 0) {
        *count = 0;
        return tokens;
    }
    for (j = 0; j < (len-(seplen-1)); j++) {
        /* make sure there is room for the next element and the final one */
        /* 保证预留下一个和最后一个元素两个元素的空间 */
        if (slots < elements+2) {
            sds *newtokens;

            slots *= 2;  // 扩充槽数为原来的2倍
            newtokens = s_realloc(tokens,sizeof(sds)*slots);  // 重新分配空间
            if (newtokens == NULL) goto cleanup;  // 申请空间失败，做清除操作
            tokens = newtokens;  // 更新tokens指针
        }
        /* search the separator */
        /* 查找分割符 */
        if ((seplen == 1 && *(s+j) == sep[0]) || (memcmp(s+j,sep,seplen) == 0)) {  // sep为单字符和多字符的比较方式
            tokens[elements] = sdsnewlen(s+start,j-start);
            if (tokens[elements] == NULL) goto cleanup;
            elements++;  // 更新分割出来的元素个数
            start = j+seplen;  // 更新下一次开始的位置
            j = j+seplen-1; /* skip the separator */  // 跳过分割符
        }
    }
    /* Add the final element. We are sure there is room in the tokens array. */
    /* 添加最后一个元素 */
    tokens[elements] = sdsnewlen(s+start,len-start);
    if (tokens[elements] == NULL) goto cleanup;
    elements++;
    *count = elements;  // 更新token个数指针，外部可以通过*count知道原字符串被分割成了多少个token
    return tokens;

cleanup:
    {
        int i;
        for (i = 0; i < elements; i++) sdsfree(tokens[i]);  // 遍历tokens数组释放token
        s_free(tokens);
        *count = 0;  // 分割token个数设置为0
        return NULL;
    }
}

/* Free the result returned by sdssplitlen(), or do nothing if 'tokens' is NULL. *／
/* 释放调用sdssplitlen()函数的返回值，如果tokens为NULL什么都不做。 */
void sdsfreesplitres(sds *tokens, int count) {
    if (!tokens) return;
    while(count--)
        sdsfree(tokens[count]);
    s_free(tokens);
}

/* Append to the sds string "s" an escaped string representation where
 * all the non-printable characters (tested with isprint()) are turned into
 * escapes in the form "\n\r\a...." or "\x<hex-number>".
 *
 * After the call, the modified sds string is no longer valid and all the
 * references must be substituted with the new pointer returned by the call. */

/* 向sds字符串's'末尾追加一个已转义的字符串，这个字符串中的所有不可打印字符（可以用isprint()测试）
 * 都被转义成"\n\r\a...."或"\x<hex-number>"。
 *
 * 调用此函数后，原来作为参数传入的sds字符串的指针不再是有效的，
 * 所有引用必须被替换为函数返回的新指针。 */
sds sdscatrepr(sds s, const char *p, size_t len) {
    s = sdscatlen(s,"\"",1);  // 在s末尾追加一个双引号
    while(len--) {
        switch(*p) {  // 判断新字符串当前位置的值
        case '\\':
        case '"':
            s = sdscatprintf(s,"\\%c",*p);
            break;
        case '\n': s = sdscatlen(s,"\\n",2); break;
        case '\r': s = sdscatlen(s,"\\r",2); break;
        case '\t': s = sdscatlen(s,"\\t",2); break;
        case '\a': s = sdscatlen(s,"\\a",2); break;
        case '\b': s = sdscatlen(s,"\\b",2); break;
        default:
            if (isprint(*p))
                s = sdscatprintf(s,"%c",*p);
            else
                s = sdscatprintf(s,"\\x%02x",(unsigned char)*p);
            break;
        }
        p++;
    }
    return sdscatlen(s,"\"",1);  // 在末尾再追加一个双引号
}

/* Helper function for sdssplitargs() that returns non zero if 'c'
 * is a valid hex digit. */

/* sdssplitargs()函数的帮助方法，该函数判断传入的字符'c'是否是一个合法的十六进制数。 */
int is_hex_digit(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
}

/* Helper function for sdssplitargs() that converts a hex digit into an
 * integer from 0 to 15 */

/* sdssplitargs()函数的帮助方法，将一个十六进制数转换成相对应的十进制数（0-15）。 */
int hex_digit_to_int(char c) {
    switch(c) {
    case '0': return 0;
    case '1': return 1;
    case '2': return 2;
    case '3': return 3;
    case '4': return 4;
    case '5': return 5;
    case '6': return 6;
    case '7': return 7;
    case '8': return 8;
    case '9': return 9;
    case 'a': case 'A': return 10;
    case 'b': case 'B': return 11;
    case 'c': case 'C': return 12;
    case 'd': case 'D': return 13;
    case 'e': case 'E': return 14;
    case 'f': case 'F': return 15;
    default: return 0;
    }
}

/* Split a line into arguments, where every argument can be in the
 * following programming-language REPL-alike form:
 *
 * foo bar "newline are supported\n" and "\xff\x00otherstuff"
 *
 * The number of arguments is stored into *argc, and an array
 * of sds is returned.
 *
 * The caller should free the resulting array of sds strings with
 * sdsfreesplitres().
 *
 * Note that sdscatrepr() is able to convert back a string into
 * a quoted string in the same format sdssplitargs() is able to parse.
 *
 * The function returns the allocated tokens on success, even when the
 * input string is empty, or NULL if the input contains unbalanced
 * quotes or closed quotes followed by non space characters
 * as in: "foo"bar or "foo'
 */

/* 将一行文本分割成多个参数，每个参数是类似交互式环境下命令的格式：
 *
 * foo bar "newline are supported\n" and "\xff\x00otherstuff"
 *
 * 参数的个数保存在*argc中，函数返回一个sds字符串的数组。
 *
 * 调用者应该调用sdsfreesplitres()函数来释放返回的结果数组。
 *
 * sdscatrepr()可以把一个被引号包含的字符串转换成一个个token，就像
 * sdssplitargs()的逆操作。
 *
 * 此函数成功时返回一个token数组，即使入参字符串为空或NULL或者
 * 包含未闭合的引号，比如："foo"bar 或 "foo'
 */
sds *sdssplitargs(const char *line, int *argc) {
    const char *p = line;
    char *current = NULL;
    char **vector = NULL;

    *argc = 0;
    while(1) {
        /* skip blanks */
        while(*p && isspace(*p)) p++;
        if (*p) {
            /* get a token */
            int inq=0;  /* set to 1 if we are in "quotes" */
            int insq=0; /* set to 1 if we are in 'single quotes' */
            int done=0;

            if (current == NULL) current = sdsempty();
            while(!done) {
                if (inq) {
                    if (*p == '\\' && *(p+1) == 'x' &&
                                             is_hex_digit(*(p+2)) &&
                                             is_hex_digit(*(p+3)))
                    {
                        unsigned char byte;

                        byte = (hex_digit_to_int(*(p+2))*16)+
                                hex_digit_to_int(*(p+3));
                        current = sdscatlen(current,(char*)&byte,1);
                        p += 3;
                    } else if (*p == '\\' && *(p+1)) {
                        char c;

                        p++;
                        switch(*p) {
                        case 'n': c = '\n'; break;
                        case 'r': c = '\r'; break;
                        case 't': c = '\t'; break;
                        case 'b': c = '\b'; break;
                        case 'a': c = '\a'; break;
                        default: c = *p; break;
                        }
                        current = sdscatlen(current,&c,1);
                    } else if (*p == '"') {
                        /* closing quote must be followed by a space or
                         * nothing at all. */
                        if (*(p+1) && !isspace(*(p+1))) goto err;
                        done=1;
                    } else if (!*p) {
                        /* unterminated quotes */
                        goto err;
                    } else {
                        current = sdscatlen(current,p,1);
                    }
                } else if (insq) {
                    if (*p == '\\' && *(p+1) == '\'') {
                        p++;
                        current = sdscatlen(current,"'",1);
                    } else if (*p == '\'') {
                        /* closing quote must be followed by a space or
                         * nothing at all. */
                        if (*(p+1) && !isspace(*(p+1))) goto err;
                        done=1;
                    } else if (!*p) {
                        /* unterminated quotes */
                        goto err;
                    } else {
                        current = sdscatlen(current,p,1);
                    }
                } else {
                    switch(*p) {
                    case ' ':
                    case '\n':
                    case '\r':
                    case '\t':
                    case '\0':
                        done=1;
                        break;
                    case '"':
                        inq=1;
                        break;
                    case '\'':
                        insq=1;
                        break;
                    default:
                        current = sdscatlen(current,p,1);
                        break;
                    }
                }
                if (*p) p++;
            }
            /* add the token to the vector */
            vector = s_realloc(vector,((*argc)+1)*sizeof(char*));
            vector[*argc] = current;
            (*argc)++;
            current = NULL;
        } else {
            /* Even on empty input string return something not NULL. */
            if (vector == NULL) vector = s_malloc(sizeof(void*));
            return vector;
        }
    }

err:
    while((*argc)--)
        sdsfree(vector[*argc]);
    s_free(vector);
    if (current) sdsfree(current);
    *argc = 0;
    return NULL;
}

/* Modify the string substituting all the occurrences of the set of
 * characters specified in the 'from' string to the corresponding character
 * in the 'to' array.
 *
 * For instance: sdsmapchars(mystring, "ho", "01", 2)
 * will have the effect of turning the string "hello" into "0ell1".
 *
 * The function returns the sds string pointer, that is always the same
 * as the input pointer since no resize is needed. */

/* 在sds字符串中，用'to'字符串中的字符替换'from'字符串中的字符，
 * 'to'字符串和'from'字符串的字符有位置对应关系。
 *
 * 比如: sdsmapchars(mystring, "ho", "01", 2)
 * 将会把"hello"转换成"0ell1"。（注意替换字符和原字符的位置对应关系）
 *
 * 此函数返回sds字符串的指针，这和入参的sds字符串相同，因为不需要重新分配空间。 */
sds sdsmapchars(sds s, const char *from, const char *to, size_t setlen) {
    size_t j, i, l = sdslen(s);

    for (j = 0; j < l; j++) {
        for (i = 0; i < setlen; i++) {
            if (s[j] == from[i]) {
                s[j] = to[i];
                break;
            }
        }
    }
    return s;
}

/* Join an array of C strings using the specified separator (also a C string).
 * Returns the result as an sds string. */

/* 使用分隔符sep将字符数组argv拼接成一个字符串。
 * 返回值是一个sds字符串。 */
sds sdsjoin(char **argv, int argc, char *sep) {
    sds join = sdsempty();  // 初始化一个空的sds字符串
    int j;

    for (j = 0; j < argc; j++) {
        join = sdscat(join, argv[j]);  // 把argv中的字符串连接到join中
        if (j != argc-1) join = sdscat(join,sep);  // 最后一个字符串手动追加一个分割符sep
    }
    return join;
}

/* Like sdsjoin, but joins an array of SDS strings. */

/* 和sdsjoin类似，但argv中的元素都是sds字符串 */
sds sdsjoinsds(sds *argv, int argc, const char *sep, size_t seplen) {
    sds join = sdsempty();
    int j;

    for (j = 0; j < argc; j++) {
        join = sdscatsds(join, argv[j]);
        if (j != argc-1) join = sdscatlen(join,sep,seplen);
    }
    return join;
}

/* Wrappers to the allocators used by SDS. Note that SDS will actually
 * just use the macros defined into sdsalloc.h in order to avoid to pay
 * the overhead of function calls. Here we define these wrappers only for
 * the programs SDS is linked to, if they want to touch the SDS internals
 * even if they use a different allocator. */

/* sds使用的内存分配包装函数。注意，实际上这些包装函数只是定义在sdsalloc.h的宏而已，
 * 因为这么做可以避免额外的函数调用开销。下面定义的这些包装函数只是给那些想要接触
 * sds内部的外部连接sds的程序用的。 */
void *sds_malloc(size_t size) { return s_malloc(size); }
void *sds_realloc(void *ptr, size_t size) { return s_realloc(ptr,size); }
void sds_free(void *ptr) { s_free(ptr); }

/* 以下是sds的测试用例 */
#if defined(SDS_TEST_MAIN)
#include <stdio.h>
#include "testhelp.h"
#include "limits.h"

#define UNUSED(x) (void)(x)
int sdsTest(void) {
    {
        sds x = sdsnew("foo"), y;

        test_cond("Create a string and obtain the length",
            sdslen(x) == 3 && memcmp(x,"foo\0",4) == 0)

        sdsfree(x);
        x = sdsnewlen("foo",2);
        test_cond("Create a string with specified length",
            sdslen(x) == 2 && memcmp(x,"fo\0",3) == 0)

        x = sdscat(x,"bar");
        test_cond("Strings concatenation",
            sdslen(x) == 5 && memcmp(x,"fobar\0",6) == 0);

        x = sdscpy(x,"a");
        test_cond("sdscpy() against an originally longer string",
            sdslen(x) == 1 && memcmp(x,"a\0",2) == 0)

        x = sdscpy(x,"xyzxxxxxxxxxxyyyyyyyyyykkkkkkkkkk");
        test_cond("sdscpy() against an originally shorter string",
            sdslen(x) == 33 &&
            memcmp(x,"xyzxxxxxxxxxxyyyyyyyyyykkkkkkkkkk\0",33) == 0)

        sdsfree(x);
        x = sdscatprintf(sdsempty(),"%d",123);
        test_cond("sdscatprintf() seems working in the base case",
            sdslen(x) == 3 && memcmp(x,"123\0",4) == 0)

        sdsfree(x);
        x = sdsnew("--");
        x = sdscatfmt(x, "Hello %s World %I,%I--", "Hi!", LLONG_MIN,LLONG_MAX);
        test_cond("sdscatfmt() seems working in the base case",
            sdslen(x) == 60 &&
            memcmp(x,"--Hello Hi! World -9223372036854775808,"
                     "9223372036854775807--",60) == 0)
        printf("[%s]\n",x);

        sdsfree(x);
        x = sdsnew("--");
        x = sdscatfmt(x, "%u,%U--", UINT_MAX, ULLONG_MAX);
        test_cond("sdscatfmt() seems working with unsigned numbers",
            sdslen(x) == 35 &&
            memcmp(x,"--4294967295,18446744073709551615--",35) == 0)

        sdsfree(x);
        x = sdsnew(" x ");
        sdstrim(x," x");
        test_cond("sdstrim() works when all chars match",
            sdslen(x) == 0)

        sdsfree(x);
        x = sdsnew(" x ");
        sdstrim(x," ");
        test_cond("sdstrim() works when a single char remains",
            sdslen(x) == 1 && x[0] == 'x')

        sdsfree(x);
        x = sdsnew("xxciaoyyy");
        sdstrim(x,"xy");
        test_cond("sdstrim() correctly trims characters",
            sdslen(x) == 4 && memcmp(x,"ciao\0",5) == 0)

        y = sdsdup(x);
        sdsrange(y,1,1);
        test_cond("sdsrange(...,1,1)",
            sdslen(y) == 1 && memcmp(y,"i\0",2) == 0)

        sdsfree(y);
        y = sdsdup(x);
        sdsrange(y,1,-1);
        test_cond("sdsrange(...,1,-1)",
            sdslen(y) == 3 && memcmp(y,"iao\0",4) == 0)

        sdsfree(y);
        y = sdsdup(x);
        sdsrange(y,-2,-1);
        test_cond("sdsrange(...,-2,-1)",
            sdslen(y) == 2 && memcmp(y,"ao\0",3) == 0)

        sdsfree(y);
        y = sdsdup(x);
        sdsrange(y,2,1);
        test_cond("sdsrange(...,2,1)",
            sdslen(y) == 0 && memcmp(y,"\0",1) == 0)

        sdsfree(y);
        y = sdsdup(x);
        sdsrange(y,1,100);
        test_cond("sdsrange(...,1,100)",
            sdslen(y) == 3 && memcmp(y,"iao\0",4) == 0)

        sdsfree(y);
        y = sdsdup(x);
        sdsrange(y,100,100);
        test_cond("sdsrange(...,100,100)",
            sdslen(y) == 0 && memcmp(y,"\0",1) == 0)

        sdsfree(y);
        sdsfree(x);
        x = sdsnew("foo");
        y = sdsnew("foa");
        test_cond("sdscmp(foo,foa)", sdscmp(x,y) > 0)

        sdsfree(y);
        sdsfree(x);
        x = sdsnew("bar");
        y = sdsnew("bar");
        test_cond("sdscmp(bar,bar)", sdscmp(x,y) == 0)

        sdsfree(y);
        sdsfree(x);
        x = sdsnew("aar");
        y = sdsnew("bar");
        test_cond("sdscmp(bar,bar)", sdscmp(x,y) < 0)

        sdsfree(y);
        sdsfree(x);
        x = sdsnewlen("\a\n\0foo\r",7);
        y = sdscatrepr(sdsempty(),x,sdslen(x));
        test_cond("sdscatrepr(...data...)",
            memcmp(y,"\"\\a\\n\\x00foo\\r\"",15) == 0)

        {
            unsigned int oldfree;
            char *p;
            int step = 10, j, i;

            sdsfree(x);
            sdsfree(y);
            x = sdsnew("0");
            test_cond("sdsnew() free/len buffers", sdslen(x) == 1 && sdsavail(x) == 0);

            /* Run the test a few times in order to hit the first two
             * SDS header types. */
            for (i = 0; i < 10; i++) {
                int oldlen = sdslen(x);
                x = sdsMakeRoomFor(x,step);
                int type = x[-1]&SDS_TYPE_MASK;

                test_cond("sdsMakeRoomFor() len", sdslen(x) == oldlen);
                if (type != SDS_TYPE_5) {
                    test_cond("sdsMakeRoomFor() free", sdsavail(x) >= step);
                    oldfree = sdsavail(x);
                }
                p = x+oldlen;
                for (j = 0; j < step; j++) {
                    p[j] = 'A'+j;
                }
                sdsIncrLen(x,step);
            }
            test_cond("sdsMakeRoomFor() content",
                memcmp("0ABCDEFGHIJABCDEFGHIJABCDEFGHIJABCDEFGHIJABCDEFGHIJABCDEFGHIJABCDEFGHIJABCDEFGHIJABCDEFGHIJABCDEFGHIJ",x,101) == 0);
            test_cond("sdsMakeRoomFor() final length",sdslen(x)==101);

            sdsfree(x);
        }
    }
    test_report()
    return 0;
}
#endif

#ifdef SDS_TEST_MAIN
int main(void) {
    return sdsTest();
}
#endif
