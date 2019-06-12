/*
 *  linux/kernel/console.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 *	console.c
 *
 * This module implements the console io functions
 *	'void con_init(void)'
 *	'void con_write(struct tty_queue * queue)'
 * Hopefully this will be a rather complete VT102 implementation.
 *
 * Beeping thanks to John T Kohl.
 */

/**
 * 该模块实现控制台的输入/输出功能
 * 'void con_init(void)': 初始化控制台
 * 'void con_write(struct tty_queue * queue)': 控制台显示字符
 *  
 */
/*
 *  NOTE!!! We sometimes disable and enable interrupts for a short while
 * (to put a word in video IO), but this will work even for keyboard
 * interrupts. We know interrupts aren't enabled when getting a keyboard
 * interrupt, as we use trap-gates. Hopefully all is well.
 */

/*
 * 注意！！！ 我们这里有时短暂地关闭和开启中断（当输出一个字到视频IO），但即便如此键盘中断依旧可以工作
 * 因为我们知道在处理一个键盘中断过程时候中断是被禁止的
 * 希望一切工作正常
 * 
 */

/*
 * Code to check for different video-cards mostly by Galen Hunt,
 * <g-hunt@ee.utah.edu>
 */

#include <linux/sched.h>
#include <linux/tty.h>
#include <asm/io.h>
#include <asm/system.h>

/*
 * These are set up by the setup-routine at boot-time:
 */

/**
 * 这些是setup程序在引导启动系统时设置的参数
 *
 * (*(unsigned char *)0x90000) 表示从绝对物理地址0x90000处获得数据，这个数据被解释成一个unsigned char，其对应的值是初始光标列号
 * 其他参数依次类推
 *
 */
#define ORIG_X			(*(unsigned char *)0x90000) // 初始光标列号
#define ORIG_Y			(*(unsigned char *)0x90001) // 初始光标行号
#define ORIG_VIDEO_PAGE		(*(unsigned short *)0x90004) // 初始显示页面
#define ORIG_VIDEO_MODE		((*(unsigned short *)0x90006) & 0xff) // 显示模式
#define ORIG_VIDEO_COLS 	(((*(unsigned short *)0x90006) & 0xff00) >> 8) // 屏幕列数
#define ORIG_VIDEO_LINES	(25) // 屏幕行数
#define ORIG_VIDEO_EGA_AX	(*(unsigned short *)0x90008)
#define ORIG_VIDEO_EGA_BX	(*(unsigned short *)0x9000a) // 显存大小和彩色模式
#define ORIG_VIDEO_EGA_CX	(*(unsigned short *)0x9000c) // 显卡独有特性参数

/** 
 * 定义显示模式的类型常数
 */
#define VIDEO_TYPE_MDA		0x10	/* Monochrome Text Display	*/ //单色文本
#define VIDEO_TYPE_CGA		0x11	/* CGA Display 			*/ // CGA显示器
#define VIDEO_TYPE_EGAM		0x20	/* EGA/VGA in Monochrome Mode	*/ // VGA显示器
#define VIDEO_TYPE_EGAC		0x21	/* EGA/VGA in Color Mode	*/ // EGA/VGA彩色

#define NPAR 16 // 转义字符序列中的最大参数个数

extern void keyboard_interrupt(void); // 键盘中断处理函数，在keyboard.S中实现

static unsigned char	video_type;		// 显示类型
static unsigned long	video_num_columns;	// 屏幕文本列数
static unsigned long	video_size_row;		// 屏幕每行使用的字节数
static unsigned long	video_num_lines;	// 屏幕文本行数
static unsigned char	video_page;		// 初始显示页面
static unsigned long	video_mem_start;	// 显存起始地址
static unsigned long	video_mem_end;		// 显存结束地址
static unsigned short	video_port_reg;		// 显卡控制器的“选择寄存器”端口
static unsigned short	video_port_val;		// 显卡控制器的“数据寄存器”端口
static unsigned short	video_erase_char;	// 擦除字符属性及字符(0x0720) 

