/*
 *  linux/kernel/hd.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * This is the low-level hd interrupt support. It traverses the
 * request-list, using interrupts to jump between functions. As
 * all the functions are called within interrupts, we may not
 * sleep. Special care is recommended.
 * 
 *  modified by Drew Eckhardt to check nr of hd's from the CMOS.
 */

#include <linux/config.h> // 内核配置头文件，可以在这里定义硬盘参数HD_TYPE（默认从CMOS读取）
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/hdreg.h> // 磁盘参数表头文件
#include <asm/system.h>
#include <asm/io.h>
#include <asm/segment.h>

/*
 * 主设备号必须定义在'blk.h'前，因为在blk.h中会用到这个符号常数！！！
 * 
 */
#define MAJOR_NR 3 // 硬盘主设备号
#include "blk.h" // 块设备头文件

// 读CMOS参数宏，与init/main.c中一样
#define CMOS_READ(addr) ({                      \
                        outb_p(0x80|addr,0x70); \
                        inb_p(0x71);            \
                })

/* Max read/write errors/sector */
#define MAX_ERRORS	7 // 读写一个硬盘最多允许的出错次数
#define MAX_HD		2 // 支持的最多硬盘数

// 重新校正处理函数
// 复位操作时在硬盘中断处理程序中调用的重新校正函数
static void recal_intr(void);

// 重新校正标志，当设置了该标志，程序中会调用recal_init()将磁头移动到0柱面
static int recalibrate = 1;
// 复位标志，发生读写错误时会设置该标志，并调用相关的复位标志，以复位硬盘和控制器
static int reset = 1;

/*
 *  This struct defines the HD's and their types.
 */
// 磁盘信息结构
struct hd_i_struct {
        int head,sect,cyl,wpcom,lzone,ctl; // 字段分别对应：磁头数，每柱面（磁道）的扇区数，柱面（磁道）数，写前预补偿柱面号，磁头着陆区柱面号，控制字节 
};

// 如果在include/linux/config.h 定义了符号参数 HD_TYPE：就取其中配置好的信息作为hd_info[]数组中的数据
// 反之：先默认都设为0，在 setup()函数中重新进行设置
#ifdef HD_TYPE
struct hd_i_struct hd_info[] = { HD_TYPE }; // 磁盘信息数组
#define NR_HD ((sizeof (hd_info))/(sizeof (struct hd_i_struct))) // 计算硬盘个数
#else
struct hd_i_struct hd_info[] = { {0,0,0,0,0,0},{0,0,0,0,0,0} };
static int NR_HD = 0;
#endif

/*
 * 硬盘分区结构数组
 *
 * hd[0]: 第一个硬盘
 * hd[1]: 第一个硬盘第一个分区
 * hd[2]: 第一个硬盘第二个分区
 * hd[3]: 第一个硬盘第三个分区
 * hd[4]: 第一个硬盘第四个分区
 * hd[5]: 第二个硬盘
 * hd[6]: 第二个硬盘第一个分区
 * hd[7]: 第二个硬盘第二个分区
 * hd[8]: 第二个硬盘第三个分区
 * hd[9]: 第二个硬盘第四个分区
 * 
 */
static struct hd_struct {
        long start_sect; // 分区在硬盘中的起始物理（绝对）扇区，从0开始计数
        long nr_sects; // 分区占据的扇区数
} hd[5*MAX_HD]={{0,0},};

/*
 * 读端口嵌入宏：数据从端口port读入到buf地址处，总共读nr个字
 *
 * port: 端口
 * buf: 缓冲区地址（内核数据段）
 * nr: 字数
 * 
 */
#define port_read(port,buf,nr)                                  \
        __asm__("cld;rep;insw"::"d" (port),"D" (buf),"c" (nr))

/*
 * 写端口嵌入宏：数据从buf地址处写入到端口port处，总共写nr个字
 *
 * port: 端口
 * buf: 缓冲区地址（内核数据段）
 * nr: 字数
 * 
 */
#define port_write(port,buf,nr)                                 \
        __asm__("cld;rep;outsw"::"d" (port),"S" (buf),"c" (nr))

