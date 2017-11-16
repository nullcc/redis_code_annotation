/* The ziplist is a specially encoded dually linked list that is designed
 * to be very memory efficient. It stores both strings and integer values,
 * where integers are encoded as actual integers instead of a series of
 * characters. It allows push and pop operations on either side of the list
 * in O(1) time. However, because every operation requires a reallocation of
 * the memory used by the ziplist, the actual complexity is related to the
 * amount of memory used by the ziplist.
 *
 * ----------------------------------------------------------------------------
 *
 * ZIPLIST OVERALL LAYOUT:
 * The general layout of the ziplist is as follows:
 * <zlbytes><zltail><zllen><entry><entry><zlend>
 *
 * <zlbytes> is an unsigned integer to hold the number of bytes that the
 * ziplist occupies. This value needs to be stored to be able to resize the
 * entire structure without the need to traverse it first.
 *
 * <zltail> is the offset to the last entry in the list. This allows a pop
 * operation on the far side of the list without the need for full traversal.
 *
 * <zllen> is the number of entries.When this value is larger than 2**16-2,
 * we need to traverse the entire list to know how many items it holds.
 *
 * <zlend> is a single byte special value, equal to 255, which indicates the
 * end of the list.
 *
 * ziplist的结构：
 * <zlbytes><zltail><zllen><entry><entry><zlend>
 * 这几个部分的含义如下：
 * <zlbytes>：一个无符号整数，表示ziplist所占用的字节数。这个值让我们在调整ziplist的大小时无须先遍历它获得其大小。
 * <zltail>：链表中最后一个元素的偏移量。保存这个值可以让我们从链表尾弹出元素而无须遍历整个链表找到最后一个元素的位置。
 * <zllen>：链表中的元素个数。当这个值大于2**16-2时，我们需要遍历真个链表计算出链表中的元素数量。
 * <entry>：链表中的节点。稍后会详细说明节点的数据结构。
 * <zlend>：一个拥有特殊值`255`的字节，它标识链表结束。

 * ZIPLIST ENTRIES:
 * Every entry in the ziplist is prefixed by a header that contains two pieces
 * of information. First, the length of the previous entry is stored to be
 * able to traverse the list from back to front. Second, the encoding with an
 * optional string length of the entry itself is stored.
 *
 * The length of the previous entry is encoded in the following way:
 * If this length is smaller than 254 bytes, it will only consume a single
 * byte that takes the length as value. When the length is greater than or
 * equal to 254, it will consume 5 bytes. The first byte is set to 254 to
 * indicate a larger value is following. The remaining 4 bytes take the
 * length of the previous entry as value.
 *
 * The other header field of the entry itself depends on the contents of the
 * entry. When the entry is a string, the first 2 bits of this header will hold
 * the type of encoding used to store the length of the string, followed by the
 * actual length of the string. When the entry is an integer the first 2 bits
 * are both set to 1. The following 2 bits are used to specify what kind of
 * integer will be stored after this header. An overview of the different
 * types and encodings is as follows:
 *
 * ziplist中每个节点都有一个header作为前缀，其中包含了两个字段。首先是前一个节点的长度，
 * 这个信息可以允许我们从后向前遍历ziplist。第二个字段是节点的编码和节点存储的字符串长度。
 *
 * 前一个节点的长度使用如下方式来编码：
 *
 * 如果前一个节点的长度小于254字节，保存前一个节点的长度只需消耗1字节，长度值就是它的值。
 * 如果前一个节点长度大于或等于254，编码它将占用5字节。其中第一个字节的值是254，用来标识后面有一个更大的值，
 * 其余4个字节的值就表示前一个节点的长度。
 * 
 * header中另一个字段的值依赖于节点的值。当节点的值是一个字符串，前两个bit将保存用于存储字符串长度的编码类型，
 * 后面是字符串的实际长度。当节点的值是一个整数时，前两个bit都为1。之后的两个bit用来指出节点header后保存的整数的类型。
 * 下面是不同类型和编码的一个概括：
 * 
 * |00pppppp| - 1 byte
 *      String value with length less than or equal to 63 bytes (6 bits).
 * |01pppppp|qqqqqqqq| - 2 bytes
 *      String value with length less than or equal to 16383 bytes (14 bits).
 * |10______|qqqqqqqq|rrrrrrrr|ssssssss|tttttttt| - 5 bytes
 *      String value with length greater than or equal to 16384 bytes.
 * |11000000| - 1 byte
 *      Integer encoded as int16_t (2 bytes).
 * |11010000| - 1 byte
 *      Integer encoded as int32_t (4 bytes).
 * |11100000| - 1 byte
 *      Integer encoded as int64_t (8 bytes).
 * |11110000| - 1 byte
 *      Integer encoded as 24 bit signed (3 bytes).
 * |11111110| - 1 byte
 *      Integer encoded as 8 bit signed (1 byte).
 * |1111xxxx| - (with xxxx between 0000 and 1101) immediate 4 bit integer.
 *      Unsigned integer from 0 to 12. The encoded value is actually from
 *      1 to 13 because 0000 and 1111 can not be used, so 1 should be
 *      subtracted from the encoded 4 bit value to obtain the right value.
 * |11111111| - End of ziplist.
 *
 * All the integers are represented in little endian byte order.
 *
 * |00pppppp| - 1 byte
 *   长度小于或等于63字节(2^6-1字节)的字符串，保存其长度需要6 bits。
 * |01pppppp|qqqqqqqq| - 2 bytes
 *   长度小于或等于16383字节(2^14-1字节)的字符串，保存其长度需要14 bits。
 * |10______|qqqqqqqq|rrrrrrrr|ssssssss|tttttttt| - 5 bytes
 *   长度大于或等于16384字节的字符串，第一个byte的第3~8个bit的值没有含义，第一个byte后的2~5个bytes保存了其长度。
 * |11000000| - 1 byte
 *   使用`int16_t`编码的整数，这个整数占用2字节。
 * |11010000| - 1 byte
 *   使用`int32_t`编码的整数，这个整数占用4字节。
 * |11100000| - 1 byte
 *   使用`int64_t`编码的整数，这个整数占用8字节。
 * |11110000| - 1 byte
 *   使用24 bits编码的整数，这个整数占用3字节。
 * |11111110| - 1 byte
 *   使用8 bits编码的整数，这个整数占用1字节。
 * |1111xxxx| - (其中xxxx的取值在0000~1101之间)
 *   表示一个4 bit整数立即编码，表示的无符号整数范围为0~12。但实际能编码的值为1(0001)~13(1101)，因为0000和1111不能使用。
 * |11111111| - ziplist的结束符
 *
 * 注意：所有整数都已小端字节序表示。
 * ----------------------------------------------------------------------------
 *
 * Copyright (c) 2009-2012, Pieter Noordhuis <pcnoordhuis at gmail dot com>
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include "zmalloc.h"
#include "util.h"
#include "ziplist.h"
#include "endianconv.h"
#include "redisassert.h"

// ziplist结束标识
#define ZIP_END 255
#define ZIP_BIGLEN 254

/* Different encoding/length possibilities */
/* 不同的编码/长度 */

// 字符串掩码 11000000
#define ZIP_STR_MASK 0xc0

// 整数掩码 00110000
#define ZIP_INT_MASK 0x30

// 字符串编码，后6位做为长度，字符串长度len<2^6 00XXXXXX，占用1字节
#define ZIP_STR_06B (0 << 6)

// 字符串编码，后14位做为长度，字符串长度len<2^14 01XXXXXX XXXXXXXX，占用2字节
#define ZIP_STR_14B (1 << 6)

// 字符串编码，后32位做为长度，字符串长度len<2^32 10000000 XXXXXXXX XXXXXXXX XXXXXXXX XXXXXXXX，占用5字节
#define ZIP_STR_32B (2 << 6)

// 16位整数编码，占用2字节，存储结构：11000000，范围-2^16~2^16-1
#define ZIP_INT_16B (0xc0 | 0<<4)

// 32位整数编码，占用4字节，存储结构：11010000，范围-2^32~2^32-1
#define ZIP_INT_32B (0xc0 | 1<<4)

// 64位整数编码，占用8字节，存储结构：11100000，范围-2^64~2^64-1
#define ZIP_INT_64B (0xc0 | 2<<4)

// 24位整数编码，占用3字节，存储结构：11110000，范围-2^24~2^24-1
#define ZIP_INT_24B (0xc0 | 3<<4)

// 8位整数编码，占用1字节，存储结构：11111110，范围-2^8~2^8-1
#define ZIP_INT_8B 0xfe

/* 4 bit integer immediate encoding */
/* 4bit整数立即编码 */

// 4bit编码整数立即编码掩码 00001111
#define ZIP_INT_IMM_MASK 0x0f

// 4bit编码整数立即编码最小值 00001111
#define ZIP_INT_IMM_MIN 0xf1    /* 11110001 */
#define ZIP_INT_IMM_MAX 0xfd    /* 11111101 */

// 获取4bit编码整数的值
#define ZIP_INT_IMM_VAL(v) (v & ZIP_INT_IMM_MASK)

// 24位整数最大值
#define INT24_MAX 0x7fffff

// 24位整数最小值
#define INT24_MIN (-INT24_MAX - 1)

/* Macro to determine type */
// 决定字符串类型的宏
#define ZIP_IS_STR(enc) (((enc) & ZIP_STR_MASK) < ZIP_STR_MASK)

/* Utility macros */
/* 工具宏 */

// 获取ziplist占用的总字节数，ziplist在zip header的第0~3个字节保存了ZIP_BYTES
#define ZIPLIST_BYTES(zl)       (*((uint32_t*)(zl)))

// 获取ziplist的尾节点偏移量，ziplist在zip header的第4~7个字节保存了ZIP_TAIL
#define ZIPLIST_TAIL_OFFSET(zl) (*((uint32_t*)((zl)+sizeof(uint32_t))))

// 获取ziplist的节点数量，ziplist在zip header的第8~9个字节保存了ZIP_LENGTH
#define ZIPLIST_LENGTH(zl)      (*((uint16_t*)((zl)+sizeof(uint32_t)*2)))

// 获取ziplist的header大小，zip header中保存了ZIP_BYTES(uint32_t)、ZIP_TAIL(uint32_t)和ZIP_LENGTH(uint16_t)，一共10字节
#define ZIPLIST_HEADER_SIZE     (sizeof(uint32_t)*2+sizeof(uint16_t))

