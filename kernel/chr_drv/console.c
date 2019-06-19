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
#define VIDEO_TYPE_EGAM		0x20	/* EGA/VGA in Monochrome Mode	*/ // VGA单色
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

/*
 * 在当前光标处插入一个空格字符
 * 
 */
static void insert_char(void)
{
        int i=x;
        unsigned short tmp, old = video_erase_char;
        unsigned short * p = (unsigned short *) pos; // 当前光标的内存地址

        // 从当前光标的后面一列开始，直到当前行的末端结束
        // 把擦除字符放在当前光标处，然后把当前光标开始的所有字符右移一个一列
        while (i++<video_num_columns) {
                tmp=*p;
                *p=old;
                old=tmp;
                p++;
        }
}

/*
 * 在当前光标处插入一行：屏幕窗口从当前光标所处的行到屏幕最底行向下卷动一行，光标将停留在插入的新行上
 * 
 */
static void insert_line(void)
{
        int oldtop,oldbottom;

        oldtop=top; // 保存屏幕还没滚动时的开始行的行号
        oldbottom=bottom; // 保存屏幕还没滚动时的结束行的行号
        top=y;// 设置屏幕滚动的开始行为当前行
        bottom = video_num_lines; // 设置屏幕滚动的结束行为屏幕最底下行
        scrdown(); // 屏幕向下滚动一行
        top=oldtop; // 恢复原来保存的top和bottom行号
        bottom=oldbottom;
}

/*
 * 删除当前光标所处的一个字符
 * 
 */
static void delete_char(void)
{
        int i;
        // 注意：因为当前光标不能变，所以必须使用另外一个指针p来做遍历！
        unsigned short * p = (unsigned short *) pos; // 当前光标的内存位置

        if (x>=video_num_columns) // x 超出当前行的最大列数，直接返回
                return;
        i = x;

        // 从当前光标开始遍历，直到最后一列结束，所有字符左移一列
        while (++i < video_num_columns) {
                *p = *(p+1);
                p++;
        }
        *p = video_erase_char; // 当前行的最末尾插入一个擦除字符
}

/*
 * 删除当前光标所处的行：从光标开始的那行到屏幕最底下行向上卷动一行，光标停留在原来行
 *
 */
static void delete_line(void)
{
        int oldtop,oldbottom;

        oldtop=top; // 保存屏幕还没滚动时的开始行的行号
        oldbottom=bottom; // 保存屏幕还没滚动时的结束行的行号
        top=y;// 设置屏幕滚动的开始行为当前行
        bottom = video_num_lines; // 设置屏幕滚动的结束行为屏幕最底下行
        scrup(); // 屏幕窗口的内容上移一行
        top=oldtop; // 恢复原来保存的top和bottom行号
        bottom=oldbottom;
}

/*
 * 在当前光标处插入nr个空格字符，光标仍将处于第一个被插入的空格字符处
 *
 * nr: 插入的空格字符个数，默认为1
 *
 */
static void csi_at(unsigned int nr)
{
        if (nr > video_num_columns) // 如果插入的空格字符个数大于1行显示的字符个数，则截短为1行显示的字符个数 
                nr = video_num_columns;
        else if (!nr) // 如果nr == 0，则默认为插入1个空格字符
                nr = 1;
        while (nr--) // 循环插入指定的空格字符
                insert_char();
}

/*
 * 在光标位置插入nr行
 *
 * nr: 插入的行数，默认为1
 * 
 */
static void csi_L(unsigned int nr)
{
        if (nr > video_num_lines) // 如果插入的行数个数大于屏幕显示行数，则截短为屏幕显示行数
                nr = video_num_lines;
        else if (!nr) // 如果nr == 0，则默认为插入1行
                nr = 1;
        while (nr--) // 循环插入指定的行
                insert_line();
}

/*
 * 删除光标处的nr个字符
 *
 * nr: 删除的字符个数，默认为1
 * 
 */