static unsigned long	origin;		/* Used for EGA/VGA fast scroll	*/ // 快速滚屏操作起始内存地址
static unsigned long	scr_end;	/* Used for EGA/VGA fast scroll	*/ // 快速滚屏操作末端内存地址
static unsigned long	pos; // 当前光标对应的显存位置
static unsigned long	x,y; // 当前光标的列，行值
static unsigned long	top,bottom; // 滚动是顶行，底行的行号
static unsigned long	state=0; // 处理转义字符或转义序列的当前状态
static unsigned long	npar,par[NPAR]; // 转义序列的参数个数，以及保存转义序列的参数数组
static unsigned long	ques=0; // 问号字符
static unsigned char	attr=0x07; // 字符属性

static void sysbeep(void);

/*
 * this is what the terminal answers to a ESC-Z or csi0c
 * query (= vt100 response).
 */

/*
 * 下面是终端响应'ESC-Z'或'csi0c'请求的应答（=vt100回响应）
 *
 * csi: 控制序列引导码
 * 主机通过发送不带参数或参数是0的“设备属性控制序列”（'Esc [c'或'Esc [0c'）要求终端回答一个设备属性控制序列（Esc-Z的功能于此类似）
 * 终端则发送以下序列('Esc [?1;2c')来响应主机，表示终端是具有高级视频功能的VT100兼容终端
 */
#define RESPONSE "\033[?1;2c" // “中断属性控制序列”：('Esc [?1;2c')

/* NOTE! gotoxy thinks x==video_num_columns is ok */

/*
 * 跟踪光标当前的位置
 *
 * new_x: 光标所在列号
 * new_y: 光标所在行号
 *
 * 无返回
 *
 * 更新光标当前的位置变量x,y，并修正光标在内存中对应的地址pos
 * 
 */
static inline void gotoxy(unsigned int new_x,unsigned int new_y)
{
        // 检查参数的有效性
        if (new_x > video_num_columns || new_y >= video_num_lines) // “给定的光标列号”超出“显示的最大列数” 或 “给定的光标行号”不小于“显示的最大行数”
                return; // 直接返回
        x=new_x; // 更新当前光标的列号
        y=new_y; // 更新当前光标的行号
        // 因为1个光标点需要用2个字节表示，所以'x<<1'
        pos=origin + y*video_size_row + (x<<1); // 更新当前光标在内存中的地址（注意：这是绝对地址！）
}

/*
 * 设置滚屏起始显存地址
 *
 * 无参数
 *
 * 无返回
 *
 */
static inline void set_origin(void)
{
        cli(); // 关闭中断
        outb_p(12, video_port_reg); // 向显卡的选择端口写入12：选择显示控制数据寄存器r12
        // 滚屏起始位置计算方式 (origin-video_mem_start)/2，其中origin表示光标在内存中的地址，video_mem_start和显卡有关，彩色显卡一般是0xb8000
        // 滚屏起始位置右移9位，高字节清零（实际上表示向右移动8位再除以2，因为每个点需要2个字节来表示）
        outb_p(0xff&((origin-video_mem_start)>>9), video_port_val); // 向显卡的数据端口写入“滚屏起始位置”的高字节
        outb_p(13, video_port_reg); // 向显卡的选择端口写入13：选择显示控制数据寄存器r13
        // 滚屏起始位置右移1位，高字节清零（同样需要除以2）
        outb_p(0xff&((origin-video_mem_start)>>1), video_port_val); // 向显卡的数据端口写入“滚屏起始位置”的低字节
        sti(); // 打开中断
}

/*
 * 向上翻滚一行：实际上是当前显存中的某个区域在内存中向下移动一行
 *
 * 无参数
 *
 * 无返回
 * 
 */