// 获取ziplist的ZIP_END大小，是一个uint8_t类型，1字节
#define ZIPLIST_END_SIZE        (sizeof(uint8_t))

// 获取ziplist ZIP_ENTRY头节点地址，ZIP_ENTRY头指针 = ziplist首地址 + head大小
#define ZIPLIST_ENTRY_HEAD(zl)  ((zl)+ZIPLIST_HEADER_SIZE)

// 获取ziplist ZIP_ENTRY尾节点地址，ZIP_ENTRY尾指针 = ziplist首地址 + 尾节点偏移量
#define ZIPLIST_ENTRY_TAIL(zl)  ((zl)+intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl)))

// 获取ziplist尾指针（ZIP_END），ziplist尾指针 = ziplist首地址 + ziplist占用的总字节数 - 1
#define ZIPLIST_ENTRY_END(zl)   ((zl)+intrev32ifbe(ZIPLIST_BYTES(zl))-1)

/* We know a positive increment can only be 1 because entries can only be
 * pushed one at a time. */
/* ziplist节点数量的正增量只能是1（删除节点时，负增量有可能小于-1），因为每次只能添加一个元素到ziplist中。 */
#define ZIPLIST_INCR_LENGTH(zl,incr) { \
    if (ZIPLIST_LENGTH(zl) < UINT16_MAX) \
        ZIPLIST_LENGTH(zl) = intrev16ifbe(intrev16ifbe(ZIPLIST_LENGTH(zl))+incr); \
}

// 压缩链表节点结构
typedef struct zlentry {
    // prevrawlensize: 上一个节点的长度所占的字节数
    // prevrawlen: 上一个节点的长度
    unsigned int prevrawlensize, prevrawlen;

    // lensize: 编码当前节点长度len所需要的字节数
    // len: 当前节点长度
    unsigned int lensize, len;

    // 当前节点的header大小，headersize = lensize + prevrawlensize
    unsigned int headersize;

    // 当前节点的编码格式
    unsigned char encoding;

    // 当前节点指针
    unsigned char *p;
} zlentry;

// 重置压缩链表节点
#define ZIPLIST_ENTRY_ZERO(zle) { \
    (zle)->prevrawlensize = (zle)->prevrawlen = 0; \
    (zle)->lensize = (zle)->len = (zle)->headersize = 0; \
    (zle)->encoding = 0; \
    (zle)->p = NULL; \
}

/* Extract the encoding from the byte pointed by 'ptr' and set it into
 * 'encoding'. */
/* 提取ptr指向的字节的编码并把其编码设置为encoding指定的值 */
#define ZIP_ENTRY_ENCODING(ptr, encoding) do {  \
    (encoding) = (ptr[0]); \
    if ((encoding) < ZIP_STR_MASK) (encoding) &= ZIP_STR_MASK; \
} while(0)

// 打印指定的ziplist信息
void ziplistRepr(unsigned char *zl);

/* Return bytes needed to store integer encoded by 'encoding' */
/* 返回存储一个以encoding为编码的整型需要的字节数。 */
unsigned int zipIntSize(unsigned char encoding) {
    switch(encoding) {
    case ZIP_INT_8B:  return 1;
    case ZIP_INT_16B: return 2;
    case ZIP_INT_24B: return 3;
    case ZIP_INT_32B: return 4;
    case ZIP_INT_64B: return 8;
    default: return 0; /* 4 bit immediate */
    }
    assert(NULL);  // 不应该到达这里
    return 0;
}

/* Encode the length 'rawlen' writing it in 'p'. If p is NULL it just returns
 * the amount of bytes required to encode such a length. */
/* 计算新节点的长度和编码所占用的字节数，并存储在'p'中。如果p为NULL就返回编码这样的长度所需要的字节数量。 */
unsigned int zipEncodeLength(unsigned char *p, unsigned char encoding, unsigned int rawlen) {
    // len: 需要的字节数量
    // buf: 字符串长度存储结构
    unsigned char len = 1, buf[5];

    if (ZIP_IS_STR(encoding)) {
        /* Although encoding is given it may not be set for strings,
         * so we determine it here using the raw length. */
        /* 虽然给定了编码，但，所以我们这里使用原始长度来判断编码类型 */
        if (rawlen <= 0x3f) {  // 长度小于2^6，使用1个字节保存
            if (!p) return len;  // p为NULL则直接返回1（字节）
            buf[0] = ZIP_STR_06B | rawlen;
        } else if (rawlen <= 0x3fff) {  // 长度小于2^14，使用2个字节保存
            len += 1;  
            if (!p) return len;
            buf[0] = ZIP_STR_14B | ((rawlen >> 8) & 0x3f);
            buf[1] = rawlen & 0xff;
        } else {  // 其他情况，使用4个字节保存
            len += 4;
            if (!p) return len;
            buf[0] = ZIP_STR_32B;
            buf[1] = (rawlen >> 24) & 0xff;
            buf[2] = (rawlen >> 16) & 0xff;
            buf[3] = (rawlen >> 8) & 0xff;
            buf[4] = rawlen & 0xff;
        }
    } else {
        /* Implies integer encoding, so length is always 1. */
        /* 整数编码，存储结构长度总是1 */
        if (!p) return len;
        buf[0] = encoding;
    }

    /* Store this length at p */
    /* p存储字符串长度存储结构 */
    memcpy(p,buf,len);
    return len;  // 返回存储数据的长度需要的字节数量
}

/* Decode the length encoded in 'ptr'. The 'encoding' variable will hold the
 * entries encoding, the 'lensize' variable will hold the number of bytes
 * required to encode the entries length, and the 'len' variable will hold the
 * entries length. */
/* 解码ptr中被编码的长度。encoding变量保存了节点的编码，lensize变量保存了编码节点长度所需要
 * 的字节数，len变量保存节点长度。 */
#define ZIP_DECODE_LENGTH(ptr, encoding, lensize, len) do {                    \
    ZIP_ENTRY_ENCODING((ptr), (encoding));                                     \
    if ((encoding) < ZIP_STR_MASK) {                                           \
        if ((encoding) == ZIP_STR_06B) {                                       \
            (lensize) = 1;                                                     \
            (len) = (ptr)[0] & 0x3f;                                           \
        } else if ((encoding) == ZIP_STR_14B) {                                \
            (lensize) = 2;                                                     \
            (len) = (((ptr)[0] & 0x3f) << 8) | (ptr)[1];                       \
        } else if (encoding == ZIP_STR_32B) {                                  \
            (lensize) = 5;                                                     \
            (len) = ((ptr)[1] << 24) |                                         \
                    ((ptr)[2] << 16) |                                         \
                    ((ptr)[3] <<  8) |                                         \
                    ((ptr)[4]);                                                \
        } else {                                                               \
            assert(NULL);                                                      \
        }                                                                      \
    } else {                                                                   \
        /* 整数编码，存储结构长度总是1 */                                          \
        (lensize) = 1;                                                         \
        (len) = zipIntSize(encoding);                                          \
    }                                                                          \
} while(0);

/* Encode the length of the previous entry and write it to "p". Return the
 * number of bytes needed to encode this length if "p" is NULL. */
/* 将p指向的节点的header以len长度进行重新编码，更新prevrawlensize和prevrawlen，
 * len为前一个节点的长度。如果p为NULL就返回存储len长度需要的字节数。 */
unsigned int zipPrevEncodeLength(unsigned char *p, unsigned int len) {
    if (p == NULL) {
        return (len < ZIP_BIGLEN) ? 1 : sizeof(len)+1;  
    } else {
        if (len < ZIP_BIGLEN) {
            p[0] = len;
            return 1;
        } else {
            p[0] = ZIP_BIGLEN;
            memcpy(p+1,&len,sizeof(len));
            memrev32ifbe(p+1);
            return 1+sizeof(len);
        }
    }
}

/* Encode the length of the previous entry and write it to "p". This only
 * uses the larger encoding (required in __ziplistCascadeUpdate). */
/* 如果p非空，记录编码前一个节点的长度需要的字节数到p中。这个函数只在比较大的
 * encoding时使用（__ziplistCascadeUpdate函数中用到了该函数）。 */
void zipPrevEncodeLengthForceLarge(unsigned char *p, unsigned int len) {
    if (p == NULL) return;
    p[0] = ZIP_BIGLEN;
    memcpy(p+1,&len,sizeof(len));
    memrev32ifbe(p+1);
}

/* Decode the number of bytes required to store the length of the previous
 * element, from the perspective of the entry pointed to by 'ptr'. */
/* 计算ptr指向的节点中存储的上一个节点长度需要的字节数，设置在prevlensize中。 */
#define ZIP_DECODE_PREVLENSIZE(ptr, prevlensize) do {                          \
    if ((ptr)[0] < ZIP_BIGLEN) {                                               \
        (prevlensize) = 1;                                                     \
    } else {                                                                   \
        (prevlensize) = 5;                                                     \
    }                                                                          \
} while(0);

/* Decode the length of the previous element, from the perspective of the entry
 * pointed to by 'ptr'. */
/* 计算ptr指向的节点的上一个节点的长度（存储在prevlen中）
 * 和存储的上一个节点长度需要的字节数（存储在prevlensize中） */
#define ZIP_DECODE_PREVLEN(ptr, prevlensize, prevlen) do {                     \
    ZIP_DECODE_PREVLENSIZE(ptr, prevlensize);                                  \
    if ((prevlensize) == 1) {                                                  \
        (prevlen) = (ptr)[0];                                                  \
    } else if ((prevlensize) == 5) {                                           \
        assert(sizeof((prevlensize)) == 4);                                    \
        memcpy(&(prevlen), ((char*)(ptr)) + 1, 4);                             \
        memrev32ifbe(&prevlen);                                                \
    }                                                                          \
} while(0);

/* Return the difference in number of bytes needed to store the length of the
 * previous element 'len', in the entry pointed to by 'p'. */
/* 返回存储长度为len所需的字节数和存储p指向的节点的上一个节点的长度所需的字节数之差（字节）*/
int zipPrevLenByteDiff(unsigned char *p, unsigned int len) {
    unsigned int prevlensize;
    ZIP_DECODE_PREVLENSIZE(p, prevlensize);  // 计算保存p指向节点的上一个节点的长度所需的字节数
    return zipPrevEncodeLength(NULL, len) - prevlensize;
}

