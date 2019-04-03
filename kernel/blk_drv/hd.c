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
 * 读端口嵌入宏：数据从端口port读入到buf地址处，总共读nr个字节
 *
 * port: 端口
 * buf: 缓冲区地址（内核数据段）
 * nr: 字节数
 * 
 */
#define port_read(port,buf,nr)                                  \
        __asm__("cld;rep;insw"::"d" (port),"D" (buf),"c" (nr))

/*
 * 写端口嵌入宏：数据从buf地址处写入到端口port处，总共写nr个字节
 *
 * port: 端口
 * buf: 缓冲区地址（内核数据段）
 * nr: 字节数
 * 
 */
#define port_write(port,buf,nr)                                 \
        __asm__("cld;rep;outsw"::"d" (port),"S" (buf),"c" (nr))

extern void hd_interrupt(void); // 硬盘中断处理过程(system_call.s) 
extern void rd_load(void); // 虚拟盘创建加载函数(ramdisk.c)  

/* This may be used only once, enforced by 'static int callable' */
int sys_setup(void * BIOS)
{
        static int callable = 1;
        int i,drive;
        unsigned char cmos_disks;
        struct partition *p;
        struct buffer_head * bh;

        if (!callable)
                return -1;
        callable = 0;
#ifndef HD_TYPE
        for (drive=0 ; drive<2 ; drive++) {
                hd_info[drive].cyl = *(unsigned short *) BIOS;
                hd_info[drive].head = *(unsigned char *) (2+BIOS);
                hd_info[drive].wpcom = *(unsigned short *) (5+BIOS);
                hd_info[drive].ctl = *(unsigned char *) (8+BIOS);
                hd_info[drive].lzone = *(unsigned short *) (12+BIOS);
                hd_info[drive].sect = *(unsigned char *) (14+BIOS);
                BIOS += 16;
        }
        if (hd_info[1].cyl)
                NR_HD=2;
        else
                NR_HD=1;
#endif
        for (i=0 ; i<NR_HD ; i++) {
                hd[i*5].start_sect = 0;
                hd[i*5].nr_sects = hd_info[i].head*
                        hd_info[i].sect*hd_info[i].cyl;
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

        if ((cmos_disks = CMOS_READ(0x12)) & 0xf0)
                if (cmos_disks & 0x0f)
                        NR_HD = 2;
                else
                        NR_HD = 1;
        else
                NR_HD = 0;
        for (i = NR_HD ; i < 2 ; i++) {
                hd[i*5].start_sect = 0;
                hd[i*5].nr_sects = 0;
        }
        for (drive=0 ; drive<NR_HD ; drive++) {
                if (!(bh = bread(0x300 + drive*5,0))) {
                        printk("Unable to read partition table of drive %d\n\r",
                               drive);
                        panic("");
                }
                if (bh->b_data[510] != 0x55 || (unsigned char)
                    bh->b_data[511] != 0xAA) {
                        printk("Bad partition table on drive %d\n\r",drive);
                        panic("");
                }
                p = 0x1BE + (void *)bh->b_data;
                for (i=1;i<5;i++,p++) {
                        hd[i+5*drive].start_sect = p->start_sect;
                        hd[i+5*drive].nr_sects = p->nr_sects;
                }
                brelse(bh);
        }
        if (NR_HD)
                printk("Partition table%s ok.\n\r",(NR_HD>1)?"s":"");
        rd_load();
        mount_root();
        return (0);
}

static int controller_ready(void)
{
        int retries=10000;

        while (--retries && (inb_p(HD_STATUS)&0xc0)!=0x40);
        return (retries);
}

static int win_result(void)
{
        int i=inb_p(HD_STATUS);

        if ((i & (BUSY_STAT | READY_STAT | WRERR_STAT | SEEK_STAT | ERR_STAT))
            == (READY_STAT | SEEK_STAT))
                return(0); /* ok */
        if (i&1) i=inb(HD_ERROR);
        return (1);
}

static void hd_out(unsigned int drive,unsigned int nsect,unsigned int sect,
                   unsigned int head,unsigned int cyl,unsigned int cmd,
                   void (*intr_addr)(void))
{
        register int port asm("dx");

        if (drive>1 || head>15)
                panic("Trying to write bad sector");
        if (!controller_ready())
                panic("HD controller not ready");
        do_hd = intr_addr;
        outb_p(hd_info[drive].ctl,HD_CMD);
        port=HD_DATA;
        outb_p(hd_info[drive].wpcom>>2,++port);
        outb_p(nsect,++port);
        outb_p(sect,++port);
        outb_p(cyl,++port);
        outb_p(cyl>>8,++port);
        outb_p(0xA0|(drive<<4)|head,++port);
        outb(cmd,++port);
}

static int drive_busy(void)
{
        unsigned int i;

        for (i = 0; i < 10000; i++)
                if (READY_STAT == (inb_p(HD_STATUS) & (BUSY_STAT|READY_STAT)))
                        break;
        i = inb(HD_STATUS);
        i &= BUSY_STAT | READY_STAT | SEEK_STAT;
        if (i == (READY_STAT | SEEK_STAT))
                return(0);
        printk("HD controller times out\n\r");
        return(1);
}

static void reset_controller(void)
{
        int	i;

        outb(4,HD_CMD);
        for(i = 0; i < 100; i++) nop();
        outb(hd_info[0].ctl & 0x0f ,HD_CMD);
        if (drive_busy())
                printk("HD-controller still busy\n\r");
        if ((i = inb(HD_ERROR)) != 1)
                printk("HD-controller reset failed: %02x\n\r",i);
}

static void reset_hd(int nr)
{
        reset_controller();
        hd_out(nr,hd_info[nr].sect,hd_info[nr].sect,hd_info[nr].head-1,
               hd_info[nr].cyl,WIN_SPECIFY,&recal_intr);
}

void unexpected_hd_interrupt(void)
{
        printk("Unexpected HD interrupt\n\r");
}

static void bad_rw_intr(void)
{
        if (++CURRENT->errors >= MAX_ERRORS)
                end_request(0);
        if (CURRENT->errors > MAX_ERRORS/2)
                reset = 1;
}

static void read_intr(void)
{
        if (win_result()) {
                bad_rw_intr();
                do_hd_request();
                return;
        }
        port_read(HD_DATA,CURRENT->buffer,256);
        CURRENT->errors = 0;
        CURRENT->buffer += 512;
        CURRENT->sector++;
        if (--CURRENT->nr_sectors) {
                do_hd = &read_intr;
                return;
        }
        end_request(1);
        do_hd_request();
}

static void write_intr(void)
{
        if (win_result()) {
                bad_rw_intr();
                do_hd_request();
                return;
        }
        if (--CURRENT->nr_sectors) {
                CURRENT->sector++;
                CURRENT->buffer += 512;
                do_hd = &write_intr;
                port_write(HD_DATA,CURRENT->buffer,256);
                return;
        }
        end_request(1);
        do_hd_request();
}

static void recal_intr(void)
{
        if (win_result())
                bad_rw_intr();
        do_hd_request();
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

void hd_init(void)
{
        blk_dev[MAJOR_NR].request_fn = DEVICE_REQUEST;
        set_intr_gate(0x2E,&hd_interrupt);
        outb_p(inb_p(0x21)&0xfb,0x21);
        outb(inb_p(0xA1)&0xbf,0xA1);
}