static void scrup(void)
{
        if (video_type == VIDEO_TYPE_EGAC || video_type == VIDEO_TYPE_EGAM) // VGA显卡，EGA显卡：可以指定范围（区域）进行滚屏操作
        {
                // 移动起始行 top == 0 并且移动最底行 bottom == video_num_lines(25)：整个屏幕向下移动一行
                if (!top && bottom == video_num_lines) {
                        origin += video_size_row; // 屏幕左上角对应的起始内存位置origin调整为向下移动一行对应的内存地址
                        pos += video_size_row; // 跟踪跳转当前光标所在的内存地址为向下一行
                        scr_end += video_size_row; // 跳转屏幕末行末端指针src_end所在的内存地址
                        if (scr_end > video_mem_end) { // 如果屏幕未行末端指针src_end的地址超出显存了
                                /*
                                 * 将屏幕内容除原来第一行以外所有行对应的内存数据移动到video_mem_start处，并在向下移动出现的新行处（应该是最后一行）填入空格字符
                                 * 
                                 * %0 - eax(擦除字符+属性) video_erase_char
                                 * %1 - ecx ：((屏幕字符行数-1)所对应的字符数)/2，以长字移动
                                 * %2 - edx: 显存起始内存地址 video_mem_start
                                 * %3 - esi: 屏幕内存起始位置 origin
                                 */
                                __asm__("cld\n\t" // 清方向位
                                        "rep\n\t" // 重复拷贝：将当前屏幕内存数据移动到显存起始处
                                        "movsl\n\t" 
                                        "movl video_num_columns,%1\n\t" // 最后一行用擦除字符来填充
                                        "rep\n\t" 
                                        "stosw" // 写入内存中
                                        ::"a" (video_erase_char),
                                         "c" ((video_num_lines-1)*video_num_columns>>1),
                                         "D" (video_mem_start),
                                         "S" (origin)
                                        );
                                scr_end -= origin-video_mem_start; // 屏幕末行末端位置减少（origin - video_mem_start）
                                pos -= origin-video_mem_start; // 当前光标地址减少了(origin - video_mem_start) 
                                origin = video_mem_start; // 屏幕起始地址为显存起始地址
                        } else { // 屏幕末端没有超出显存
                                // 这里只需要用擦除字符填充新行即可，下面汇编和上面类似
                                __asm__("cld\n\t"
                                        "rep\n\t"
                                        "stosw"
                                        ::"a" (video_erase_char),
                                         "c" (video_num_columns),
                                         "D" (scr_end-video_size_row)
                                        );
                        }
                        set_origin(); // 把新屏幕的滚动窗口内存起始地址写入显卡控制器
                } else {
                        // 滚动某段区域（top行到bottom行）
                        // 直接把屏幕从top行到bottom行中每一行向上移动一行，并在新行填入擦除字符
                        __asm__("cld\n\t"
                                "rep\n\t" // 循环操作，将top+1行到bottom行所有的字符移动到top行位置
                                "movsl\n\t" 
                                "movl video_num_columns,%%ecx\n\t" // 在新行中填入擦除字符
                                "rep\n\t"
                                "stosw"
                                ::"a" (video_erase_char),
                                 "c" ((bottom-top-1)*video_num_columns>>1), // (top + 1)行到bottom行所对应的内存长字数
                                 "D" (origin+video_size_row*top), // top行所对应的内存地址
                                 "S" (origin+video_size_row*(top+1)) // (top + 1)行的内存地址
                                );
                }
        }
        else		/* Not EGA/VGA */ 
        {
                // MDA显卡：只能整屏滚动，并且会自动调整超出显卡范围的情况，所以这里不对超出显存做单独处理
                // 这里处理方法和EGA非整屏移动完全一样
                __asm__("cld\n\t"
                        "rep\n\t"
                        "movsl\n\t"
                        "movl video_num_columns,%%ecx\n\t"
                        "rep\n\t"
                        "stosw"
                        ::"a" (video_erase_char),
                         "c" ((bottom-top-1)*video_num_columns>>1),
                         "D" (origin+video_size_row*top),
                         "S" (origin+video_size_row*(top+1))
                        );
        }
}

/*
 * 向下滚动一行：将屏幕对应的显存中的滚动窗口向上移动一行，并在移动开始行的上方出现一新行
 *
 * 
 */