/* Return the total number of bytes used by the entry pointed to by 'p'. */
/* 返回p指向的节点所占用的字节数。 */
unsigned int zipRawEntryLength(unsigned char *p) {
    // 
    unsigned int prevlensize, encoding, lensize, len;
    ZIP_DECODE_PREVLENSIZE(p, prevlensize);
    ZIP_DECODE_LENGTH(p + prevlensize, encoding, lensize, len);
    return prevlensize + lensize + len;
}

/* Check if string pointed to by 'entry' can be encoded as an integer.
 * Stores the integer value in 'v' and its encoding in 'encoding'. */
/* 检查entry指向的字符串能否被编码成一个整型，能返回1，不能返回0。
 * 并保存这个整数在v中，保存这个整数的编码在encoding中。 */
int zipTryEncoding(unsigned char *entry, unsigned int entrylen, long long *v, unsigned char *encoding) {
    long long value;

    if (entrylen >= 32 || entrylen == 0) return 0;  // entry长度大于32位或者为0都不能被编码为一个整型
    if (string2ll((char*)entry,entrylen,&value)) {
        /* Great, the string can be encoded. Check what's the smallest
         * of our encoding types that can hold this value. */
        /* 这个字符串可以被编码。判断能够编码它的最小编码类型。 */
        if (value >= 0 && value <= 12) {
            *encoding = ZIP_INT_IMM_MIN+value;
        } else if (value >= INT8_MIN && value <= INT8_MAX) {
            *encoding = ZIP_INT_8B;
        } else if (value >= INT16_MIN && value <= INT16_MAX) {
            *encoding = ZIP_INT_16B;
        } else if (value >= INT24_MIN && value <= INT24_MAX) {
            *encoding = ZIP_INT_24B;
        } else if (value >= INT32_MIN && value <= INT32_MAX) {
            *encoding = ZIP_INT_32B;
        } else {
            *encoding = ZIP_INT_64B;
        }
        *v = value;
        return 1;
    }
    return 0;
}

/* Store integer 'value' at 'p', encoded as 'encoding' */
/* 把value的值保存在p指向的节点中，其中编码类型为encoding。 */
void zipSaveInteger(unsigned char *p, int64_t value, unsigned char encoding) {
    int16_t i16;
    int32_t i32;
    int64_t i64;
    if (encoding == ZIP_INT_8B) {
        ((int8_t*)p)[0] = (int8_t)value;
    } else if (encoding == ZIP_INT_16B) {
        i16 = value;
        memcpy(p,&i16,sizeof(i16));
        memrev16ifbe(p);
    } else if (encoding == ZIP_INT_24B) {
        i32 = value<<8;
        memrev32ifbe(&i32);
        memcpy(p,((uint8_t*)&i32)+1,sizeof(i32)-sizeof(uint8_t));
    } else if (encoding == ZIP_INT_32B) {
        i32 = value;
        memcpy(p,&i32,sizeof(i32));
        memrev32ifbe(p);
    } else if (encoding == ZIP_INT_64B) {
        i64 = value;
        memcpy(p,&i64,sizeof(i64));
        memrev64ifbe(p);
    } else if (encoding >= ZIP_INT_IMM_MIN && encoding <= ZIP_INT_IMM_MAX) {
        /* Nothing to do, the value is stored in the encoding itself. */
        /* 值直接保存在编码本身中，什么也不做。 */
    } else {
        assert(NULL);
    }
}

/* Read integer encoded as 'encoding' from 'p' */
/* 返回p指向的节点中以encoding为编码的整型数。 */
int64_t zipLoadInteger(unsigned char *p, unsigned char encoding) {
    int16_t i16;
    int32_t i32;
    int64_t i64, ret = 0;
    if (encoding == ZIP_INT_8B) {
        ret = ((int8_t*)p)[0];
    } else if (encoding == ZIP_INT_16B) {
        memcpy(&i16,p,sizeof(i16));
        memrev16ifbe(&i16);
        ret = i16;
    } else if (encoding == ZIP_INT_32B) {
        memcpy(&i32,p,sizeof(i32));
        memrev32ifbe(&i32);
        ret = i32;
    } else if (encoding == ZIP_INT_24B) {
        i32 = 0;
        memcpy(((uint8_t*)&i32)+1,p,sizeof(i32)-sizeof(uint8_t));
        memrev32ifbe(&i32);
        ret = i32>>8;
    } else if (encoding == ZIP_INT_64B) {
        memcpy(&i64,p,sizeof(i64));
        memrev64ifbe(&i64);
        ret = i64;
    } else if (encoding >= ZIP_INT_IMM_MIN && encoding <= ZIP_INT_IMM_MAX) {
        ret = (encoding & ZIP_INT_IMM_MASK)-1;
    } else {
        assert(NULL);
    }
    return ret;
}

/* Return a struct with all information about an entry. */
/* 将p指向的节点的所有信息存储在压缩链表节点e中 */
void zipEntry(unsigned char *p, zlentry *e) {

    ZIP_DECODE_PREVLEN(p, e->prevrawlensize, e->prevrawlen);
    ZIP_DECODE_LENGTH(p + e->prevrawlensize, e->encoding, e->lensize, e->len);
    e->headersize = e->prevrawlensize + e->lensize;
    e->p = p;
}

/* Create a new empty ziplist. */
/* 创建一个空的压缩链表 */
unsigned char *ziplistNew(void) {
    unsigned int bytes = ZIPLIST_HEADER_SIZE+1;  // 空压缩链表占用的总字节数 = 压缩链表头部大小 + ZIP_END大小（等于1）
    unsigned char *zl = zmalloc(bytes);  // 为压缩链表分配空间
    ZIPLIST_BYTES(zl) = intrev32ifbe(bytes);  // 填充压缩链表占用的总字节数数据域
    ZIPLIST_TAIL_OFFSET(zl) = intrev32ifbe(ZIPLIST_HEADER_SIZE);  // // 填充压缩链表最后一个节点的偏移量数据域
    ZIPLIST_LENGTH(zl) = 0;  // 填充压缩链表节点数量数据域
    zl[bytes-1] = ZIP_END;  // 填充压缩链表ZIP_END数据域（ZIP_END在压缩链表的最后一个字节）
    return zl;
}

/* Resize the ziplist. */
/* 调整指定的压缩链表大小 */
unsigned char *ziplistResize(unsigned char *zl, unsigned int len) {
    zl = zrealloc(zl,len);  // realloc空间
    ZIPLIST_BYTES(zl) = intrev32ifbe(len);  // 填充压缩链表占用的总字节数数据域
    zl[len-1] = ZIP_END;  // 填充压缩链表ZIP_END数据域（ZIP_END在压缩链表的最后一个字节）
    return zl;
}

/* When an entry is inserted, we need to set the prevlen field of the next
 * entry to equal the length of the inserted entry. It can occur that this
 * length cannot be encoded in 1 byte and the next entry needs to be grow
 * a bit larger to hold the 5-byte encoded prevlen. This can be done for free,
 * because this only happens when an entry is already being inserted (which
 * causes a realloc and memmove). However, encoding the prevlen may require
 * that this entry is grown as well. This effect may cascade throughout
 * the ziplist when there are consecutive entries with a size close to
 * ZIP_BIGLEN, so we need to check that the prevlen can be encoded in every
 * consecutive entry.
 *
 * Note that this effect can also happen in reverse, where the bytes required
 * to encode the prevlen field can shrink. This effect is deliberately ignored,
 * because it can cause a "flapping" effect where a chain prevlen fields is
 * first grown and then shrunk again after consecutive inserts. Rather, the
 * field is allowed to stay larger than necessary, because a large prevlen
 * field implies the ziplist is holding large entries anyway.
 *
 * The pointer "p" points to the first entry that does NOT need to be
 * updated, i.e. consecutive fields MAY need an update. */

/* 压缩链表级联更新 */
unsigned char *__ziplistCascadeUpdate(unsigned char *zl, unsigned char *p) {
    // curlen: 当前压缩链表占用的字节数，rawlen: 节点长度，rawlensize: 保存节点长度需要的字节数
    size_t curlen = intrev32ifbe(ZIPLIST_BYTES(zl)), rawlen, rawlensize;
    size_t offset, noffset, extra;
    unsigned char *np;
    zlentry cur, next;

    while (p[0] != ZIP_END) {  // p指向当前节点的首地址
        zipEntry(p, &cur);  // 将p指向的节点信息初始化到cur指向的zipEntry中
        rawlen = cur.headersize + cur.len;  // 节点总长度 = 节点头长度 + 当前节点长度
        rawlensize = zipPrevEncodeLength(NULL,rawlen);  // 计算存储当前节点的长度需要的字节数

        /* Abort if there is no next entry. */
        /* 如果是最后一个节点，则跳出while循环。 */
        if (p[rawlen] == ZIP_END) break;
        zipEntry(p+rawlen, &next);  // p+rawlen为下一个节点的首地址，初始化next为下一个节点

        /* Abort when "prevlen" has not changed. */
        /* 对next节点来说，如果上一个节点（就是cur）的长度没有改变，就不做任何操作。 */
        if (next.prevrawlen == rawlen) break;

        if (next.prevrawlensize < rawlensize) {
            /* The "prevlen" field of "next" needs more bytes to hold
             * the raw length of "cur". */
            /* next节点的上一个节点的长度所占的字节数next.prevrawlensize
             * 小于存储当前节点的长度需要的字节数时，需要扩容 */
            offset = p-zl;  // 当前节点相对于压缩链表首地址的偏移量
            extra = rawlensize-next.prevrawlensize;  // 额外需要的字节数 = 存储当前节点的长度需要的字节数（刚计算得出）- next中存储上一个节点的长度所占的字节数
            zl = ziplistResize(zl,curlen+extra);  // 调整压缩链表长度
            p = zl+offset;  // 当前节点指针

            /* Current pointer and offset for next element. */
            np = p+rawlen;  // next节点新地址
            noffset = np-zl;  // next节点的偏移量

            /* Update tail offset when next element is not the tail element. */
            /* 更新ziplist最后一个节点偏移量，如果next节点是尾部节点就不做更新。 */
            if ((zl+intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl))) != np) {
                ZIPLIST_TAIL_OFFSET(zl) =
                    intrev32ifbe(intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl))+extra);
            }

            /* Move the tail to the back. */
            /* 移动next节点到新地址，为当前节点cur空出空间。 
             *  */
            memmove(np+rawlensize,
                np+next.prevrawlensize,
                curlen-noffset-next.prevrawlensize-1);
            // 将next节点的header以rawlen长度进行重新编码，更新prevrawlensize和prevrawlen
            zipPrevEncodeLength(np,rawlen);

            /* Advance the cursor */
            /* 更新当前节点指针 */
            p += rawlen;  //  指向下一个节点
            curlen += extra;  // 更新压缩链表占用的总字节数
        } else {  
            if (next.prevrawlensize > rawlensize) {
                /* This would result in shrinking, which we want to avoid.
                 * So, set "rawlen" in the available bytes. */
                /* next节点的上一个节点的长度所占的字节数next.prevrawlensize, 
                 * 小于存储当前节点的长度需要的字节数时，这意味着next节点编码前置节点的
                 * header空间有5字节，而编码rawlen只需要1字节，需要缩容。但应该尽量避免这么做。
                 * 所以我们用5字节的空间将1字节的编码重新编码 */
                zipPrevEncodeLengthForceLarge(p+rawlen,rawlen);
            } else {
                // 说明next.prevrawlensize = rawlensize，只需要更新next节点的header
                zipPrevEncodeLength(p+rawlen,rawlen);
            }

            /* Stop here, as the raw length of "next" has not changed. */
            break;
        }
    }
    return zl;
}