static void csi_P(unsigned int nr)
{
        if (nr > video_num_columns) // 如果删除的字符个数大于1行显示的字符个数，则截短为1行显示字符个数
                nr = video_num_columns;
        else if (!nr) // 如果nr == 0，则默认为删除1个字符
                nr = 1;
        while (nr--) // 循环删除指定的字符
                delete_char();
}

/*
 * 删除光标处的nr行
 * 
 * nr: 删除的行数，默认为1
 *
 */
static void csi_M(unsigned int nr)
{
        if (nr > video_num_lines) // 如果删除的行数个数大于屏幕显示行数，则截短为屏幕显示行数
                nr = video_num_lines;
        else if (!nr) // 如果nr == 0，则默认为删除1行
                nr=1;
        while (nr--) // 循环删除指定的行
                delete_line();
}

static int saved_x=0;
static int saved_y=0;

/*
 * 临时保存当前光标的行号，列号
 * 
 */
static void save_cur(void)
{
        saved_x=x;
        saved_y=y;
}

/*
 * 恢复临时保存的光标位置
 * 
 */
static void restore_cur(void)
{
        gotoxy(saved_x, saved_y);
}

/**
 * 控制台终端写函数
 *
 * tty: 当前控制台使用的tty结构指针
 *
 * 无返回
 * 
 * 从控制台终端对应的写缓冲队列取字符，针对每个字符进行分析：
 * 如果字符是控制字符、转义或控制序列，则进行光标定位，字符删除等控制处理，对于普通字符则直接在光标处显示
 * 
 */
