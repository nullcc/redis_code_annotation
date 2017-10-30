#ifndef __GEO_H__
#define __GEO_H__

#include "server.h"

/* Structures used inside geo.c in order to represent points and array of
 * points on the earth. */
/* geo.c中使用的数据结构，用来表示地球上的地理位置和地理位置数组*/
typedef struct geoPoint {
    double longitude;  // 经度
    double latitude;   // 纬度
    double dist;
    double score;
    char *member;
} geoPoint;

typedef struct geoArray {
    struct geoPoint *array;  // 地理位置数组
    size_t buckets;  // 地理位置数组大小
    size_t used;  // 地理位置数组中元素个数
} geoArray;

#endif