extern void hd_interrupt(void); // 硬盘中断处理过程(system_call.s) 
extern void rd_load(void); // 虚拟盘创建加载函数(ramdisk.c)  

/* This may be used only once, enforced by 'static int callable' */

/**
 * 系统设置函数：读取 CMOS 和硬盘参数表结构，用于设置硬盘分区结构 hd_info，并尝试加载“RAM 虚拟盘” 和“根文件系统”
 *
 * BIOS: 指向硬盘参数表结构的指针（由初始化程序init/main.c中init()中设置）
 *
 * 成功：返回 0，失败：返回 -1
 * 
 * 硬盘参数表结构包含2个硬盘参数表的内容（共32字节），是从内存0x90080处复制得来，
 * 0x90080处的硬盘参数表是由setup.s程序通过 rom bios功能获取到
 *
 * 注意：本函数仅在系统启动时被调用一次(init/main.c)，静态变量 callable作为是否已经调用的标志
 * 
 */
int sys_setup(void * BIOS)
{
        static int callable = 1; // 已经调用标志
        int i,drive;
        unsigned char cmos_disks;
        struct partition *p;
        struct buffer_head * bh;
        
        if (!callable) // 如果 callable == 0，本函数已经被调用过了
                return -1; // 直接返回
        callable = 0;

        // 没有在 include/linux/config.h 中定义常数 HD_TYPE（如果已经定义了，则hd_info数组已经被初始化好）
#ifndef HD_TYPE
        // 从BIOS指针处，依次读取硬盘参数结构表
        for (drive=0 ; drive<2 ; drive++) {
                hd_info[drive].cyl = *(unsigned short *) BIOS; // 柱面数
                hd_info[drive].head = *(unsigned char *) (2+BIOS); // 磁头数
                hd_info[drive].wpcom = *(unsigned short *) (5+BIOS); // 写前预补偿柱面号
                hd_info[drive].ctl = *(unsigned char *) (8+BIOS); // 控制字节
                hd_info[drive].lzone = *(unsigned short *) (12+BIOS); // 磁头着陆区柱面号
                hd_info[drive].sect = *(unsigned char *) (14+BIOS); // 每柱面扇区数
                BIOS += 16;
        }
        // 如果系统只有一个硬盘，那么第二个硬盘对应的16字节会全部清零
        if (hd_info[1].cyl) // 判断第二个硬盘的柱面数是否为0，就可以知道是否存在第二个硬盘
                NR_HD=2;
        else
                NR_HD=1;
#endif
        // 现在开始设置硬盘分区表数组：这里只设置第0项和第5项，对应第一个块硬盘，和第二块硬盘，其他对应硬盘分区的项不做设置
        for (i=0 ; i<NR_HD ; i++) {
                hd[i*5].start_sect = 0; // 初始扇区为0
                hd[i*5].nr_sects = hd_info[i].head*
                        hd_info[i].sect*hd_info[i].cyl; // 总扇区数量 = 磁头数 * 每柱面（磁道）扇区数 * 柱面数
        }

        /*
          We querry CMOS about hard disks : it could be that 
          we have a SCSI/ESDI/etc controller that is BIOS
          compatable with ST-506, and thus showing up in our
          BIOS table, but not register compatable, and therefore
          not present in CMOS.

          Furthurmore, we will assume that our ST-506 drives
          <if any> are the primary drives in the system, and 
          the ones reflected as drive 1 or 2.

          The first drive is stored in the high nibble of CMOS
          byte 0x12, the second in the low nibble.  This will be
          either a 4 bit drive type or 0xf indicating use byte 0x19 
          for an 8 bit type, drive 1, 0x1a for drive 2 in CMOS.

          Needless to say, a non-zero value means we have 
          an AT controller hard disk for that drive.

		
        */

        // 检查硬盘到底是不是 AT控制器兼容
        // 从CMOS偏移地址0x12读出磁盘类型字节：
        // 1. 如果低半字节值不为0， 则表示有2块“AT兼容”的硬盘
        // 2. 如果低半字节值为0，表示只有1块“AT兼容”的硬盘
        // 3. 如果整个字节为0，没有“AT兼容”的硬盘
        if ((cmos_disks = CMOS_READ(0x12)) & 0xf0) // 整个字节不为0
                if (cmos_disks & 0x0f) // 低半字节不为0
                        NR_HD = 2;
                else // 低半字节为0
                        NR_HD = 1; 
        else
                NR_HD = 0;

        // 根据上面检测结果，重新设置硬盘分区表
        for (i = NR_HD ; i < 2 ; i++) {
                hd[i*5].start_sect = 0;
                hd[i*5].nr_sects = 0;
        }

        // 初始化硬盘分区表数组hd中的分区项
        for (drive=0 ; drive<NR_HD ; drive++) {
                // 从每块硬盘的“第一个扇区”读出分区表信息到“高速缓冲区”
                if (!(bh = bread(0x300 + drive*5,0))) { // 0x300, 0x305 分别是第一块硬盘，第二块硬盘的设备号
                        printk("Unable to read partition table of drive %d\n\r", drive); // 无法读取分区表 
                        panic(""); // 死机
                }
                // 硬盘第一个扇区的结束标志应该是0xAA55 
                if (bh->b_data[510] != 0x55 || (unsigned char)
                    bh->b_data[511] != 0xAA) { // 判断硬盘标志0xAA55
                        printk("Bad partition table on drive %d\n\r",drive); // 分区表损坏
                        panic(""); // 死机
                }
                p = 0x1BE + (void *)bh->b_data; // 分区表位于第一个扇区的 0x1BE 偏移处
                for (i=1;i<5;i++,p++) {
                        hd[i+5*drive].start_sect = p->start_sect; // 设置分区的开始扇区
                        hd[i+5*drive].nr_sects = p->nr_sects; // 设置分区的扇区数
                }
                brelse(bh); // 释放高速缓冲块
        }
        if (NR_HD)
                printk("Partition table%s ok.\n\r",(NR_HD>1)?"s":""); // 打印初始化号的分区表
        rd_load(); // 尝试加载 RAM盘 (blk_dev/ramdisk.c)
        mount_root(); // 加载根文件系统 (fs/super.c)
        return (0); // 成功：返回 0
}