void con_write(struct tty_struct * tty)
{
        int nr;
        char c;

        /*
         * 获取控制台终端写队列中的字符数nr，并循环取出每个字符进行处理：
         * 如果控制台由于收到键盘或程序发出的暂停命令（如按键Ctrl-S）而处于停止状态，那么本函数就停止处理写队列中的字符，退出本函数
         * 另外，如果取出的是控制字符CAN(24)或SUB(26)，并且在转义或控制序列期间收到，则序列不会执行而立刻终止，同时显示随后的字符
         *
         * 注意：con_write只处理取队列字符时写队列当前含有的字符，而当一个序列正被放到写队列期间如果触发con_write，
         * 则本函数退出的时候state就处于处理转义或控制序列的其他状态上！！！
         * 
         */
        nr = CHARS(tty->write_q); // 获取控制台终端写队列的字符数
        while (nr--) { // 循环读取
                GETCH(tty->write_q,c); // 从写队列缓冲区取走一个字符，赋值给c变量
                switch(state) { // 根据当前状态对取到的字符进行相关处理
                        /*
                         * 初始正常状态，此时若接受到的是普通字符，则把字符直接显示在屏幕上
                         *
                         * 如果是控制字符（如回车字符），则对光标进行设置
                         * 当处理完一个转义或控制序列，程序也会回到此状态
                         * 
                         */
                case 0: 
                        if (c>31 && c<127) { // 普通可显示的字符
                                if (x>=video_num_columns) { // 光标处于本行最后一列：需要换行
                                        x -= video_num_columns; // 当前光标的列数回到头列
                                        pos -= video_size_row; // 当前光标对应的内存位置减去一行字符所占用的字节
                                        lf(); // 光标向下移动一行
                                }
                                __asm__("movb attr,%%ah\n\t"
                                        "movw %%ax,%1\n\t"
                                        ::"a" (c),"m" (*(short *)pos)
                                        ); // 把获取的字符写入当前光标位置
                                pos += 2; // 当前光标的内存位置增加2字节
                                x++; // 列数增1：也就是光标向右移一列
                        } else if (c==27) // c是转义字符'Esc'
                                state=1; // 转换状态state到1（处理转义序列）
                        else if (c==10 || c==11 || c==12) // c是换行符LF(10) 或 垂直制表符VT(11) 或 换页符FF(12)
                                lf(); // 光标移动到下一行
                        else if (c==13) // c是回车符CR(13)
                                cr(); // 光标回到左边第一列
                        else if (c==ERASE_CHAR(tty)) // c是擦除字符DEL(127)
                                del(); // 光标左边的字符擦除（用空格替换），并将光标移动到被擦除的位置
                        else if (c==8) { // c是BS(Backspace 8), 光标左移一列
                                if (x) { // 当前光标不在首列
                                        x--; // 列数减1：左移一列
                                        pos -= 2; // 当前光标在内存中地址减2字节（一个字符）
                                }
                        } else if (c==9) { // c是水平制表符HT（9）
                                c=8-(x&7); // 计算要移动的列数
                                x += c; // 调整当前光标的列数：把光标移动到8的倍数列上
                                pos += c<<1; // 当前内存地址相应调整
                                if (x>video_num_columns) { // 如果列数超出一行能显示的数量，则跳转到下一行
                                        x -= video_num_columns;
                                        pos -= video_size_row;
                                        lf();
                                }
                                c=9; // 恢复c原来的值为水平制表符HT
                        } else if (c==7) // c是响铃符BEL(7)
                                sysbeep(); // 发出蜂鸣
                        break;
                        
                        /*
                         * 接受到转义序列引导字符'Esc'(0x1b=033=27)
                         * 
                         * 如果在此状态接收到一个字符'['：则说明是控制序列，跳转到state=2去处理
                         * 否则就把接收到字符作为转义序列来处理
                         * 
                         */
                case 1:
                        state=0; // 处理完转义序列后状态恢复为”初始显示“
                        if (c=='[') // 如果字符是'['：说明是控制序列
                                state=2; // 转换状态为”开始控制序列处理“
                        else if (c=='E') // Esc E 
                                gotoxy(0,y+1); // 光标下移一行，回到0列
                        else if (c=='M') // Esc M 
                                ri(); // 光标上移一行
                        else if (c=='D') // Esc D 
                                lf(); // 光标下移一行
                        else if (c=='Z') // Esc Z 
                                respond(tty); // 设备属性查询
                        else if (x=='7') // Esc 7 
                                save_cur(); // 保存光标位置
                        else if (x=='8') // Esc 8 
                                restore_cur(); // 恢复保存的光标位置
                        break;
                        
                        /*
                         * 已经接收到一个控制序列引导符'Esc ['，执行参数数组par[]的清零工作后，转向state=3处理
                         * 
                         */
                case 2:
                        for(npar=0;npar<NPAR;npar++) // 初始化参数数组par[]
                                par[npar]=0; 
                        npar=0;
                        state=3; // 转到状态3处执行
                        // 如果下一个接受到的字符是'?'：说明这是一个终端私有序列，后面会有一个功能字符，然后才转到state=3处取执行
                        if ((ques=(c=='?'))) 
                                break;

                        /*
                         * 开始接收控制序列的参数值
                         *
                         * 如果接收到的一个分号';'：维持本状态，并把接收到的数值放入par[]数组中的下一项
                         * 如果接收到的是数字字符：参数值用十进制数表示，把接受到的数字字符转换成数值放入par[]数组中
                         * 如果接收到即不是一个分号，也不是数字：说明控制序列字符已经全部放入par[]中，转向state=4处理
                         * 
                         */
                case 3:
                        if (c==';' && npar<NPAR-1) { // 接收到的一个分号';'
                                npar++; // 
                                break;
                        } else if (c>='0' && c<='9') { // 接收到的数字字符
                                par[npar]=10*par[npar]+c-'0'; // 转换数字字符为十进制数值：‘1’ -> 1 
                                break;
                        } else state=4; // 即不是一个分号，也不是数字：转向状态4处理

                        /*
                         * 已经接收到一个完整的控制序列：根据本状态接受到的结尾字符对相应控制序列进行处理
                         * 
                         */
                case 4:
                        state=0; // 执行完控制序列，状态恢复为“初始显示”
                        switch(c) {
                                // CSI Pn G - 光标水平移动
                        case 'G': case '`': // 如果c是'G'或'`'：par[]中第一个参数代表移动到的列号
                                if (par[0]) par[0]--; // 列号不为0，光标左移一列
                                gotoxy(par[0],y);
                                break;
                                // CSI Pn A - 光标上移
                        case 'A': // 如果c是'A'：par[]中第一个参数代表光标上移的行数 
                                if (!par[0]) par[0]++; // 若参数为0，光标上移一行
                                gotoxy(x,y-par[0]); // 光标上移若干行
                                break;
                                // CSI Pn B - 光标下移
                        case 'B': case 'e': // 如果c是'B'或'e'：par[]中第一个参数代表下移的行数
                                if (!par[0]) par[0]++; // 若参数为0，光标下移一行
                                gotoxy(x,y+par[0]); // 光标下移若干行
                                break;
                                // CSI Pn C - 光标右移
                        case 'C': case 'a': // 如果c是'C'或'a'：par[]中第一个参数代表右移的列数
                                if (!par[0]) par[0]++; // 若参数为0，光标右移一列
                                gotoxy(x+par[0],y); // 光标右移若干列
                                break;
                                // CSI Pn D - 光标左移
                        case 'D': // 如果c是'D'：par[]中第一个参数代表左移的列数
                                if (!par[0]) par[0]++; // 若参数为0，光标左移一列
                                gotoxy(x-par[0],y); // 光标左移若干列
                                break;
                                // CSI Pn E - 光标下移回0列
                        case 'E': // 如果c是'D'：par[]中第一个参数代表下移的行数
                                if (!par[0]) par[0]++; // 若参数为0，光标下移一行
                                gotoxy(0,y+par[0]); // 光标下移若干行，并回到0列
                                break;
                                // CSI Pn F - 光标上移回0列
                        case 'F': // 如果c是'D'：par[]中第一个参数代表下移的行数
                                if (!par[0]) par[0]++; // 若参数为0，光标上移一行
                                gotoxy(0,y-par[0]); // 光标上移若干行，并回到0列
                                break;
                                // CSI Pn d - 在当前列置行位置
                        case 'd': // 如果c是'd': par[]中第一个参数代表行号
                                if (par[0]) par[0]--; // 行号从0开始计数，所以需要减1
                                gotoxy(x,par[0]); // 移动到指定行
                                break;
                                // CSI Pn H - 光标定位
                        case 'H': case 'f': // 如果c是'H'或'f'：第一个参数代表行号，第二个参数代表列号
                                if (par[0]) par[0]--; 
                                if (par[1]) par[1]--;
                                gotoxy(par[1],par[0]); // 移动到指定的位置
                                break;
                                // CSI Pn J - 屏幕擦除字符
                        case 'J': // 如果c是'J'：第一个参数代表光标所处位置清屏的方式
                                csi_J(par[0]);
                                break;
                                // CSI Pn K - 行擦除字符
                        case 'K': // 如果c是'K'：第一个参数代表光标所处位置行内擦除字符的方式
                                csi_K(par[0]);
                                break;
                                // CSI Pn L - 插入行
                        case 'L': // 如果c是'L'：第一个参数代表插入的行数
                                csi_L(par[0]);
                                break;
                                // CSI Pn M - 删除行
                        case 'M': // 如果c是'M'：第一个参数代表删除的行数
                                csi_M(par[0]);
                                break;
                                // CSI Pn P - 删除字符
                        case 'P': // 如果c是'M'：第一个参数代表删除的字符数
                                csi_P(par[0]);
                                break;
                                // CSI Pn @ - 插入字符
                        case '@': // 如果c是'@'：第一个参数代表插入的空格字符数
                                csi_at(par[0]);
                                break;
                                // CSI Pn m - 设置显示字符属性
                        case 'm': // 如果c是'm'：第一个参数代表要设置的显示字符的属性值
                                csi_m();
                                break;
                                // CSI Pn r - 设置滚屏上下界
                        case 'r': // 如果c是'r'或'f'：第一个参数代表滚屏的起始行号，第二个参数代表滚屏的终止行号
                                if (par[0]) par[0]--; // 如果起始行号不等于0, 减1
                                if (!par[1]) par[1] = video_num_lines; // 如果终止行号为0, 默认为可显示行号的最大值
                                // 设置滚屏的起始和终止行号
                                if (par[0] < par[1] &&
                                    par[1] <= video_num_lines) {
                                        top=par[0];
                                        bottom=par[1];
                                }
                                break;
                                // CSI Pn s - 保存当前光标位置
                        case 's': 
                                save_cur();
                                break;
                                // CSI Pn u - 恢复保存的光标位置
                        case 'u':
                                restore_cur();
                                break;
                        }
                }
        }
        set_cursor(); // 根据上面设置的光标位置，设置显示控制器中当前光标的位置
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

/**
 * 控制台终端初始化函数
 *
 * 读取setup.s保存的信息，用以确认显示器类型名，并且设置相关的参数。接着初始化键盘控制器中断，其他什么都不做
 * 如果想让屏幕干净的话，就使用适当的转义字符序列来调用tty_write()函数
 * 
 */
void con_init(void)
{
        register unsigned char a;
        char *display_desc = "????";
        char *display_ptr;

        // 用setup.s中保存的信息来初始化本文件使用的一些静态变量
        video_num_columns = ORIG_VIDEO_COLS; // 显示器显示的字符列数
        video_size_row = video_num_columns * 2; // 每行字符需要的字节数
        video_num_lines = ORIG_VIDEO_LINES; // 显示器显示的行数
        video_page = ORIG_VIDEO_PAGE; // 当前显示页面
        video_erase_char = 0x0720; // 擦除字符（0x20是字符，0x07是属性）

        // 根据显示模型是单色还是彩色，分别设置所使用的显存起始位置，以及显卡索引端口号和显卡数据端口号
        if (ORIG_VIDEO_MODE == 7)			// BIOS显示方式 == 7 ：单色显卡模式
        {
                video_mem_start = 0xb0000; // 设置显存开始地址
                video_port_reg = 0x3b4; // 设置显卡索引端口
                video_port_val = 0x3b5; // 设置显卡数据端口

                /*
                 * 根据BIOS中断int 0x10功能0x12获得的显示模式信息，判断显卡是单色显卡还是彩色显卡
                 *
                 * 若使用上述中断功能所得到的BX寄存器返回的值不等于0x10: 则说明是EGA显卡。因此显示类型为EGAm
                 * 虽然EGA显卡有较多的显存，但是在单色方式下仍然只能利用地址范围是 0xb0000~0xb8000
                 *
                 */
                if ((ORIG_VIDEO_EGA_BX & 0xff) != 0x10) // EGA单色模式
                {
                        video_type = VIDEO_TYPE_EGAM; // EGA单色显卡
                        video_mem_end = 0xb8000; // 显存结束位置
                        display_desc = "EGAm"; // 显卡描述字符串
                }
                else // 单色MDA显卡
                {
                        video_type = VIDEO_TYPE_MDA; // MDA显卡
                        video_mem_end	= 0xb2000; // 显存结束位置
                        display_desc = "*MDA"; // 显卡描述字符串
                }
        }
        else // 彩色显卡
        {
                video_mem_start = 0xb8000; // 显存开始位置
                video_port_reg	= 0x3d4; // 显卡索引端口
                video_port_val	= 0x3d5; // 显卡数据端口
                
                if ((ORIG_VIDEO_EGA_BX & 0xff) != 0x10) // 如果BX不等于0x10，说明是EGA显卡，有16KB显存可用
                {
                        video_type = VIDEO_TYPE_EGAC; // EGA彩色模式
                        video_mem_end = 0xbc000; // 显存结束位置
                        display_desc = "EGAc"; // 显卡描述字符串
                }
                else // CGA显卡，只有8KB显存可用
                {
                        video_type = VIDEO_TYPE_CGA; // CGA彩色模式
                        video_mem_end = 0xba000; // 显存结束位置
                        display_desc = "*CGA"; // 显卡描述字符串
                }
        }

        /* Let the user known what kind of display driver we are using */

        // 在屏幕的右上脚展示”显卡描述字符串“
        display_ptr = ((char *)video_mem_start) + video_size_row - 8; // display_ptr指向第一行倒数第四个字符的位置
        while (*display_desc) // 复制显卡描述字符串到display_ptr指向的显存位置
        {
                *display_ptr++ = *display_desc++;
                display_ptr++;
        }
	
        /* Initialize the variables used for scrolling (mostly EGA/VGA)	*/

        // 初始化用于滚动窗口的一些变量
        origin	= video_mem_start; // 默认滚动窗口开始内存位置
        scr_end	= video_mem_start + video_num_lines * video_size_row; // 滚动窗口末端内存位置
        top	= 0; // 滚动窗口第一行
        bottom	= video_num_lines; // 滚动窗口最后一行

        gotoxy(ORIG_X,ORIG_Y); // 追踪光标到初始位置
        
        set_trap_gate(0x21,&keyboard_interrupt); // 设置键盘中断0x21的陷阱门描述符，处理过程为keyboard_interrupt例程（实现在keyboard.S中）
        outb_p(inb_p(0x21)&0xfd,0x21); // 取消对键盘中断的屏蔽，允许IRQ1
        // 复位键盘键盘控制器
        a=inb_p(0x61); // 读取键盘端口0x61（8255A端口PB）
        outb_p(a|0x80,0x61); // 禁止键盘工作（位7置位）
        outb(a,0x61); // 再次允许键盘工作
}
/* from bsd-net-2: */

/**
 * 停止蜂鸣
 * 
 * 复位8255A FB端口的位1和0
 * 
 */
void sysbeepstop(void)
{
        /* disable counter 2 */
        outb(inb_p(0x61)&0xFC, 0x61); // 禁止定时器2
}

int beepcount = 0; //蜂鸣时间滴答计数


/**
 * 开通蜂鸣
 *
 * 8255A PB端口的位1用作扬声器的开门信号，位0用作8253定时器2的门信号，该定时器的输出脉冲送往扬声器，作为扬声器发声的频率
 * 因此要使扬声器发声需要两步：
 * 1. 开启 8255A PB端口的位1和位0
 * 2. 设置定时器2通道发送一定的定时频率
 * 
 */
static void sysbeep(void)
{
        /* enable counter 2 */
        outb_p(inb_p(0x61)|3, 0x61); // 开启 8255A PB端口的位1和位0
        /* set command for counter 2, 2 byte write */
        outb_p(0xB6, 0x43); // 0xB6: ”设置定时器2“的命令，0x43：定时器2芯片控制寄存器端口
        /* send 0x637 for 750 HZ */
        // 发送0x637（代表750HZ）到定时器2芯片的数据寄存器端口0x42
        outb_p(0x37, 0x42); // 0x37：0x637的低字节，0x42：定时器2芯片的数据寄存器端口
        outb(0x06, 0x42); // 0x06：0x637的高字节，0x42：定时器2芯片的数据寄存器端口
        /* 1/8 second */
        beepcount = HZ/8; // 蜂鸣时间为100/8个滴答，每个滴答是10ms，所以蜂鸣时间为1000/8ms，也就是1/8s
}
