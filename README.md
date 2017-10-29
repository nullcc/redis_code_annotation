# Redis代码注释

Reids版本：3.2

本注释版本不会删除原始代码中的任何东西，只会增加注释。

本注释项目只是个人爱好，受技术水平和时间限制，出差错在所难免，有感兴趣的同学欢迎指出问题、交流和提交issue。

## 进度

|  文件名  | 描述    | 完成情况
|:------------------|:------------------|:------------------
| adlist.h 和 adlist.c  | 双端链表 | ✔
| sds.h 和 sds.c | 简单动态字符串 | ✔
| dict.h 和 dict.c | 字典 | 99%
| ziplist.h 和 ziplist.c | 压缩列表 |
| zipmap.h 和 zipmap.c | 压缩字典 |
| hyperloglog.c | hyperloglog算法 |