static void scrdown(void)
{
        if (video_type == VIDEO_TYPE_EGAC || video_type == VIDEO_TYPE_EGAM)
        {
                /*
                 * 把从top行到bottom-1行的内存数据拷贝到top+1行到bottom行，top行用擦除字符填充
                 *
                 * %0 - eax: video_erase_char 擦除字符 + 属性
                 * %1 - ecx: ((bottom-top-1)*video_num_columns>>1) top行到bottom-1行对应的内存长字数
                 * %2 - edi: (origin+video_size_row*bottom-4) 窗口右下角最后一个长字位置
                 * %3 - esi: (origin+video_size_row*(bottom-1)-4) 窗口倒数第二行最后一个长字位置
                 *
                 * 移动方向 [edi] -> [esi], 移动ecx个长字
                 *
                 * 注意：拷贝是逆向处理的，即先从bottom-1行开始，拷贝到bottom行，依次类推到top行为止
                 * 这是为了避免在移动显存数据时候不会出现数据覆盖的情况，否则就会出现top行的数据一直复制到bottom行为止 :-(
                 * 
                 */
                __asm__("std\n\t" // 设置拷贝的方向位 
                        "rep\n\t" // 重复操作，向下移动从top行到bottom -1 行对应的内存数据
                        "movsl\n\t"
                        // edi 已经减4,所以也是反向填充擦除字符
                        "addl $2,%%edi\n\t"	/* %edi has been decremented by 4 */
                        "movl video_num_columns,%%ecx\n\t"
                        "rep\n\t" // 将擦除字符填入最上面的新行中
                        "stosw" 
                        ::"a" (video_erase_char),
                         "c" ((bottom-top-1)*video_num_columns>>1),
                         "D" (origin+video_size_row*bottom-4),
                         "S" (origin+video_size_row*(bottom-1)-4)
                        );
        }
        else		/* Not EGA/VGA */
        {
                // 这里MDA显卡和VGA显卡处理完全一样
                __asm__("std\n\t"
                        "rep\n\t"
                        "movsl\n\t"
                        "addl $2,%%edi\n\t"	/* %edi has been decremented by 4 */
                        "movl video_num_columns,%%ecx\n\t"
                        "rep\n\t"
                        "stosw"
                        ::"a" (video_erase_char),
                         "c" ((bottom-top-1)*video_num_columns>>1),
                         "D" (origin+video_size_row*bottom-4),
                         "S" (origin+video_size_row*(bottom-1)-4)
                        );
        }
}

/*
 * 光标在同列位置下移一行
 * 
 */
static void lf(void)
{
        if (y+1<bottom) { // 光标没有处在最后一行
                y++; // 当前光标的行值加1 
                pos += video_size_row; // 当前光标的内存地址加“一行所占用的内存字节数“
                return;
        }
        scrup(); // 屏幕窗口的内容上移一行
}

/*
 * 光标在同列位置上移一行
 * 
 */
static void ri(void)
{
        if (y>top) { // 光标不在最开始一行
                y--; // 当前光标的行值减1
                pos -= video_size_row; // 当前光标的内存地址减”一行所占用的内存字节数“
                return;
        }
        scrdown(); // 屏幕窗口的内容下移一行
}

/*
 * 光标回到左边第一列: x = 0 
 * 
 */
static void cr(void)
{
        // 注意：每列需要2个字节来保存，因此这里 x << 1
        pos -= x<<1; //当前光标内存地址减去”0列到光标所占用的内存字节数“
        x=0; //当前光标的列值设置为0
}

/*
 * 删除光标前一个字符（用”空格“代替）
 * 
 */
static void del(void)
{
        if (x) { // 光标没有处在0列
                pos -= 2; // 光标对应的内存位置后退2字节
                x--; // 当前光标的列值减1
                *(unsigned short *)pos = video_erase_char; // 把“擦除字符”写入当前光标的位置
        }
}

/*
 * 删除屏幕上与光标位置相关的部分
 *
 * par: 0 - 删除光标到屏幕底端，1 - 删除屏幕开始到光标，2 - 整屏删除
 *
 * csi(Control Sequence Introducer) 控制序列引导码
 * 
 */
static void csi_J(int par)
{
        long count;
        long start;

        switch (par) {
		case 0:	/* erase from cursor to end of display */
                count = (scr_end-pos)>>1; // 屏幕低端到当前光标所占用的字节数
                start = pos; // 从当前光标位置开始删除
                break;
		case 1:	/* erase from start to cursor */
                count = (pos-origin)>>1; // 屏幕开头到当前光标所占用的字节数
                start = origin; // 从屏幕开头开始删除
                break;
		case 2: /* erase whole display */
                count = video_num_columns * video_num_lines; // 整屏占用的字节数
                start = origin; // 从屏幕开头开始删除
                break;
		default:
                return;
        }
        // 从start位置开始写入count个擦除字符对应的字
        __asm__("cld\n\t"
                "rep\n\t"
                "stosw\n\t"
                ::"c" (count),
                 "D" (start),"a" (video_erase_char)
                );
}

/*
 * 删除一行上与光标位置相关的部分
 *
 * par: 0 - 删除从当前光标到行末，1 - 删除行首到当前光标，2 - 删除当前行
 */