/* Delete "num" entries, starting at "p". Returns pointer to the ziplist. */
/* 从p指向的节点开始，删除num个节点。返回ziplist的指针。 */
unsigned char *__ziplistDelete(unsigned char *zl, unsigned char *p, unsigned int num) {
    unsigned int i, totlen, deleted = 0;
    size_t offset;
    int nextdiff = 0;
    zlentry first, tail;

    zipEntry(p, &first);  // p指向第一个要删除的节点
    for (i = 0; p[0] != ZIP_END && i < num; i++) {  // 从p开始遍历num个节点（如果有这么多），统计要删除的节点数量
        p += zipRawEntryLength(p);  // zipRawEntryLength(p)返回p指向的节点所占用的字节数
        deleted++;
    }

    totlen = p-first.p;  // 总的删除长度
    if (totlen > 0) {
        if (p[0] != ZIP_END) {
            /* Storing `prevrawlen` in this entry may increase or decrease the
             * number of bytes required compare to the current `prevrawlen`.
             * There always is room to store this, because it was previously
             * stored by an entry that is now being deleted. */
            /* 如果被删除的最后一个节点不是压缩链表的最后一个节点，说明它后面还有节点A。
             * A节点的header部分的大小可能无法容纳新的前置节点B（被删除的第一个节点的前置节点）
             * 所以这里需要计算这里面的差值。 */
            // first.prevrawlen为被删除的第一个节点的前置节点的长度
            // p指向被删除的最后一个节点的后置节点
            nextdiff = zipPrevLenByteDiff(p,first.prevrawlen);  // 差值
            p -= nextdiff;  // 更新p的指针
            zipPrevEncodeLength(p,first.prevrawlen);  // 更新被删除的最后一个节点的后置节点的prevrawlensize和prevrawlen

            /* Update offset for tail */
            /* 更新表尾偏移量，新的表尾偏移量 = 当前表尾偏移量 - 删除的长度 */
            ZIPLIST_TAIL_OFFSET(zl) =
                intrev32ifbe(intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl))-totlen);

            /* When the tail contains more than one entry, we need to take
             * "nextdiff" in account as well. Otherwise, a change in the
             * size of prevlen doesn't have an effect on the *tail* offset. */
            zipEntry(p, &tail);  //  tail为最后一个删除节点的后置节点
            // 当被删除的最后一个节点后面有多于一个的节点，需要更新ziplist表尾偏移量，加上修正值
            if (p[tail.headersize+tail.len] != ZIP_END) {
                ZIPLIST_TAIL_OFFSET(zl) =
                   intrev32ifbe(intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl))+nextdiff);
            }

            /* Move tail to the front of the ziplist */
            /* 把tail节点之后的数据移动到被删除的第一个节点的位置 */
            memmove(first.p,p,
                intrev32ifbe(ZIPLIST_BYTES(zl))-(p-zl)-1);
        } else {
            /* The entire tail was deleted. No need to move memory. */
            /* 把p指向的节点和其后面的所有节点都删除了，无须移动数据，只需要更新ziplist表尾偏移量 */
            ZIPLIST_TAIL_OFFSET(zl) =
                intrev32ifbe((first.p-zl)-first.prevrawlen);
        }
        
        /* Resize and update length */
        offset = first.p-zl;  // 节点结合处偏移量
        zl = ziplistResize(zl, intrev32ifbe(ZIPLIST_BYTES(zl))-totlen+nextdiff);  // 调整ziplist大小
        ZIPLIST_INCR_LENGTH(zl,-deleted);  // 调整ziplist节点数量
        p = zl+offset;  // 节点结合处指针

        /* When nextdiff != 0, the raw length of the next entry has changed, so
         * we need to cascade the update throughout the ziplist */
        /* 当nextdiff != 0时，结合处的节点将发生变化（前置节点长度prevrawlen会改变），
         * 这里我们需要级联更新ziplist。 */
        if (nextdiff != 0)
            zl = __ziplistCascadeUpdate(zl,p);
    }
    return zl;
}

/* Insert item at "p". */
/* 在p指向的地方插入元素，元素值为s，值长度为slen。 */
unsigned char *__ziplistInsert(unsigned char *zl, unsigned char *p, unsigned char *s, unsigned int slen) {
    // curlen: 压缩链表占用的字节长度
    size_t curlen = intrev32ifbe(ZIPLIST_BYTES(zl)), reqlen;
    // prevlensize: 保存插入位置处节点的前置节点len所需的字节数，prevlen: 插入位置处节点的前置节点长度
    unsigned int prevlensize, prevlen = 0;
    size_t offset;
    int nextdiff = 0;
    unsigned char encoding = 0;
    long long value = 123456789; /* initialized to avoid warning. Using a value
                                    that is easy to see if for some reason
                                    we use it uninitialized. */
    zlentry tail;

    /* Find out prevlen for the entry that is inserted. */
    /* 找出插入位置的前置节点的长度 */
    if (p[0] != ZIP_END) {
        // 获取插入位置处节点的前置节点长度len所需的字节数和前置节点的长度
        ZIP_DECODE_PREVLEN(p, prevlensize, prevlen);
    } else {
        // 插入位置为链表尾
        unsigned char *ptail = ZIPLIST_ENTRY_TAIL(zl);  // ptail为尾节点指针
        if (ptail[0] != ZIP_END) {
            prevlen = zipRawEntryLength(ptail);  // 计算尾节点的前置节点的长度
        }
    }

    /* See if the entry can be encoded */
    /* 检查节点是否可以被编码，并判断编码类型 */
    if (zipTryEncoding(s,slen,&value,&encoding)) {
        /* 'encoding' is set to the appropriate integer encoding */
        /* zipIntSize(encoding)返回编码指定类型整数需要的空间大小 */
        reqlen = zipIntSize(encoding);
    } else {
        /* 'encoding' is untouched, however zipEncodeLength will use the
         * string length to figure out how to encode it. */
        /* 无法用一个整数编码，使用字符串编码，编码长度为入参slen。 */
        reqlen = slen;
    }
    /* We need space for both the length of the previous entry and
     * the length of the payload. */
    /* 计算保存前置节点长度需要的空间和保存值需要的空间大小。 */
    reqlen += zipPrevEncodeLength(NULL,prevlen);
    reqlen += zipEncodeLength(NULL,encoding,slen);

    /* When the insert position is not equal to the tail, we need to
     * make sure that the next entry can hold this entry's length in
     * its prevlen field. */
    /* 当不是在链表尾插入时，我们需要保证插入位置的后置节点的空间能够保存这个
     * 被插入节点的长度。 */
    int forcelarge = 0;
    nextdiff = (p[0] != ZIP_END) ? zipPrevLenByteDiff(p,reqlen) : 0;  // 计算空间差值
    if (nextdiff == -4 && reqlen < 4) {
        nextdiff = 0;
        forcelarge = 1;
    }

    /* Store offset because a realloc may change the address of zl. */
    /* 保存插入位置的偏移量，因为realloc调用有可能会改变ziplist的地址。 */
    offset = p-zl;
    zl = ziplistResize(zl,curlen+reqlen+nextdiff);  // ziplist调整大小
    p = zl+offset;  // 更新插入位置指针

    /* Apply memory move when necessary and update tail offset. */
    /*  */
    if (p[0] != ZIP_END) {  // 不是在链表尾插入
        /* Subtract one because of the ZIP_END bytes */
        /* 新节点长度为reqlen，将新节点后面的节点都移动到新的位置。 */
        memmove(p+reqlen,p-nextdiff,curlen-offset-1+nextdiff);

        /* Encode this entry's raw length in the next entry. */
        /* 在新节点的后置节点中更新前置节点的信息。 */
        if (forcelarge)
            zipPrevEncodeLengthForceLarge(p+reqlen,reqlen);
        else
            zipPrevEncodeLength(p+reqlen,reqlen);

        /* Update offset for tail */
        /* 更新尾节点偏移量，直接在原来的基础加上新节点长度即可 */
        ZIPLIST_TAIL_OFFSET(zl) =
            intrev32ifbe(intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl))+reqlen);

        /* When the tail contains more than one entry, we need to take
         * "nextdiff" in account as well. Otherwise, a change in the
         * size of prevlen doesn't have an effect on the *tail* offset. */
        zipEntry(p+reqlen, &tail);  //  tail为新节点的后置节点
        // 当新节点的后面有多于一个的节点，需要更新ziplist表尾偏移量，加上修正值
        if (p[reqlen+tail.headersize+tail.len] != ZIP_END) {
            ZIPLIST_TAIL_OFFSET(zl) =
                intrev32ifbe(intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl))+nextdiff);
        }
    } else {
        /* This element will be the new tail. */
        // 在链表尾插入，新节点成为新的尾节点，更新尾节点偏移量。
        ZIPLIST_TAIL_OFFSET(zl) = intrev32ifbe(p-zl);
    }

    /* When nextdiff != 0, the raw length of the next entry has changed, so
     * we need to cascade the update throughout the ziplist */
    /* 当nextdiff != 0时，结合处的节点将发生变化（前置节点长度prevrawlen会改变），
     * 这里我们需要级联更新ziplist。 */
    if (nextdiff != 0) {
        offset = p-zl;
        zl = __ziplistCascadeUpdate(zl,p+reqlen);
        p = zl+offset;
    }

    /* Write the entry */
    /* 真正插入节点 */
    p += zipPrevEncodeLength(p,prevlen);  // 新节点存储前置节点长度需要的字节数
    p += zipEncodeLength(p,encoding,slen);  // 新节点编码长度为slen的数据所需要的字节数量
    if (ZIP_IS_STR(encoding)) {  // 字符串型数据
        memcpy(p,s,slen);  // 拷贝数据s到指定位置
    } else {
        zipSaveInteger(p,value,encoding);  // 保存整数
    }
    ZIPLIST_INCR_LENGTH(zl,1);  // 更新链表节点数量
    return zl;
}