/*
 * 判断并循环等待硬盘控制器就绪
 *
 * 返回值：> 0 表示控制器就绪， == 0 表示“等待时间期限”已经超时
 *
 * 注意：实际上只需要判断第7位就可以，另外现在PC的处理速度很快，循环的次数应该适当加大
 * 
 */
static int controller_ready(void)
{
        int retries=10000;

        // 读”主状态端口“HD_STATUS(0x1f7)
        // 循环检测其中的驱动器就绪位（位6 = 1）是否被置位，并且控制器忙位是否被复位（位7 = 0）
        // 0xc0 = 0b11000000, 因此 &0xc0 可能的结果有 '0b11000000','0b10000000','0b1000000','0b0'
        // 其中 0b100000 = 0x40 是满足控制器空闲要求的
        while (--retries && (inb_p(HD_STATUS)&0xc0)!=0x40);
        return (retries); // 循环计数次数
}

/*
 * 检查硬盘执行命令后的状态（win是温切斯特硬盘的缩写）
 *
 * 返回：0 表示正常，1 表示出错
 * 
 * 如果执行命令错误，还需要再读”错误端口“ HD_ERROR(0x1f1)
 * 
 */
static int win_result(void)
{
        int i=inb_p(HD_STATUS); // 读取状态端口

        // 命令执行成功：BUSY_STAT == 0，READY_STAT == 1，WRERR_STAT == 0, SEEK_STAT == 1, ERR_STAT == 0
        if ((i & (BUSY_STAT | READY_STAT | WRERR_STAT | SEEK_STAT | ERR_STAT))
            == (READY_STAT | SEEK_STAT))
                return(0); /* ok */
        if (i&1) // 若 ERR_STAT（位0 == 1）被置位
                i=inb(HD_ERROR); // 则读取“错误端口”HD_ERROR
        return (1); // 表示出错
}

