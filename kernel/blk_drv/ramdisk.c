/*
 *  linux/kernel/blk_drv/ramdisk.c
 *
 *  Written by Theodore Ts'o, 12/2/91
 */

#include <string.h> // 字符串头文件

#include <linux/config.h> // 系统配置头文件
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <asm/system.h>
#include <asm/segment.h>
#include <asm/memory.h>

/*
 * 主设备常量必须定义在引入'blk.h'头文件之前，因为头文件中会根据这个常量绑定不同的DEVICE_REQUEST函数指针
 * 
 */
#define MAJOR_NR 1 // RAM盘的主设备常量
#include "blk.h" // 块设备头文件

// 下面两个参数会在rd_init中被初始化！
char	*rd_start; // 虚拟盘所在内存的开始地址
int	rd_length = 0; // 虚拟盘所占内存的大小（字节）

/**
 * RAM盘设备请求项处理函数
 * 
 * 无参数
 *
 * 无返回值
 *
 * 在低级块设备接口ll_rw_block()建立起虚拟盘(rd)的请求项，并添加到rd的链表中，就会调用本函数来对链表中的当前请求项进行处理：
 * 首先计算当前请求项的起始扇区位置对应虚拟盘所处内存的起始位置addr和要求的扇区数对应的长度len
 * 然后根据请求项的命令进行操作：如果是写命令，则把请求项中缓冲区中的数据直接复制到addr处，如果是读，则反之
 * 数据完成后，则可直接调用end_request()结束本次操作
 * 接着跳转到函数开始处开始处理下一个请求项
 * 如果没有请求项，则退出
 * 
 */
void do_rd_request(void)
{
        int	len;
        char	*addr;

        INIT_REQUEST; // 检测请求项的合法性，如果没有请求项则退出
        // 计算当前请求项在RAM盘中的起始地址
        addr = rd_start + (CURRENT->sector << 9); // 一个扇区对应512字节，因此CURRENT->sector << 9 实际上计算的就是“扇区数 * 512”
        // 同样计算当前请求项要读写的数据长度（字节）
        len = CURRENT->nr_sectors << 9;
        // 当前请求项的主设备号 != 1 或 当前请求项的结尾地址 > RAM盘的起始地址 + RAM盘的大小
        if ((MINOR(CURRENT->dev) != 1) || (addr+len > rd_start+rd_length)) {
                end_request(0); // 结束该请求项，打印错误信息，并转向下一个请求项 
                goto repeat; 
        }
        if (CURRENT-> cmd == WRITE) { // 写命令
                // 从“当前请求项”的“高速缓冲区”位置复制len字节到“RAM盘”的“偏移位置addr”处
                (void ) memcpy(addr,
                               CURRENT->buffer,
                               len);
        } else if (CURRENT->cmd == READ) { // 读命令
                // 从“RAM盘”的“偏移位置addr”处复制len字节到“当前请求项”的“高速缓冲区”位置
                (void) memcpy(CURRENT->buffer, 
                              addr,
                              len);
        } else
                panic("unknown ramdisk-command"); // 无效命令，打印，死机
        end_request(1); // 结束当前请求项，转向处理下一个，直到没有任何请求项为止
        goto repeat;
}

/*
 * Returns amount of memory which needs to be reserved.
 */

/**
 * RAM盘初始化函数，在init/main.c中被调用，调用前计算下面两个参数
 *
 * mem_start: 虚拟盘初始内存地址（绝对物理地址）
 * length: 虚拟盘长度
 *
 * 返回：虚拟盘长度（字节）
 * 
 */
long rd_init(long mem_start, int length)
{
        int	i;
        char	*cp;

        blk_dev[MAJOR_NR].request_fn = DEVICE_REQUEST; // 设置块设备结构数组中对应项的函数处理指针request_fn
        rd_start = (char *) mem_start; // 设置虚拟盘的初始内存地址
        rd_length = length; // 设置虚拟盘的长度
        cp = rd_start;
        // 整个虚拟盘初始化为0
        for (i=0; i < length; i++)
                *cp++ = '\0';
        return(length); // 返回虚拟盘的长度
}