/* Merge ziplists 'first' and 'second' by appending 'second' to 'first'.
 *
 * NOTE: The larger ziplist is reallocated to contain the new merged ziplist.
 * Either 'first' or 'second' can be used for the result.  The parameter not
 * used will be free'd and set to NULL.
 *
 * After calling this function, the input parameters are no longer valid since
 * they are changed and free'd in-place.
 *
 * The result ziplist is the contents of 'first' followed by 'second'.
 *
 * On failure: returns NULL if the merge is impossible.
 * On success: returns the merged ziplist (which is expanded version of either
 * 'first' or 'second', also frees the other unused input ziplist, and sets the
 * input ziplist argument equal to newly reallocated ziplist return value. */
/* 合并两个ziplist，把第一个ziplist和第二个ziplist首尾相连 */
unsigned char *ziplistMerge(unsigned char **first, unsigned char **second) {
    /* If any params are null, we can't merge, so NULL. */
    /* 如果所有参数都是NULL，无须合并，直接返回NULL。 */
    if (first == NULL || *first == NULL || second == NULL || *second == NULL)
        return NULL;

    /* Can't merge same list into itself. */
    /* 如果两个ziplist是同一个，也无法合并。 */
    if (*first == *second)
        return NULL;

    // 第1个ziplist占用的空间大小和节点数量
    size_t first_bytes = intrev32ifbe(ZIPLIST_BYTES(*first));
    size_t first_len = intrev16ifbe(ZIPLIST_LENGTH(*first));

    // 第2个ziplist占用的空间大小和节点数量
    size_t second_bytes = intrev32ifbe(ZIPLIST_BYTES(*second));
    size_t second_len = intrev16ifbe(ZIPLIST_LENGTH(*second));

    int append;
    unsigned char *source, *target;
    size_t target_bytes, source_bytes;
    /* Pick the largest ziplist so we can resize easily in-place.
     * We must also track if we are now appending or prepending to
     * the target ziplist. */
    /* 选择比较大的那个ziplist，这样直接就地扩容比较容易。
     *  */
    if (first_len >= second_len) {
        /* retain first, append second to first. */
        /* 以第一个ziplist为target，把第二个ziplist追加到它后面。 */
        target = *first;
        target_bytes = first_bytes;
        source = *second;
        source_bytes = second_bytes;
        append = 1;  // 后向追加
    } else {
        /* else, retain second, prepend first to second. */
        /* 以第二个ziplist为target，把第一个ziplist前向追加到它上面。 */
        target = *second;
        target_bytes = second_bytes;
        source = *first;
        source_bytes = first_bytes;
        append = 0;  // 前向追加
    }

    /* Calculate final bytes (subtract one pair of metadata) */
    /* 计算合并后的ziplist占用的空间大小，需要扣除其中一个ziplist的元数据（zip_header和zip_end）的大小 */
    size_t zlbytes = first_bytes + second_bytes -
                     ZIPLIST_HEADER_SIZE - ZIPLIST_END_SIZE;
    size_t zllength = first_len + second_len;  // 合并后的ziplist节点数量

    /* Combined zl length should be limited within UINT16_MAX */
    /* 合并后的ziplist节点数量必须限制在UINT16_MAX之内 */
    zllength = zllength < UINT16_MAX ? zllength : UINT16_MAX;

    /* Save offset positions before we start ripping memory apart. */
    /* 在操作内存之前先保存两个ziplist的尾节点偏移量。 */
    size_t first_offset = intrev32ifbe(ZIPLIST_TAIL_OFFSET(*first));
    size_t second_offset = intrev32ifbe(ZIPLIST_TAIL_OFFSET(*second));

    /* Extend target to new zlbytes then append or prepend source. */
    /* realloc目标ziplist的空间。 */
    target = zrealloc(target, zlbytes);
    if (append) {
        /* append == appending to target */
        /* Copy source after target (copying over original [END]):
         *   [TARGET - END, SOURCE - HEADER] */
        /* target = ziplist_1 <- ziplist_2 */
        memcpy(target + target_bytes - ZIPLIST_END_SIZE,
               source + ZIPLIST_HEADER_SIZE,
               source_bytes - ZIPLIST_HEADER_SIZE);
    } else {
        /* !append == prepending to target */
        /* Move target *contents* exactly size of (source - [END]),
         * then copy source into vacataed space (source - [END]):
         *   [SOURCE - END, TARGET - HEADER] */
        /* target = ziplist_1 -> ziplist_2 */
        memmove(target + source_bytes - ZIPLIST_END_SIZE,
                target + ZIPLIST_HEADER_SIZE,
                target_bytes - ZIPLIST_HEADER_SIZE);
        memcpy(target, source, source_bytes - ZIPLIST_END_SIZE);
    }

    /* Update header metadata. */
    /* 更新目标ziplist header元数据 */
    ZIPLIST_BYTES(target) = intrev32ifbe(zlbytes);  // 更新目标ziplist占用的字节数
    ZIPLIST_LENGTH(target) = intrev16ifbe(zllength);  // 更新目标ziplist节点数量
    /* New tail offset is:
     *   + N bytes of first ziplist
     *   - 1 byte for [END] of first ziplist
     *   + M bytes for the offset of the original tail of the second ziplist
     *   - J bytes for HEADER because second_offset keeps no header. */
    /* 新的尾节点偏移量计算方式：
     *   + N 字节：第一个ziplist的总字节数
     *   - 1 字节：第一个ziplist的ZIP_END
     *   + M 字节：第二个ziplist原来的尾节点偏移量
     *   - J 字节：第二个ziplist的header的字节数 */
    ZIPLIST_TAIL_OFFSET(target) = intrev32ifbe(
                                   (first_bytes - ZIPLIST_END_SIZE) +
                                   (second_offset - ZIPLIST_HEADER_SIZE));

    /* __ziplistCascadeUpdate just fixes the prev length values until it finds a
     * correct prev length value (then it assumes the rest of the list is okay).
     * We tell CascadeUpdate to start at the first ziplist's tail element to fix
     * the merge seam. */
    /* 在接合处级联更新目标ziplist */
    target = __ziplistCascadeUpdate(target, target+first_offset);

    /* Now free and NULL out what we didn't realloc */
    if (append) {
        // target = ziplist_1 <- ziplist_2，释放第二个ziplist的空间，并更新ziplist指针
        zfree(*second);
        *second = NULL;
        *first = target;
    } else {
        // target = ziplist_1 -> ziplist_2，释放第一个ziplist的空间，并更新ziplist指针
        zfree(*first);
        *first = NULL;
        *second = target;
    }
    return target;
}

/* 向ziplist中插入元素，只能在首尾添加。 */
unsigned char *ziplistPush(unsigned char *zl, unsigned char *s, unsigned int slen, int where) {
    unsigned char *p;
    p = (where == ZIPLIST_HEAD) ? ZIPLIST_ENTRY_HEAD(zl) : ZIPLIST_ENTRY_END(zl);  // 获取插入位置指针
    return __ziplistInsert(zl,p,s,slen);
}

/* Returns an offset to use for iterating with ziplistNext. When the given
 * index is negative, the list is traversed back to front. When the list
 * doesn't contain an element at the provided index, NULL is returned. */
/* 根据给定的索引值返回一个节点的指针。当给定的索引值为负时，从后向前遍历。
 * 当链表在给定的索引值上没有节点时返回NULL。 */
unsigned char *ziplistIndex(unsigned char *zl, int index) {
    unsigned char *p;
    unsigned int prevlensize, prevlen = 0;
    if (index < 0) {
        // 从后向前遍历链表
        index = (-index)-1;
        p = ZIPLIST_ENTRY_TAIL(zl);  // 尾节点指针
        if (p[0] != ZIP_END) {
            ZIP_DECODE_PREVLEN(p, prevlensize, prevlen);
            while (prevlen > 0 && index--) {
                p -= prevlen;
                ZIP_DECODE_PREVLEN(p, prevlensize, prevlen);
            }
        }
    } else {
        // 从前向后遍历链表
        p = ZIPLIST_ENTRY_HEAD(zl);  // 头节点指针
        while (p[0] != ZIP_END && index--) {
            p += zipRawEntryLength(p);
        }
    }
    return (p[0] == ZIP_END || index > 0) ? NULL : p;  // 没有找到相应的节点，返回NULL，否则返回这个节点的指针
}

/* Return pointer to next entry in ziplist.
 *
 * zl is the pointer to the ziplist
 * p is the pointer to the current element
 *
 * The element after 'p' is returned, otherwise NULL if we are at the end. */
/* 返回ziplist中当前节点的后置节点指针，如果当前节点是尾节点则返回NULL。 */
unsigned char *ziplistNext(unsigned char *zl, unsigned char *p) {
    ((void) zl);

    /* "p" could be equal to ZIP_END, caused by ziplistDelete,
     * and we should return NULL. Otherwise, we should return NULL
     * when the *next* element is ZIP_END (there is no next entry). */
    /* 由于调用ziplistDelete函数，p有可能等于ZIP_END，
     * 这时应该返回NULL。否则，当后置节点为ZIP_END时返回NULL。 */
    if (p[0] == ZIP_END) {
        return NULL;
    }

    p += zipRawEntryLength(p);  // p加上当前节点长度为它的后置节点的地址
    if (p[0] == ZIP_END) {
        return NULL;
    }

    return p;
}