static void csi_K(int par)
{
        long count;
        long start;

        switch (par) {
		case 0:	/* erase from cursor to end of line */
                if (x>=video_num_columns) // 当前列数已经在最末端，直接返回
                        return;
                count = video_num_columns-x; // 删除的列数
                start = pos; // 从当前位置开始删除
                break;
		case 1:	/* erase from start of line to cursor */
                start = pos - (x<<1); // 从行首开始删除
                count = (x<video_num_columns)?x:video_num_columns; // 删除的列数
                break;
		case 2: /* erase whole line */
                start = pos - (x<<1); // 从行首开始删除
                count = video_num_columns; // 删除整行的列数
                break;
		default:
                return;
        }

        // 使用擦除字符填写被删除的地方
        __asm__("cld\n\t"
                "rep\n\t"
                "stosw\n\t"
                ::"c" (count),
                 "D" (start),"a" (video_erase_char)
                );
}


/*
 * 设置显示字符属性：根据参数设置字符显示属性，以后所有发送到终端的字符都将使用这里定义的属性，直到再次执行本控制序列来重新设置属性
 *
 * 0 - 默认属性，1 - 粗体并增亮，4 - 下划线，5 - 闪烁，7 - 反显， 27 - 正显
 *
 */
void csi_m(void)
{
        int i;

        // 注意：一个控制序列可以带多个不同的参数。参数存储在数组par[]中，参数个数保存在npar
        // 这里循环遍历整个数组来处理o素有的属性
        for (i=0;i<=npar;i++)
                switch (par[i]) {
                case 0:attr=0x07;break; // 默认属性： 0x07 表示黑底白字
                case 1:attr=0x0f;break; // 粗体增亮
                case 4:attr=0x0f;break; // 下划线？？？ 这里应该是有错误
                case 7:attr=0x70;break; // 反显：白底黑字
                case 27:attr=0x07;break; // 正显：黑底白字
                }
}

/*
 * 设置显示光标
 * 
 */
static inline void set_cursor(void)
{
        cli(); // 关闭中断
        outb_p(14, video_port_reg); // 向显卡的选择端口写入14：选择显示控制数据寄存器r14
        outb_p(0xff&((pos-video_mem_start)>>9), video_port_val); // 向显卡的数据端口写入“光标当前位置”的高字节
        outb_p(15, video_port_reg); // 向显卡的选择端口写入15：选择显示控制数据寄存器r15
        outb_p(0xff&((pos-video_mem_start)>>1), video_port_val); // 向显卡的数据端口写入“光标当前位置”的低字节
        sti(); // 开启中断
}

/*
 * 发送对VT100的响应序列
 *
 * 主机通过发送不带参数或参数是0的“设备属性控制序列”（'Esc [c'或'Esc [0c'）要求终端回答一个设备属性控制序列（Esc-Z的功能于此类似）
 * 终端则发送以下序列('Esc [?1;2c')来响应主机，表示终端是具有高级视频功能的VT100兼容终端
 *
 * 处理过程：将应答序列放入读缓冲队列，并使用copy_to_cooked函数处理后放入辅助队列中
 * 
 */
static void respond(struct tty_struct * tty)
{
        char * p = RESPONSE;

        cli(); 
        while (*p) {
                PUTCH(*p,tty->read_q); // 将应答序列放入读队列
                p++; // 逐字符放入
        }
        sti();
        copy_to_cooked(tty); // 转换成归范模式（放入辅助队列中）
}

static void insert_char(void)
{
        int i=x;
        unsigned short tmp, old = video_erase_char;
        unsigned short * p = (unsigned short *) pos;

        while (i++<video_num_columns) {
                tmp=*p;
                *p=old;
                old=tmp;
                p++;
        }
}

static void insert_line(void)
{
        int oldtop,oldbottom;

        oldtop=top;
        oldbottom=bottom;
        top=y;
        bottom = video_num_lines;
        scrdown();
        top=oldtop;
        bottom=oldbottom;
}

static void delete_char(void)
{
        int i;
        unsigned short * p = (unsigned short *) pos;

        if (x>=video_num_columns)
                return;
        i = x;
        while (++i < video_num_columns) {
                *p = *(p+1);
                p++;
        }
        *p = video_erase_char;
}

