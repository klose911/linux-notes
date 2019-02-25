/*
 *  linux/fs/block_dev.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <errno.h>

#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/segment.h>
#include <asm/system.h>

/**
 * 数据块写函数：向指定设备从给定偏移处写入指定长度的数据
 *
 * dev: 设备号
 * pos: 设备文件中的偏移量指针
 * buf: 用户空间中的缓冲区地址
 * count: 写入的数据长度
 *
 * 成功：返回已经写入的字节数，如果没有写入任何字节或出错：返回错误号
 *
 * 1. 对于内核来说：写操作实际上是向高速缓冲区写入数据。真正的写入设备是由高速缓冲区管理程序决定的
 * 2. 对于块设备来说，是以块为单位读写的，因此对于开始位置不处于块起始处的时候，需要将字节所在整个块读出，然后将需要写的数据从写开始处填写，最后再将这块数据块写盘（高速缓冲区程序处理）
 * 
 */
int block_write(int dev, long * pos, char * buf, int count)
{
        // 设备文件的偏移量指针pos换算成读写盘块的序号block, 并求出需写第一个字节在该块的偏移量offset
        int block = *pos >> BLOCK_SIZE_BITS; // pos 所在文件数据块号
        int offset = *pos & (BLOCK_SIZE-1); // pos 在数据块中的偏移值
        int chars; 
        int written = 0;
        struct buffer_head * bh;
        // 局部寄存器变量，被存放在寄存器中，为了加速操作！！！
        register char * p;

        // 针对要写入的count个字节数，执行下列循环，直到数据全部写入
        while (count>0) {
                chars = BLOCK_SIZE - offset; // 计算写入块从偏移量 offset 到块尾的字节数
                if (chars > count) // 如果写入的字节数，不足以填满接下来的块
                        chars=count; // chars 设置为要写入的字节数
                if (chars == BLOCK_SIZE) // 从块开头开始写
                        bh = getblk(dev,block); // 从设备读取对应的逻辑块到高速缓冲区
                else
                        bh = breada(dev,block,block+1,block+2,-1); // 从设备预读2个逻辑块到高速缓冲区，为下次写做准备
                block++; // 逻辑块号递增，为下个循环准备
                if (!bh) // 读取设备逻辑块失败
                        return written?written:-EIO; // 如果已经写入数据，则返回写入的字节数，否则返回 EIO 
                p = offset + bh->b_data; // 指针p指向高速缓冲区的offset偏移处
                offset = 0; // 从第二个循环开始，每次都是从逻辑块开始写，所以这里预先把offset设置为0
                *pos += chars; // pos指针向后chars个字节（这次循环将要写的字节数）
                written += chars; // 总写入字节数加上本次循环将写的chars个字节 
                count -= chars; // 总写入字节数扣除本次循环将要写的chars个字节
                
                // 从用户缓冲区复制chars个字节到高速缓冲块
                while (chars-->0)
                        // 从用户缓冲中复制一个字节到p指向的高速缓冲区中
                        *(p++) = get_fs_byte(buf++); // p指向下一个字节， buf指向下一个字节
                bh->b_dirt = 1; // 置位高速缓冲块的修改标志
                brelse(bh); // 释放已写入的缓冲区（缓冲区引用计数减1）
        }
        return written; // 成功：返回已写入的字节数
}

/**
 * 数据块读函数：从指定设备的给定偏移处读入指定长度的数据
 *
 * dev: 设备号
 * pos: 设备文件偏移处
 * buf: 用户空间中的缓冲区地址
 * count: 读入的数据长度
 *
 * 成功：返回已经读入的字节数，如果没有读入任何字节或出错：返回错误号
 *
 * 实现上和把数据写入块设备一样，先从设备读入到高速缓冲区，再从高速缓冲区复制字节到用户空间缓冲地址
 *
 * 注意：1. 总是预读多个逻辑块
 *      2. 不需要置位高速缓冲区的修改标志
 * 
 */
int block_read(int dev, unsigned long * pos, char * buf, int count)
{
        int block = *pos >> BLOCK_SIZE_BITS; // pos 所在文件数据块号
        int offset = *pos & (BLOCK_SIZE-1); // pos 在数据块中的偏移值
        int chars;
        int read = 0;
        struct buffer_head * bh;
        register char * p;


// 针对要读入的count个字节数，执行下列循环，直到数据全部读入
        while (count>0) {
                chars = BLOCK_SIZE - offset; // 计算读入块从偏移量 offset 到块尾的字节数 
                if (chars > count) // 如果要读入的字节数不满偏移量到块尾
                        chars = count; // 本次循环读入的字节数(chars)设置为要读入的总字节数(count)
                // 从设备预读2个逻辑块到高速缓冲区
                if (!(bh = breada(dev,block,block+1,block+2,-1))) // 从设备读取失败
                        return read?read:-EIO; // 如果已经读入数据，则返回读入的字节数，否则返回 EIO 
                block++; // 逻辑块号递增，为下个循环准备
                p = offset + bh->b_data; // 指针p指向高速缓冲区的offset偏移处
                offset = 0; // 从第二个循环开始，每次都是从逻辑块开始读，所以这里预先把offset设置为0
                *pos += chars; // pos指针向后chars个字节（这次循环将要读的字节数）
                read += chars; // 总读入字节数加上本次循环将读的chars个字节 
                count -= chars; // 总读入字节数扣除本次循环将要读的chars个字节
                // 从高速缓冲区复制chars个字节到用户缓冲地址
                while (chars-->0)
                        // 从p指向的高速缓冲区复制一个字节到buf指向的用户空间缓冲中
                        put_fs_byte(*(p++),buf++); // p指向下一个字节，buf指向下一个字节
                brelse(bh); // 释放已读取的高速缓冲块
        }
        return read; // 成功：返回总写入的字节数
}