/*
 * 向硬盘控制器发送命令块
 *
 * drive: 硬盘号 0～1
 * nsect: 读写扇区数
 * sect: 起始扇区
 * head: 磁头号
 * cyl: 柱面号
 * cmd: 命令码
 * intr_addr: 硬盘中断处理程序中将要调用的C函数指针
 * 
 * 无返回
 *
 * 该函数在硬盘控制器就绪后调用：
 * 1. 设置 do_hd 为硬盘中断处理程序中将要调用的C函数指针 (read_intr, write_intr)
 * 2. 发送硬盘控制字节
 * 3. 发送7字节的参数命令块
 * 
 */
static void hd_out(unsigned int drive,unsigned int nsect,unsigned int sect,
                   unsigned int head,unsigned int cyl,unsigned int cmd,
                   void (*intr_addr)(void))
{
        register int port asm("dx"); // 定义局部寄存器变量并存放在寄存器dx中

        // 检查参数有效性
        if (drive>1 || head>15) // 硬盘号 > 1 或者 磁头号 > 15 
                panic("Trying to write bad sector"); // 打印出错信息，死机
        if (!controller_ready()) // 硬盘状态未就绪
                panic("HD controller not ready"); // 打印出错信息，死机

        // 硬盘控制器执行完读写命令后会发出一个硬盘中断
        // 硬盘中断程序由 system_call.s/hd_interrupt来处理
        // hd_interrupt 又会调用 do_hd 这个C函数指针，所以需要事先设置
        // 注意：do_hd被声明在blk.h第114行！！！
        do_hd = intr_addr; // 设置硬盘中断处理程序中将要调用的C函数指针(read_intr, write_intr)
        // 先向”命令端口“(0x3f6)发送指定硬盘的”控制字节”
        outb_p(hd_info[drive].ctl,HD_CMD); // 向”磁盘控制命令端口“(HD_CMD)发送”控制字节“(hd_info[drive].ctl)
        // 依次向“数据端口”(0x1f1~0x1f7)发送7字节的参数命令块
        port=HD_DATA; // dx寄存器设置为数据端口 0x1f0
        outb_p(hd_info[drive].wpcom>>2,++port); // 参数：写预补偿柱面号（需除4），0x1f1 
        outb_p(nsect,++port); // 参数：读/写扇区总数, 0x1f2
        outb_p(sect,++port); // 参数：起始扇区号, 0x1f3
        outb_p(cyl,++port); // 参数：柱面号低8位, 0x1f4
        outb_p(cyl>>8,++port); // 参数：柱面号高8位, 0x1f5
        outb_p(0xA0|(drive<<4)|head,++port); // 参数：驱动器 + 磁头号, 0x1f6
        outb(cmd,++port); // 命令：磁盘控制命令, 0x1f7
        // 磁盘控制器接到这7个字节的命令后：
        // 1. 执行磁盘操作（比如：把一个扇区的磁盘读入硬盘控制器缓冲区） 
        // 2. 操作完成后触发硬盘中断
        // 3. 硬盘中断处理调用do_hd(比如: read_intr 检查执行状态，如果成功把一个扇区的数据从磁盘控制器缓冲区读入到请求项的缓冲块中)
        // 继续执行下一个扇区的读/写命令 ......
}

/*
 * 等待磁盘就绪
 *
 * 返回：成功返回 0, 若等待时间超时，仍然忙碌，返回 1
 */
static int drive_busy(void)
{
        unsigned int i;

        // 循环读取主状态端口
        for (i = 0; i < 10000; i++)
                if (READY_STAT == (inb_p(HD_STATUS) & (BUSY_STAT|READY_STAT))) // READY_STAT 位 == 1 并且 BUSY_STAT == 0
                        break; // 退出循环
        
        i = inb(HD_STATUS);
        i &= BUSY_STAT | READY_STAT | SEEK_STAT; 
        if (i == (READY_STAT | SEEK_STAT)) // 仅有 READY_STAT == 1 或 SEEK_STAT = 1 
                return(0); // 返回 0 表示成功
        printk("HD controller times out\n\r"); // 等待超时，打印出错，返回 1 表示失败
        return(1);
}