/* Return pointer to previous entry in ziplist. */
/* 返回ziplist当前节点的前置节点指针。 */
unsigned char *ziplistPrev(unsigned char *zl, unsigned char *p) {
    unsigned int prevlensize, prevlen = 0;

    /* Iterating backwards from ZIP_END should return the tail. When "p" is
     * equal to the first element of the list, we're already at the head,
     * and should return NULL. */
    /* 从ZIP_END开始向前迭代会返回尾节点。当p指向链表头节点时，返回NULL。 */
    if (p[0] == ZIP_END) {
        // p指向ZIP_END时，返回链表尾节点
        p = ZIPLIST_ENTRY_TAIL(zl);
        return (p[0] == ZIP_END) ? NULL : p;
    } else if (p == ZIPLIST_ENTRY_HEAD(zl)) {
        // p指向链表头节点时，返回NULL
        return NULL;
    } else {
        // 获得p指向节点的前置节点长度，p减该长度即为当前节点前置节点
        ZIP_DECODE_PREVLEN(p, prevlensize, prevlen);
        assert(prevlen > 0);
        return p-prevlen;
    }
}

/* Get entry pointed to by 'p' and store in either '*sstr' or 'sval' depending
 * on the encoding of the entry. '*sstr' is always set to NULL to be able
 * to find out whether the string pointer or the integer value was set.
 * Return 0 if 'p' points to the end of the ziplist, 1 otherwise. */
/* 获取p指向的节点的数据，根据其编码决定数据保存在*sstr（字符串）还是sval（整数）中。
 * *sstr刚开始总是被设置为NULL。当p指向ziplist的尾部（ZIP_END）时返回0，否则返回1。 */
unsigned int ziplistGet(unsigned char *p, unsigned char **sstr, unsigned int *slen, long long *sval) {
    zlentry entry;
    if (p == NULL || p[0] == ZIP_END) return 0;
    if (sstr) *sstr = NULL;

    zipEntry(p, &entry);  // 初始化entry为当前节点
    if (ZIP_IS_STR(entry.encoding)) {  // 当前节点为字符串，数据保存在*sstr
        if (sstr) {
            *slen = entry.len;
            *sstr = p+entry.headersize;
        }
    } else {  // 当前节点为整数，数据保存在*sval
        if (sval) {
            *sval = zipLoadInteger(p+entry.headersize,entry.encoding);
        }
    }
    return 1;
}

/* Insert an entry at "p". */
/* 向ziplist中p指向的节点处插入一个节点 */
unsigned char *ziplistInsert(unsigned char *zl, unsigned char *p, unsigned char *s, unsigned int slen) {
    return __ziplistInsert(zl,p,s,slen);
}

/* Delete a single entry from the ziplist, pointed to by *p.
 * Also update *p in place, to be able to iterate over the
 * ziplist, while deleting entries. */
/* 从ziplist中删除p指向的节点。还就地更新了*p，以使得在删除节点的时候还能迭代ziplist。 */
unsigned char *ziplistDelete(unsigned char *zl, unsigned char **p) {
    size_t offset = *p-zl;  // 当前节点的偏移量
    zl = __ziplistDelete(zl,*p,1);  // 从ziplist中删除当前节点，由于ziplistDelete会调用realloc，zl有可能会发生变化

    /* Store pointer to current element in p, because ziplistDelete will
     * do a realloc which might result in a different "zl"-pointer.
     * When the delete direction is back to front, we might delete the last
     * entry and end up with "p" pointing to ZIP_END, so check this. */
    /* 事先在p中保存当前元素的指针，因为ziplistDelete会调用realloc，有可能会导致zl指针发生变化。 */
    *p = zl+offset;  // 更新了*p，此时*p指向的是被删除节点的后置节点，可以继续使用这个指针进行迭代。
    return zl;
}

/* Delete a range of entries from the ziplist. */
/* 删除ziplist中一个范围内的节点 */
unsigned char *ziplistDeleteRange(unsigned char *zl, int index, unsigned int num) {
    unsigned char *p = ziplistIndex(zl,index);
    return (p == NULL) ? zl : __ziplistDelete(zl,p,num);
}

/* Compare entry pointer to by 'p' with 'sstr' of length 'slen'. */
/* Return 1 if equal. */
/* 比较p指向节点的值和sstr指向的长度为slen的数据，当相等时返回1，否则返回0。 */
unsigned int ziplistCompare(unsigned char *p, unsigned char *sstr, unsigned int slen) {
    zlentry entry;
    unsigned char sencoding;
    long long zval, sval;
    if (p[0] == ZIP_END) return 0;

    zipEntry(p, &entry);  // entry为p指向的节点
    if (ZIP_IS_STR(entry.encoding)) {
        /* Raw compare */
        /* entry的值是字符串 */
        if (entry.len == slen) {
            return memcmp(p+entry.headersize,sstr,slen) == 0;
        } else {
            return 0;
        }
    } else {
        /* Try to compare encoded values. Don't compare encoding because
         * different implementations may encoded integers differently. */
        /* entry的值是整数，此时不比较编码类型，因为不同编码类型的位数不同，只比较值是否相等。 */
        if (zipTryEncoding(sstr,slen,&sval,&sencoding)) {
          zval = zipLoadInteger(p+entry.headersize,entry.encoding);
          return zval == sval;
        }
    }
    return 0;
}

/* Find pointer to the entry equal to the specified entry. Skip 'skip' entries
 * between every comparison. Returns NULL when the field could not be found. */
/* 在ziplist中查找与指定节点相等的节点。每次比较后跳过skip个节点。
 * 没有找到相应节点时返回NULL。 */
unsigned char *ziplistFind(unsigned char *p, unsigned char *vstr, unsigned int vlen, unsigned int skip) {
    int skipcnt = 0;  // 已经跳过的节点数
    unsigned char vencoding = 0;
    long long vll = 0;

    while (p[0] != ZIP_END) {
        unsigned int prevlensize, encoding, lensize, len;
        unsigned char *q;

        ZIP_DECODE_PREVLENSIZE(p, prevlensize);  // 保存当前节点的前置节点长度所需的字节数
        ZIP_DECODE_LENGTH(p + prevlensize, encoding, lensize, len);  // 获取当前节点的encoding、lensize和len

        q = p + prevlensize + lensize;  // 当前节点value域指针

        if (skipcnt == 0) {
            /* Compare current entry with specified entry */
            /* 比较当前节点和给定节点的值 */
            if (ZIP_IS_STR(encoding)) {
                // 当前节点的值为字符串
                if (len == vlen && memcmp(q, vstr, vlen) == 0) {
                    return p;
                }
            } else {
                /* Find out if the searched field can be encoded. Note that
                 * we do it only the first time, once done vencoding is set
                 * to non-zero and vll is set to the integer value. */
                /* 判断vstr指向的数据能否被编码成整数，这个操作只做一次，
                 * 一旦判定为可以被编码成整数，vencoding被设置为非0值且vll被设置成一对应的整数。
                 * 如果不能，vencoding被设置为UCHAR_MAX。 */
                if (vencoding == 0) {
                    if (!zipTryEncoding(vstr, vlen, &vll, &vencoding)) {
                        /* If the entry can't be encoded we set it to
                         * UCHAR_MAX so that we don't retry again the next
                         * time. */
                        vencoding = UCHAR_MAX;
                    }
                    /* Must be non-zero by now */
                    assert(vencoding);
                }

                /* Compare current entry with specified entry, do it only
                 * if vencoding != UCHAR_MAX because if there is no encoding
                 * possible for the field it can't be a valid integer. */
                /* 只有当vencoding != UCHAR_MAX时才能以整数比较当前节点和给定节点的值。 */
                if (vencoding != UCHAR_MAX) {  // vstr指向的值可以被以整数编码
                    long long ll = zipLoadInteger(q, encoding);
                    if (ll == vll) {
                        return p;
                    }
                }
            }

            /* Reset skip count */
            skipcnt = skip;
        } else {
            /* Skip entry */
            skipcnt--;
        }

        /* Move to next entry */
        /* 移动到下个节点 */
        p = q + len;
    }

    return NULL;
}

/* Return length of ziplist. */
/* 返回ziplist的节点数量 */
unsigned int ziplistLen(unsigned char *zl) {
    unsigned int len = 0;
    if (intrev16ifbe(ZIPLIST_LENGTH(zl)) < UINT16_MAX) {
        // 如果ziplist的节点数量小于UINT16_MAX，直接取ziplist header中存放的节点数量
        len = intrev16ifbe(ZIPLIST_LENGTH(zl));
    } else {
        unsigned char *p = zl+ZIPLIST_HEADER_SIZE;  // ziplist头节点指针
        while (*p != ZIP_END) {  // 遍历ziplist计算节点数量
            p += zipRawEntryLength(p);
            len++;
        }

        /* Re-store length if small enough */
        /* 如果实际计算出来的长度小于UINT16_MAX，更新ziplist header中的节点数量 */
        if (len < UINT16_MAX) ZIPLIST_LENGTH(zl) = intrev16ifbe(len);
    }
    return len;
}

/* Return ziplist blob size in bytes. */
/* 获取链表占用的总字节数。 */
size_t ziplistBlobLen(unsigned char *zl) {
    return intrev32ifbe(ZIPLIST_BYTES(zl));
}

/* ziplist信息可读化输出 */
void ziplistRepr(unsigned char *zl) {
    unsigned char *p;
    int index = 0;
    zlentry entry;

    printf(
        "{total bytes %d} "
        "{length %u}\n"
        "{tail offset %u}\n",
        intrev32ifbe(ZIPLIST_BYTES(zl)),
        intrev16ifbe(ZIPLIST_LENGTH(zl)),
        intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl)));
    p = ZIPLIST_ENTRY_HEAD(zl);
    while(*p != ZIP_END) {
        zipEntry(p, &entry);
        printf(
            "{"
                "addr 0x%08lx, "
                "index %2d, "
                "offset %5ld, "
                "rl: %5u, "
                "hs %2u, "
                "pl: %5u, "
                "pls: %2u, "
                "payload %5u"
            "} ",
            (long unsigned)p,
            index,
            (unsigned long) (p-zl),
            entry.headersize+entry.len,
            entry.headersize,
            entry.prevrawlen,
            entry.prevrawlensize,
            entry.len);
        p += entry.headersize;
        if (ZIP_IS_STR(entry.encoding)) {
            if (entry.len > 40) {
                if (fwrite(p,40,1,stdout) == 0) perror("fwrite");
                printf("...");
            } else {
                if (entry.len &&
                    fwrite(p,entry.len,1,stdout) == 0) perror("fwrite");
            }
        } else {
            printf("%lld", (long long) zipLoadInteger(p,entry.encoding));
        }
        printf("\n");
        p += entry.len;
        index++;
    }
    printf("{end}\n\n");
}

#ifdef REDIS_TEST
#include <sys/time.h>
#include "adlist.h"
#include "sds.h"