static void delete_line(void)
{
        int oldtop,oldbottom;

        oldtop=top;
        oldbottom=bottom;
        top=y;
        bottom = video_num_lines;
        scrup();
        top=oldtop;
        bottom=oldbottom;
}

static void csi_at(unsigned int nr)
{
        if (nr > video_num_columns)
                nr = video_num_columns;
        else if (!nr)
                nr = 1;
        while (nr--)
                insert_char();
}

static void csi_L(unsigned int nr)
{
        if (nr > video_num_lines)
                nr = video_num_lines;
        else if (!nr)
                nr = 1;
        while (nr--)
                insert_line();
}

static void csi_P(unsigned int nr)
{
        if (nr > video_num_columns)
                nr = video_num_columns;
        else if (!nr)
                nr = 1;
        while (nr--)
                delete_char();
}

static void csi_M(unsigned int nr)
{
        if (nr > video_num_lines)
                nr = video_num_lines;
        else if (!nr)
                nr=1;
        while (nr--)
                delete_line();
}

static int saved_x=0;
static int saved_y=0;

static void save_cur(void)
{
        saved_x=x;
        saved_y=y;
}

static void restore_cur(void)
{
        gotoxy(saved_x, saved_y);
}

void con_write(struct tty_struct * tty)
{
        int nr;
        char c;

        nr = CHARS(tty->write_q);
        while (nr--) {
                GETCH(tty->write_q,c);
                switch(state) {
                case 0:
                        if (c>31 && c<127) {
                                if (x>=video_num_columns) {
                                        x -= video_num_columns;
                                        pos -= video_size_row;
                                        lf();
                                }
                                __asm__("movb attr,%%ah\n\t"
                                        "movw %%ax,%1\n\t"
                                        ::"a" (c),"m" (*(short *)pos)
                                        );
                                pos += 2;
                                x++;
                        } else if (c==27)
                                state=1;
                        else if (c==10 || c==11 || c==12)
                                lf();
                        else if (c==13)
                                cr();
                        else if (c==ERASE_CHAR(tty))
                                del();
                        else if (c==8) {
                                if (x) {
                                        x--;
                                        pos -= 2;
                                }
                        } else if (c==9) {
                                c=8-(x&7);
                                x += c;
                                pos += c<<1;
                                if (x>video_num_columns) {
                                        x -= video_num_columns;
                                        pos -= video_size_row;
                                        lf();
                                }
                                c=9;
                        } else if (c==7)
                                sysbeep();
                        break;
                case 1:
                        state=0;
                        if (c=='[')
                                state=2;
                        else if (c=='E')
                                gotoxy(0,y+1);
                        else if (c=='M')
                                ri();
                        else if (c=='D')
                                lf();
                        else if (c=='Z')
                                respond(tty);
                        else if (x=='7')
                                save_cur();
                        else if (x=='8')
                                restore_cur();
                        break;
                case 2:
                        for(npar=0;npar<NPAR;npar++)
                                par[npar]=0;
                        npar=0;
                        state=3;
                        if ((ques=(c=='?')))
                                break;
                case 3:
                        if (c==';' && npar<NPAR-1) {
                                npar++;
                                break;
                        } else if (c>='0' && c<='9') {
                                par[npar]=10*par[npar]+c-'0';
                                break;
                        } else state=4;
                case 4:
                        state=0;
                        switch(c) {
                        case 'G': case '`':
                                if (par[0]) par[0]--;
                                gotoxy(par[0],y);
                                break;
                        case 'A':
                                if (!par[0]) par[0]++;
                                gotoxy(x,y-par[0]);
                                break;
                        case 'B': case 'e':
                                if (!par[0]) par[0]++;
                                gotoxy(x,y+par[0]);
                                break;
                        case 'C': case 'a':
                                if (!par[0]) par[0]++;
                                gotoxy(x+par[0],y);
                                break;
                        case 'D':
                                if (!par[0]) par[0]++;
                                gotoxy(x-par[0],y);
                                break;
                        case 'E':
                                if (!par[0]) par[0]++;
                                gotoxy(0,y+par[0]);
                                break;
                        case 'F':
                                if (!par[0]) par[0]++;
                                gotoxy(0,y-par[0]);
                                break;
                        case 'd':
                                if (par[0]) par[0]--;
                                gotoxy(x,par[0]);
                                break;
                        case 'H': case 'f':
                                if (par[0]) par[0]--;
                                if (par[1]) par[1]--;
                                gotoxy(par[1],par[0]);
                                break;
                        case 'J':
                                csi_J(par[0]);
                                break;
                        case 'K':
                                csi_K(par[0]);
                                break;
                        case 'L':
                                csi_L(par[0]);
                                break;
                        case 'M':
                                csi_M(par[0]);
                                break;
                        case 'P':
                                csi_P(par[0]);
                                break;
                        case '@':
                                csi_at(par[0]);
                                break;
                        case 'm':
                                csi_m();
                                break;
                        case 'r':
                                if (par[0]) par[0]--;
                                if (!par[1]) par[1] = video_num_lines;
                                if (par[0] < par[1] &&
                                    par[1] <= video_num_lines) {
                                        top=par[0];
                                        bottom=par[1];
                                }
                                break;
                        case 's':
                                save_cur();
                                break;
                        case 'u':
                                restore_cur();
                                break;
                        }
                }
        }
        set_cursor();
}

