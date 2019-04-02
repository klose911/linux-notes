/*
 * This file contains some defines for the AT-hd-controller.
 * Various sources. Check out some definitions (see comments with
 * a ques).
 */

/*
 * AT 硬盘控制器的定义
 */
#ifndef _HDREG_H
#define _HDREG_H

/* Hd controller regs. Ref: IBM AT Bios-listing */
#define HD_DATA		0x1f0	/* _CTL when writing 数据寄存器 */ 
#define HD_ERROR	0x1f1	/* see err-bits 错误寄存器 */ 
#define HD_NSECTOR	0x1f2	/* nr of sectors to read/write 扇区数寄存器 */
#define HD_SECTOR	0x1f3	/* starting sector 起始扇区寄存器 */
#define HD_LCYL		0x1f4	/* starting cylinder  柱面号寄存器（低字节）*/
#define HD_HCYL		0x1f5	/* high byte of starting cyl 柱面号寄存器（高字节） */
#define HD_CURRENT	0x1f6	/* 101dhhhh , d=drive（驱动器号）, hhhh=head（磁头号） 驱动器/磁头寄存器*/
#define HD_STATUS	0x1f7	/* see status-bits 读时：主状态寄存器 */
#define HD_PRECOMP HD_ERROR	/* same io address, read=error, write=precomp 写前补偿寄存器 */
#define HD_COMMAND HD_STATUS	/* same io address, read=status, write=cmd 写时：命令寄存器 */

#define HD_CMD		0x3f6 // 硬盘控制寄存器：用于步进选择

/* Bits of HD_STATUS */
// 状态寄存器各位的定义
#define ERR_STAT	0x01 // 命令执行出错
#define INDEX_STAT	0x02 // 收到索引
#define ECC_STAT	0x04	/* Corrected error */ // ECC 校验错
#define DRQ_STAT	0x08 // 请求服务
#define SEEK_STAT	0x10 // 寻道结束
#define WRERR_STAT	0x20 // 驱动器故障
#define READY_STAT	0x40 // 驱动器就绪
#define BUSY_STAT	0x80 // 驱动器繁忙

/* Values for HD_COMMAND */
// 磁盘命令值
#define WIN_RESTORE		0x10 // 驱动器校正（复位）
#define WIN_READ		0x20 // 读扇区
#define WIN_WRITE		0x30 // 写扇区
#define WIN_VERIFY		0x40 // 扇区校验
#define WIN_FORMAT		0x50 // 格式化磁道
#define WIN_INIT		0x60 // 控制器初始化
#define WIN_SEEK 		0x70 // 寻道
#define WIN_DIAGNOSE    0x90 // 控制器诊断
#define WIN_SPECIFY		0x91 // 建立驱动器参数

/* Bits for HD_ERROR */
// 错误寄存器的各位的定义
// 执行诊断命令时与执行其他命令时有所不同：
// ========================================
//           诊断命令            其他命令
// 0x01      无错误              数据标志丢失
// 0x02      控制器出错           无法找到磁道0
// 0x03      扇区缓冲器错
// 0x04      ECC部件错           命令放弃
// 0x05      控制处理器错
// 0x10                         ID未找到
// 0x40                         ECC校验错
// 0x80                         坏扇区
#define MARK_ERR	0x01	/* Bad address mark ? */
#define TRK0_ERR	0x02	/* couldn't find track 0 */
#define ABRT_ERR	0x04	/* ? */
#define ID_ERR		0x10	/* ? */
#define ECC_ERR		0x40	/* ? */
#define	BBD_ERR		0x80	/* ? */

struct partition {
        unsigned char boot_ind;		/* 0x80 - active (unused) */
        unsigned char head;		/* ? */
        unsigned char sector;		/* ? */
        unsigned char cyl;		/* ? */
        unsigned char sys_ind;		/* ? */
        unsigned char end_head;		/* ? */
        unsigned char end_sector;	/* ? */
        unsigned char end_cyl;		/* ? */
        unsigned int start_sect;	/* starting sector counting from 0 */
        unsigned int nr_sects;		/* nr of sectors in partition */
};

#endif