#define debug(f, ...) { if (DEBUG) printf(f, __VA_ARGS__); }

static unsigned char *createList() {
    unsigned char *zl = ziplistNew();
    zl = ziplistPush(zl, (unsigned char*)"foo", 3, ZIPLIST_TAIL);
    zl = ziplistPush(zl, (unsigned char*)"quux", 4, ZIPLIST_TAIL);
    zl = ziplistPush(zl, (unsigned char*)"hello", 5, ZIPLIST_HEAD);
    zl = ziplistPush(zl, (unsigned char*)"1024", 4, ZIPLIST_TAIL);
    return zl;
}

static unsigned char *createIntList() {
    unsigned char *zl = ziplistNew();
    char buf[32];

    sprintf(buf, "100");
    zl = ziplistPush(zl, (unsigned char*)buf, strlen(buf), ZIPLIST_TAIL);
    sprintf(buf, "128000");
    zl = ziplistPush(zl, (unsigned char*)buf, strlen(buf), ZIPLIST_TAIL);
    sprintf(buf, "-100");
    zl = ziplistPush(zl, (unsigned char*)buf, strlen(buf), ZIPLIST_HEAD);
    sprintf(buf, "4294967296");
    zl = ziplistPush(zl, (unsigned char*)buf, strlen(buf), ZIPLIST_HEAD);
    sprintf(buf, "non integer");
    zl = ziplistPush(zl, (unsigned char*)buf, strlen(buf), ZIPLIST_TAIL);
    sprintf(buf, "much much longer non integer");
    zl = ziplistPush(zl, (unsigned char*)buf, strlen(buf), ZIPLIST_TAIL);
    return zl;
}

static long long usec(void) {
    struct timeval tv;
    gettimeofday(&tv,NULL);
    return (((long long)tv.tv_sec)*1000000)+tv.tv_usec;
}

static void stress(int pos, int num, int maxsize, int dnum) {
    int i,j,k;
    unsigned char *zl;
    char posstr[2][5] = { "HEAD", "TAIL" };
    long long start;
    for (i = 0; i < maxsize; i+=dnum) {
        zl = ziplistNew();
        for (j = 0; j < i; j++) {
            zl = ziplistPush(zl,(unsigned char*)"quux",4,ZIPLIST_TAIL);
        }

        /* Do num times a push+pop from pos */
        start = usec();
        for (k = 0; k < num; k++) {
            zl = ziplistPush(zl,(unsigned char*)"quux",4,pos);
            zl = ziplistDeleteRange(zl,0,1);
        }
        printf("List size: %8d, bytes: %8d, %dx push+pop (%s): %6lld usec\n",
            i,intrev32ifbe(ZIPLIST_BYTES(zl)),num,posstr[pos],usec()-start);
        zfree(zl);
    }
}

static unsigned char *pop(unsigned char *zl, int where) {
    unsigned char *p, *vstr;
    unsigned int vlen;
    long long vlong;

    p = ziplistIndex(zl,where == ZIPLIST_HEAD ? 0 : -1);
    if (ziplistGet(p,&vstr,&vlen,&vlong)) {
        if (where == ZIPLIST_HEAD)
            printf("Pop head: ");
        else
            printf("Pop tail: ");

        if (vstr) {
            if (vlen && fwrite(vstr,vlen,1,stdout) == 0) perror("fwrite");
        }
        else {
            printf("%lld", vlong);
        }

        printf("\n");
        return ziplistDelete(zl,&p);
    } else {
        printf("ERROR: Could not pop\n");
        exit(1);
    }
}

static int randstring(char *target, unsigned int min, unsigned int max) {
    int p = 0;
    int len = min+rand()%(max-min+1);
    int minval, maxval;
    switch(rand() % 3) {
    case 0:
        minval = 0;
        maxval = 255;
    break;
    case 1:
        minval = 48;
        maxval = 122;
    break;
    case 2:
        minval = 48;
        maxval = 52;
    break;
    default:
        assert(NULL);
    }

    while(p < len)
        target[p++] = minval+rand()%(maxval-minval+1);
    return len;
}

static void verify(unsigned char *zl, zlentry *e) {
    int len = ziplistLen(zl);
    zlentry _e;

    ZIPLIST_ENTRY_ZERO(&_e);

    for (int i = 0; i < len; i++) {
        memset(&e[i], 0, sizeof(zlentry));
        zipEntry(ziplistIndex(zl, i), &e[i]);

        memset(&_e, 0, sizeof(zlentry));
        zipEntry(ziplistIndex(zl, -len+i), &_e);

        assert(memcmp(&e[i], &_e, sizeof(zlentry)) == 0);
    }
}