/*
 * 重新校正磁盘控制器
 * 
 */
static void reset_controller(void)
{
        int	i;

        outb(4,HD_CMD); // 向”控制端口“(0x3f6)发送”允许复位“(4)控制字节
        for(i = 0; i < 100; i++) nop(); // 循环空操作，使得磁盘控制器有时间复位
        outb(hd_info[0].ctl & 0x0f ,HD_CMD); // 再向”控制端口“(0x3f6)发送”正常控制字节“（&0xf：不禁止重试、重读）
        // 等待硬盘控制器就绪
        if (drive_busy()) // 等待就绪超时
                printk("HD-controller still busy\n\r"); // 打印出错信息
        // 读取”错误端口“(0x1f1)
        if ((i = inb(HD_ERROR)) != 1) // 如果其内容 != 1 : 表示复位操作失败 
                printk("HD-controller reset failed: %02x\n\r",i); // 打印出错信息
}

/*
 * 硬盘复位操作
 * 
 */
static void reset_hd(int nr)
{
        reset_controller(); // 复位硬盘控制器
        // 向硬盘控制器发送”建立驱动器参数“(WIN_SPECIFY)命令，中断处理程序中将要调用的C函数指针为'&recal_intr'
        hd_out(nr,hd_info[nr].sect,hd_info[nr].sect,hd_info[nr].head-1,
               hd_info[nr].cyl,WIN_SPECIFY,&recal_intr); 
}

/**
 * 意外硬盘中断处理
 *
 * 如果硬盘中断处理程序hd_interrupt(kernel/system_call.s)中对应的C函数指针为NULL时候，调用本函数
 * 
 */
void unexpected_hd_interrupt(void)
{
        printk("Unexpected HD interrupt\n\r"); // 打印出错信息
}

/*
 * 读写硬盘失败处理
 *
 */
static void bad_rw_intr(void)
{
        if (++CURRENT->errors >= MAX_ERRORS) // 读写硬盘出错次数大于等于7 
                end_request(0); // 结束当前请求项，并唤醒等待该请求的进程
        if (CURRENT->errors > MAX_ERRORS/2) // 读写硬盘次数大于3
                reset = 1; // 设置复位标志：要求执行复位硬盘控制器的操作
}

/*
 * 读操作中断调用
 * 
 */
static void read_intr(void)
{
        if (win_result()) { // 读操作失败：控制器忙，读出错，或命令执行出错
                bad_rw_intr(); // 执行读写失败处理
                do_hd_request(); // 请求硬件做相应处理：复位或执行下一个请求项
                return;
        }
        // 读当前扇区操作成功
        // 注意：port_read 读取的是256个字，相当于512字节
        // request->buffer, request->bh->data 这两者是同一个指针，因此下面的读取操作，也会把数据写入到“高速缓冲块”的数据区
        port_read(HD_DATA,CURRENT->buffer,256); // 从硬盘控制器的“数据端口”读取一个扇区（512字节）到“当前请求项”的“高速缓冲块”的“数据区”
        CURRENT->errors = 0; // 清空“当前请求项”的错误计数
        CURRENT->buffer += 512; // 当前请求项的“缓冲区”指针增加512
        CURRENT->sector++; // “当前请求项”的已读扇区数 + 1 
        if (--CURRENT->nr_sectors) { // 所需读取的总扇区数 > 0 : 说明还没全部读完
                do_hd = &read_intr; // 再次设置硬盘中断调用的C函数指针为'read_intr'
                return;
        }
        // 本次请求项的全部扇区已经读完：结束当前请求项的操作（解锁高速缓冲块，唤醒等待高速缓冲块的进程，唤醒等待空闲请求项的进程等）
        end_request(1); // 置位“当前请求项”中的“高速缓冲块”中的“数据已更新”标志
        do_hd_request(); // 处理的下一个“硬盘请求项”
}

