#ifndef _HEAD_H
#define _HEAD_H

// 段描述符的结构，每个描述符8个字节长
// 不要和段选择子混淆，段选择子相当于段描述符表的索引，一般是2个字节长
typedef struct desc_struct {
	unsigned long a,b;
} desc_table[256]; // 段描述符表，这个表有256项段描述符

extern unsigned long pg_dir[1024]; // 页目录表
extern desc_table idt,gdt; // 中断描述符表，全局描述符表

#define GDT_NUL 0 // 全局描述符表第一项：不使用
#define GDT_CODE 1 // 全局描述符表第二项：内核代码段
#define GDT_DATA 2 // 全局描述符表第三项：内核数据段
#define GDT_TMP 3 // 全局描述符表第四项：系统段描述符，未使用

#define LDT_NUL 0 // 局部描述符表第一项：不使用
#define LDT_CODE 1 // 局部描述符表第二项：用户进程代码段
#define LDT_DATA 2 // 局部描述符表第三项：用户进程数据段

#endif
