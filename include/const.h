#ifndef _CONST_H
#define _CONST_H

/**
 * 该文件定义了i节点中的文件属性类型和i_mode字段所用到的一些标志位常量符号
 */
#define BUFFER_END 0x200000 // 缓冲使用的内存末端

// i节点数据结构中的i_mode字段的各标志位
#define I_TYPE          0170000 // 指明i节点类型
#define I_DIRECTORY	    0040000 // 目录文件
#define I_REGULAR       0100000 // 常规文件
#define I_BLOCK_SPECIAL 0060000 // 块设备特殊文件
#define I_CHAR_SPECIAL  0020000 // 字符设备特殊文件
#define I_NAMED_PIPE	0010000 // 命名管道文件
#define I_SET_UID_BIT   0004000 // ”执行时设置有效用户“标志位
#define I_SET_GID_BIT   0002000 // ”执行时设置有效组“标志位

#endif