/*
 * 写操作中断调用
 * 
 */
static void write_intr(void)
{
        if (win_result()) { // 写操作失败
                bad_rw_intr(); // 执行读写操作失败处理
                do_hd_request(); // 请求硬盘做相应处理：重复执行，复位硬盘，或执行一个请求项
                return;
        }
        // 当前扇区写操作成功
        if (--CURRENT->nr_sectors) { // 判断是否还有扇区需要写
                CURRENT->sector++; // 写入扇区总数 + 1 
                CURRENT->buffer += 512; // “当前请求项”的“缓冲区”指针向后移动512字节 
                do_hd = &write_intr; // 再次设置硬盘中断调用的C函数指针为'write_intr'
                port_write(HD_DATA,CURRENT->buffer,256); // 从“当前请求项”的“高速缓冲块”的“数据区”向“硬盘控制器”的“数据端口”写入512字节（256字） 
                return;
        }
        // 本次请求项的全部扇区已经写完：结束当前请求项的操作
        end_request(1); // 置位“当前请求项”中的“高速缓冲块”中的“数据已更新”标志
        do_hd_request(); // 处理的下一个“硬盘请求项”
}

/*
 * 复位操作的中断处理
 * 
 */
static void recal_intr(void)
{
        if (win_result()) // 复位操作出错
                bad_rw_intr(); // 执行出错处理
        do_hd_request(); // 执行硬盘请求项
}

void do_hd_request(void)
{
        int i,r = 0;
        unsigned int block,dev;
        unsigned int sec,head,cyl;
        unsigned int nsect;

        INIT_REQUEST;
        dev = MINOR(CURRENT->dev);
        block = CURRENT->sector;
        if (dev >= 5*NR_HD || block+2 > hd[dev].nr_sects) {
                end_request(0);
                goto repeat;
        }
        block += hd[dev].start_sect;
        dev /= 5;
        __asm__("divl %4":"=a" (block),"=d" (sec):"0" (block),"1" (0),
                "r" (hd_info[dev].sect));
        __asm__("divl %4":"=a" (cyl),"=d" (head):"0" (block),"1" (0),
                "r" (hd_info[dev].head));
        sec++;
        nsect = CURRENT->nr_sectors;
        if (reset) {
                reset = 0;
                recalibrate = 1;
                reset_hd(CURRENT_DEV);
                return;
        }
        if (recalibrate) {
                recalibrate = 0;
                hd_out(dev,hd_info[CURRENT_DEV].sect,0,0,0,
                       WIN_RESTORE,&recal_intr);
                return;
        }	
        if (CURRENT->cmd == WRITE) {
                hd_out(dev,nsect,sec,head,cyl,WIN_WRITE,&write_intr);
                for(i=0 ; i<3000 && !(r=inb_p(HD_STATUS)&DRQ_STAT) ; i++)
                        /* nothing */ ;
                if (!r) {
                        bad_rw_intr();
                        goto repeat;
                }
                port_write(HD_DATA,CURRENT->buffer,256);
        } else if (CURRENT->cmd == READ) {
                hd_out(dev,nsect,sec,head,cyl,WIN_READ,&read_intr);
        } else
                panic("unknown hd-command");
}

/**
 * 硬盘初始化函数：init/main.c的main()中被调用
 */
void hd_init(void)
{
        blk_dev[MAJOR_NR].request_fn = DEVICE_REQUEST; // 设置“硬盘设备”的“请求项处理”函数指针为 do_hd_request 
        set_intr_gate(0x2E,&hd_interrupt); // 设置硬盘中断门描述符：硬盘中断信号为46(0x2E), 中断处理函数 hd_interrupt（位于kernel/system_call.s中）
        // 中断号 0x2E 对应8259A芯片的中断请求号 IRQ14
        outb_p(inb_p(0x21)&0xfb,0x21); // 复位接联8259A IRQ 2 的屏蔽位，允许从片发出中断请求
        outb(inb_p(0xA1)&0xbf,0xA1); // 复位从片的 IRQ14 屏蔽位，允许硬盘控制器发出中断请求
}