int ziplistTest(int argc, char **argv) {
    unsigned char *zl, *p;
    unsigned char *entry;
    unsigned int elen;
    long long value;

    /* If an argument is given, use it as the random seed. */
    if (argc == 2)
        srand(atoi(argv[1]));

    zl = createIntList();
    ziplistRepr(zl);

    zfree(zl);

    zl = createList();
    ziplistRepr(zl);

    zl = pop(zl,ZIPLIST_TAIL);
    ziplistRepr(zl);

    zl = pop(zl,ZIPLIST_HEAD);
    ziplistRepr(zl);

    zl = pop(zl,ZIPLIST_TAIL);
    ziplistRepr(zl);

    zl = pop(zl,ZIPLIST_TAIL);
    ziplistRepr(zl);

    zfree(zl);

    printf("Get element at index 3:\n");
    {
        zl = createList();
        p = ziplistIndex(zl, 3);
        if (!ziplistGet(p, &entry, &elen, &value)) {
            printf("ERROR: Could not access index 3\n");
            return 1;
        }
        if (entry) {
            if (elen && fwrite(entry,elen,1,stdout) == 0) perror("fwrite");
            printf("\n");
        } else {
            printf("%lld\n", value);
        }
        printf("\n");
        zfree(zl);
    }

    printf("Get element at index 4 (out of range):\n");
    {
        zl = createList();
        p = ziplistIndex(zl, 4);
        if (p == NULL) {
            printf("No entry\n");
        } else {
            printf("ERROR: Out of range index should return NULL, returned offset: %ld\n", p-zl);
            return 1;
        }
        printf("\n");
        zfree(zl);
    }

    printf("Get element at index -1 (last element):\n");
    {
        zl = createList();
        p = ziplistIndex(zl, -1);
        if (!ziplistGet(p, &entry, &elen, &value)) {
            printf("ERROR: Could not access index -1\n");
            return 1;
        }
        if (entry) {
            if (elen && fwrite(entry,elen,1,stdout) == 0) perror("fwrite");
            printf("\n");
        } else {
            printf("%lld\n", value);
        }
        printf("\n");
        zfree(zl);
    }

    printf("Get element at index -4 (first element):\n");
    {
        zl = createList();
        p = ziplistIndex(zl, -4);
        if (!ziplistGet(p, &entry, &elen, &value)) {
            printf("ERROR: Could not access index -4\n");
            return 1;
        }
        if (entry) {
            if (elen && fwrite(entry,elen,1,stdout) == 0) perror("fwrite");
            printf("\n");
        } else {
            printf("%lld\n", value);
        }
        printf("\n");
        zfree(zl);
    }

    printf("Get element at index -5 (reverse out of range):\n");
    {
        zl = createList();
        p = ziplistIndex(zl, -5);
        if (p == NULL) {
            printf("No entry\n");
        } else {
            printf("ERROR: Out of range index should return NULL, returned offset: %ld\n", p-zl);
            return 1;
        }
        printf("\n");
        zfree(zl);
    }

    printf("Iterate list from 0 to end:\n");
    {
        zl = createList();
        p = ziplistIndex(zl, 0);
        while (ziplistGet(p, &entry, &elen, &value)) {
            printf("Entry: ");
            if (entry) {
                if (elen && fwrite(entry,elen,1,stdout) == 0) perror("fwrite");
            } else {
                printf("%lld", value);
            }
            p = ziplistNext(zl,p);
            printf("\n");
        }
        printf("\n");
        zfree(zl);
    }

    printf("Iterate list from 1 to end:\n");
    {
        zl = createList();
        p = ziplistIndex(zl, 1);
        while (ziplistGet(p, &entry, &elen, &value)) {
            printf("Entry: ");
            if (entry) {
                if (elen && fwrite(entry,elen,1,stdout) == 0) perror("fwrite");
            } else {
                printf("%lld", value);
            }
            p = ziplistNext(zl,p);
            printf("\n");
        }
        printf("\n");
        zfree(zl);
    }

    printf("Iterate list from 2 to end:\n");
    {
        zl = createList();
        p = ziplistIndex(zl, 2);
        while (ziplistGet(p, &entry, &elen, &value)) {
            printf("Entry: ");
            if (entry) {
                if (elen && fwrite(entry,elen,1,stdout) == 0) perror("fwrite");
            } else {
                printf("%lld", value);
            }
            p = ziplistNext(zl,p);
            printf("\n");
        }
        printf("\n");
        zfree(zl);
    }

    printf("Iterate starting out of range:\n");
    {
        zl = createList();
        p = ziplistIndex(zl, 4);
        if (!ziplistGet(p, &entry, &elen, &value)) {
            printf("No entry\n");
        } else {
            printf("ERROR\n");
        }
        printf("\n");
        zfree(zl);
    }

    printf("Iterate from back to front:\n");
    {
        zl = createList();
        p = ziplistIndex(zl, -1);
        while (ziplistGet(p, &entry, &elen, &value)) {
            printf("Entry: ");
            if (entry) {
                if (elen && fwrite(entry,elen,1,stdout) == 0) perror("fwrite");
            } else {
                printf("%lld", value);
            }
            p = ziplistPrev(zl,p);
            printf("\n");
        }
        printf("\n");
        zfree(zl);
    }

    printf("Iterate from back to front, deleting all items:\n");
    {
        zl = createList();
        p = ziplistIndex(zl, -1);
        while (ziplistGet(p, &entry, &elen, &value)) {
            printf("Entry: ");
            if (entry) {
                if (elen && fwrite(entry,elen,1,stdout) == 0) perror("fwrite");
            } else {
                printf("%lld", value);
            }
            zl = ziplistDelete(zl,&p);
            p = ziplistPrev(zl,p);
            printf("\n");
        }
        printf("\n");
        zfree(zl);
    }

    printf("Delete inclusive range 0,0:\n");
    {
        zl = createList();
        zl = ziplistDeleteRange(zl, 0, 1);
        ziplistRepr(zl);
        zfree(zl);
    }

    printf("Delete inclusive range 0,1:\n");
    {
        zl = createList();
        zl = ziplistDeleteRange(zl, 0, 2);
        ziplistRepr(zl);
        zfree(zl);
    }

    printf("Delete inclusive range 1,2:\n");
    {
        zl = createList();
        zl = ziplistDeleteRange(zl, 1, 2);
        ziplistRepr(zl);
        zfree(zl);
    }

    printf("Delete with start index out of range:\n");
    {
        zl = createList();
        zl = ziplistDeleteRange(zl, 5, 1);
        ziplistRepr(zl);
        zfree(zl);
    }

    printf("Delete with num overflow:\n");
    {
        zl = createList();
        zl = ziplistDeleteRange(zl, 1, 5);
        ziplistRepr(zl);
        zfree(zl);
    }

    printf("Delete foo while iterating:\n");
    {
        zl = createList();
        p = ziplistIndex(zl,0);
        while (ziplistGet(p,&entry,&elen,&value)) {
            if (entry && strncmp("foo",(char*)entry,elen) == 0) {
                printf("Delete foo\n");
                zl = ziplistDelete(zl,&p);
            } else {
                printf("Entry: ");
                if (entry) {
                    if (elen && fwrite(entry,elen,1,stdout) == 0)
                        perror("fwrite");
                } else {
                    printf("%lld",value);
                }
                p = ziplistNext(zl,p);
                printf("\n");
            }
        }
        printf("\n");
        ziplistRepr(zl);
        zfree(zl);
    }

    printf("Regression test for >255 byte strings:\n");
    {
        char v1[257] = {0}, v2[257] = {0};
        memset(v1,'x',256);
        memset(v2,'y',256);
        zl = ziplistNew();
        zl = ziplistPush(zl,(unsigned char*)v1,strlen(v1),ZIPLIST_TAIL);
        zl = ziplistPush(zl,(unsigned char*)v2,strlen(v2),ZIPLIST_TAIL);

        /* Pop values again and compare their value. */
        p = ziplistIndex(zl,0);
        assert(ziplistGet(p,&entry,&elen,&value));
        assert(strncmp(v1,(char*)entry,elen) == 0);
        p = ziplistIndex(zl,1);
        assert(ziplistGet(p,&entry,&elen,&value));
        assert(strncmp(v2,(char*)entry,elen) == 0);
        printf("SUCCESS\n\n");
        zfree(zl);
    }

    printf("Regression test deleting next to last entries:\n");
    {
        char v[3][257] = {{0}};
        zlentry e[3] = {{.prevrawlensize = 0, .prevrawlen = 0, .lensize = 0,
                         .len = 0, .headersize = 0, .encoding = 0, .p = NULL}};
        size_t i;

        for (i = 0; i < (sizeof(v)/sizeof(v[0])); i++) {
            memset(v[i], 'a' + i, sizeof(v[0]));
        }

        v[0][256] = '\0';
        v[1][  1] = '\0';
        v[2][256] = '\0';

        zl = ziplistNew();
        for (i = 0; i < (sizeof(v)/sizeof(v[0])); i++) {
            zl = ziplistPush(zl, (unsigned char *) v[i], strlen(v[i]), ZIPLIST_TAIL);
        }

        verify(zl, e);

        assert(e[0].prevrawlensize == 1);
        assert(e[1].prevrawlensize == 5);
        assert(e[2].prevrawlensize == 1);

        /* Deleting entry 1 will increase `prevrawlensize` for entry 2 */
        unsigned char *p = e[1].p;
        zl = ziplistDelete(zl, &p);

        verify(zl, e);

        assert(e[0].prevrawlensize == 1);
        assert(e[1].prevrawlensize == 5);

        printf("SUCCESS\n\n");
        zfree(zl);
    }

    printf("Create long list and check indices:\n");
    {
        zl = ziplistNew();
        char buf[32];
        int i,len;
        for (i = 0; i < 1000; i++) {
            len = sprintf(buf,"%d",i);
            zl = ziplistPush(zl,(unsigned char*)buf,len,ZIPLIST_TAIL);
        }
        for (i = 0; i < 1000; i++) {
            p = ziplistIndex(zl,i);
            assert(ziplistGet(p,NULL,NULL,&value));
            assert(i == value);

            p = ziplistIndex(zl,-i-1);
            assert(ziplistGet(p,NULL,NULL,&value));
            assert(999-i == value);
        }
        printf("SUCCESS\n\n");
        zfree(zl);
    }

    printf("Compare strings with ziplist entries:\n");
    {
        zl = createList();
        p = ziplistIndex(zl,0);
        if (!ziplistCompare(p,(unsigned char*)"hello",5)) {
            printf("ERROR: not \"hello\"\n");
            return 1;
        }
        if (ziplistCompare(p,(unsigned char*)"hella",5)) {
            printf("ERROR: \"hella\"\n");
            return 1;
        }

        p = ziplistIndex(zl,3);
        if (!ziplistCompare(p,(unsigned char*)"1024",4)) {
            printf("ERROR: not \"1024\"\n");
            return 1;
        }
        if (ziplistCompare(p,(unsigned char*)"1025",4)) {
            printf("ERROR: \"1025\"\n");
            return 1;
        }
        printf("SUCCESS\n\n");
        zfree(zl);
    }

    printf("Merge test:\n");
    {
        /* create list gives us: [hello, foo, quux, 1024] */
        zl = createList();
        unsigned char *zl2 = createList();

        unsigned char *zl3 = ziplistNew();
        unsigned char *zl4 = ziplistNew();

        if (ziplistMerge(&zl4, &zl4)) {
            printf("ERROR: Allowed merging of one ziplist into itself.\n");
            return 1;
        }

        /* Merge two empty ziplists, get empty result back. */
        zl4 = ziplistMerge(&zl3, &zl4);
        ziplistRepr(zl4);
        if (ziplistLen(zl4)) {
            printf("ERROR: Merging two empty ziplists created entries.\n");
            return 1;
        }
        zfree(zl4);

        zl2 = ziplistMerge(&zl, &zl2);
        /* merge gives us: [hello, foo, quux, 1024, hello, foo, quux, 1024] */
        ziplistRepr(zl2);

        if (ziplistLen(zl2) != 8) {
            printf("ERROR: Merged length not 8, but: %u\n", ziplistLen(zl2));
            return 1;
        }

        p = ziplistIndex(zl2,0);
        if (!ziplistCompare(p,(unsigned char*)"hello",5)) {
            printf("ERROR: not \"hello\"\n");
            return 1;
        }
        if (ziplistCompare(p,(unsigned char*)"hella",5)) {
            printf("ERROR: \"hella\"\n");
            return 1;
        }

        p = ziplistIndex(zl2,3);
        if (!ziplistCompare(p,(unsigned char*)"1024",4)) {
            printf("ERROR: not \"1024\"\n");
            return 1;
        }
        if (ziplistCompare(p,(unsigned char*)"1025",4)) {
            printf("ERROR: \"1025\"\n");
            return 1;
        }

        p = ziplistIndex(zl2,4);
        if (!ziplistCompare(p,(unsigned char*)"hello",5)) {
            printf("ERROR: not \"hello\"\n");
            return 1;
        }
        if (ziplistCompare(p,(unsigned char*)"hella",5)) {
            printf("ERROR: \"hella\"\n");
            return 1;
        }

        p = ziplistIndex(zl2,7);
        if (!ziplistCompare(p,(unsigned char*)"1024",4)) {
            printf("ERROR: not \"1024\"\n");
            return 1;
        }
        if (ziplistCompare(p,(unsigned char*)"1025",4)) {
            printf("ERROR: \"1025\"\n");
            return 1;
        }
        printf("SUCCESS\n\n");
        zfree(zl);
    }

    printf("Stress with random payloads of different encoding:\n");
    {
        int i,j,len,where;
        unsigned char *p;
        char buf[1024];
        int buflen;
        list *ref;
        listNode *refnode;

        /* Hold temp vars from ziplist */
        unsigned char *sstr;
        unsigned int slen;
        long long sval;

        for (i = 0; i < 20000; i++) {
            zl = ziplistNew();
            ref = listCreate();
            listSetFreeMethod(ref,(void (*)(void*))sdsfree);
            len = rand() % 256;

            /* Create lists */
            for (j = 0; j < len; j++) {
                where = (rand() & 1) ? ZIPLIST_HEAD : ZIPLIST_TAIL;
                if (rand() % 2) {
                    buflen = randstring(buf,1,sizeof(buf)-1);
                } else {
                    switch(rand() % 3) {
                    case 0:
                        buflen = sprintf(buf,"%lld",(0LL + rand()) >> 20);
                        break;
                    case 1:
                        buflen = sprintf(buf,"%lld",(0LL + rand()));
                        break;
                    case 2:
                        buflen = sprintf(buf,"%lld",(0LL + rand()) << 20);
                        break;
                    default:
                        assert(NULL);
                    }
                }

                /* Add to ziplist */
                zl = ziplistPush(zl, (unsigned char*)buf, buflen, where);

                /* Add to reference list */
                if (where == ZIPLIST_HEAD) {
                    listAddNodeHead(ref,sdsnewlen(buf, buflen));
                } else if (where == ZIPLIST_TAIL) {
                    listAddNodeTail(ref,sdsnewlen(buf, buflen));
                } else {
                    assert(NULL);
                }
            }

            assert(listLength(ref) == ziplistLen(zl));
            for (j = 0; j < len; j++) {
                /* Naive way to get elements, but similar to the stresser
                 * executed from the Tcl test suite. */
                p = ziplistIndex(zl,j);
                refnode = listIndex(ref,j);

                assert(ziplistGet(p,&sstr,&slen,&sval));
                if (sstr == NULL) {
                    buflen = sprintf(buf,"%lld",sval);
                } else {
                    buflen = slen;
                    memcpy(buf,sstr,buflen);
                    buf[buflen] = '\0';
                }
                assert(memcmp(buf,listNodeValue(refnode),buflen) == 0);
            }
            zfree(zl);
            listRelease(ref);
        }
        printf("SUCCESS\n\n");
    }

    printf("Stress with variable ziplist size:\n");
    {
        stress(ZIPLIST_HEAD,100000,16384,256);
        stress(ZIPLIST_TAIL,100000,16384,256);
    }

    return 0;
}
#endif