/*
 * If the root device is the ram disk, try to load it.
 * In order to do this, the root device is originally set to the
 * floppy, and we later change it to be ram disk.
 */

/*
 * 如果根文件系统对应的设备是虚拟盘，则尝试加载它
 * root_device 默认应该是软盘，所以在这里把它改成ramdisk
 * 
 */

/**
 * 尝试把根文件系统加载到虚拟盘中，本函数在内核设置函数hd.c/setup()中被调用
 *
 * 无参数
 *
 * 无返回值
 *
 * 要加载的根文件系统被存储在boot软盘的第256块磁盘块上（1磁盘块=1024字节）
 * 
 */
void rd_load(void)
{
        struct buffer_head *bh;
        struct super_block	s;
        int		block = 256;	/* Start at block 256 */ // 根文件系统开始于软盘的第256磁盘块上
        int		i = 1;
        int		nblocks;
        char		*cp;		/* Move pointer */
	
        if (!rd_length) // ramdisk的长度为0，直接返回
                return;
        printk("Ram disk: %d bytes, starting at 0x%x\n", rd_length,
               (int) rd_start);
        if (MAJOR(ROOT_DEV) != 2) // 根文件系统的主设备号应该是2（软盘），直接返回
                return;
        // 预读软盘的第256 + 1, 256, 256 + 2 这些磁盘块到高速缓冲区，其中 256 + 1对应的是根文件系统的超级块
        bh = breada(ROOT_DEV,block+1,block,block+2,-1);
        if (!bh) { // 预读根文件系统的超级块失败，打印错误，返回
                printk("Disk error while looking for ramdisk!\n");
                return;
        }
        *((struct d_super_block *) &s) = *((struct d_super_block *) bh->b_data); // 把根文件系统的超级块复制到变量s指向的内存中
        brelse(bh); // 释放高速缓冲块
        if (s.s_magic != SUPER_MAGIC) // 读入的文件魔数不是超级块的魔数，超级块读取无效，返回
                /* No ram disk image present, assume normal floppy boot */
                return;
        nblocks = s.s_nzones << s.s_log_zone_size; // 计算根文件系统的总数据块的个数（具体逻辑见文件系统部分）
        if (nblocks > (rd_length >> BLOCK_SIZE_BITS)) { // 根文件系统的数据太大，ramdisk无法容纳，打印错误，返回
                printk("Ram disk image too big!  (%d blocks, %d avail)\n", 
                       nblocks, rd_length >> BLOCK_SIZE_BITS);
                return;
        }
        printk("Loading %d bytes into ram disk... 0000k", 
               nblocks << BLOCK_SIZE_BITS); // 调试信息
        cp = rd_start; // cp指向ramdisk开始的地方
        // 循环拷贝根文件到ramdisk
        while (nblocks) { 
                if (nblocks > 2) // 要读的数据块数大于2：可以支持预读 
                        bh = breada(ROOT_DEV, block, block+1, block+2, -1); // 预读对应的block, block + 1, block + 2 到高速缓冲区中
                else
                        bh = bread(ROOT_DEV, block); // 单块读取block块到高速缓冲区中
                if (!bh) { // 读取失败，打印错误信息，返回
                        printk("I/O error on block %d, aborting load\n", 
                               block);
                        return;
                }
                (void) memcpy(cp, bh->b_data, BLOCK_SIZE); // 从高速缓冲块拷贝到ramdisk区
                brelse(bh); // 释放高速缓冲块
                printk("\010\010\010\010\010%4dk",i); // 打印读取的数据块编号
                cp += BLOCK_SIZE; // cp指针向后增加1024（每块数据块是1024字节）
                block++; // 软盘要读取的数据块数字 + 1 
                nblocks--; // 总共要读取的数据块个数 - 1 
                i++; // 下一个要读取的块编号 + 1 
        }
        printk("\010\010\010\010\010done \n"); // 打印“已经成功加载根文件系统”
        ROOT_DEV=0x0101; // 根文件系统的设备号修改成0x101
}