/*
 *  void con_init(void);
 *
 * This routine initalizes console interrupts, and does nothing
 * else. If you want the screen to clear, call tty_write with
 * the appropriate escape-sequece.
 *
 * Reads the information preserved by setup.s to determine the current display
 * type and sets everything accordingly.
 */
void con_init(void)
{
        register unsigned char a;
        char *display_desc = "????";
        char *display_ptr;

        video_num_columns = ORIG_VIDEO_COLS;
        video_size_row = video_num_columns * 2;
        video_num_lines = ORIG_VIDEO_LINES;
        video_page = ORIG_VIDEO_PAGE;
        video_erase_char = 0x0720;
	
        if (ORIG_VIDEO_MODE == 7)			/* Is this a monochrome display? */
        {
                video_mem_start = 0xb0000;
                video_port_reg = 0x3b4;
                video_port_val = 0x3b5;
                if ((ORIG_VIDEO_EGA_BX & 0xff) != 0x10)
                {
                        video_type = VIDEO_TYPE_EGAM;
                        video_mem_end = 0xb8000;
                        display_desc = "EGAm";
                }
                else
                {
                        video_type = VIDEO_TYPE_MDA;
                        video_mem_end	= 0xb2000;
                        display_desc = "*MDA";
                }
        }
        else								/* If not, it is color. */
        {
                video_mem_start = 0xb8000;
                video_port_reg	= 0x3d4;
                video_port_val	= 0x3d5;
                if ((ORIG_VIDEO_EGA_BX & 0xff) != 0x10)
                {
                        video_type = VIDEO_TYPE_EGAC;
                        video_mem_end = 0xbc000;
                        display_desc = "EGAc";
                }
                else
                {
                        video_type = VIDEO_TYPE_CGA;
                        video_mem_end = 0xba000;
                        display_desc = "*CGA";
                }
        }

        /* Let the user known what kind of display driver we are using */
	
        display_ptr = ((char *)video_mem_start) + video_size_row - 8;
        while (*display_desc)
        {
                *display_ptr++ = *display_desc++;
                display_ptr++;
        }
	
        /* Initialize the variables used for scrolling (mostly EGA/VGA)	*/
	
        origin	= video_mem_start;
        scr_end	= video_mem_start + video_num_lines * video_size_row;
        top	= 0;
        bottom	= video_num_lines;

        gotoxy(ORIG_X,ORIG_Y);
        set_trap_gate(0x21,&keyboard_interrupt);
        outb_p(inb_p(0x21)&0xfd,0x21);
        a=inb_p(0x61);
        outb_p(a|0x80,0x61);
        outb(a,0x61);
}
/* from bsd-net-2: */

void sysbeepstop(void)
{
        /* disable counter 2 */
        outb(inb_p(0x61)&0xFC, 0x61);
}

int beepcount = 0;

static void sysbeep(void)
{
        /* enable counter 2 */
        outb_p(inb_p(0x61)|3, 0x61);
        /* set command for counter 2, 2 byte write */
        outb_p(0xB6, 0x43);
        /* send 0x637 for 750 HZ */
        outb_p(0x37, 0x42);
        outb(0x06, 0x42);
        /* 1/8 second */
        beepcount = HZ/8;	
}
