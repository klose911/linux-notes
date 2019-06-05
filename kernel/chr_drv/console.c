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
static inline void gotoxy(unsigned int new_x,unsigned int new_y)
{
        if (new_x > video_num_columns || new_y >= video_num_lines)
                return;
        x=new_x;
        y=new_y;
        pos=origin + y*video_size_row + (x<<1);
}

static inline void set_origin(void)
{
        cli();
        outb_p(12, video_port_reg);
        outb_p(0xff&((origin-video_mem_start)>>9), video_port_val);
        outb_p(13, video_port_reg);
        outb_p(0xff&((origin-video_mem_start)>>1), video_port_val);
        sti();
}

static void scrup(void)
{
        if (video_type == VIDEO_TYPE_EGAC || video_type == VIDEO_TYPE_EGAM)
        {
                if (!top && bottom == video_num_lines) {
                        origin += video_size_row;
                        pos += video_size_row;
                        scr_end += video_size_row;
                        if (scr_end > video_mem_end) {
                                __asm__("cld\n\t"
                                        "rep\n\t"
                                        "movsl\n\t"
                                        "movl video_num_columns,%1\n\t"
                                        "rep\n\t"
                                        "stosw"
                                        ::"a" (video_erase_char),
                                         "c" ((video_num_lines-1)*video_num_columns>>1),
                                         "D" (video_mem_start),
                                         "S" (origin)
                                        );
                                scr_end -= origin-video_mem_start;
                                pos -= origin-video_mem_start;
                                origin = video_mem_start;
                        } else {
                                __asm__("cld\n\t"
                                        "rep\n\t"
                                        "stosw"
                                        ::"a" (video_erase_char),
                                         "c" (video_num_columns),
                                         "D" (scr_end-video_size_row)
                                        );
                        }
                        set_origin();
                } else {
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
        else		/* Not EGA/VGA */
        {
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

static void scrdown(void)
{
        if (video_type == VIDEO_TYPE_EGAC || video_type == VIDEO_TYPE_EGAM)
        {
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
        else		/* Not EGA/VGA */
        {
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

static void lf(void)
{
        if (y+1<bottom) {
                y++;
                pos += video_size_row;
                return;
        }
        scrup();
}

static void ri(void)
{
        if (y>top) {
                y--;
                pos -= video_size_row;
                return;
        }
        scrdown();
}

static void cr(void)
{
        pos -= x<<1;
        x=0;
}

static void del(void)
{
        if (x) {
                pos -= 2;
                x--;
                *(unsigned short *)pos = video_erase_char;
        }
}

static void csi_J(int par)
{
        long count;
        long start;

        switch (par) {
		case 0:	/* erase from cursor to end of display */
                count = (scr_end-pos)>>1;
                start = pos;
                break;
		case 1:	/* erase from start to cursor */
                count = (pos-origin)>>1;
                start = origin;
                break;
		case 2: /* erase whole display */
                count = video_num_columns * video_num_lines;
                start = origin;
                break;
		default:
                return;
        }
        __asm__("cld\n\t"
                "rep\n\t"
                "stosw\n\t"
                ::"c" (count),
                 "D" (start),"a" (video_erase_char)
                );
}

static void csi_K(int par)
{
        long count;
        long start;

        switch (par) {
		case 0:	/* erase from cursor to end of line */
                if (x>=video_num_columns)
                        return;
                count = video_num_columns-x;
                start = pos;
                break;
		case 1:	/* erase from start of line to cursor */
                start = pos - (x<<1);
                count = (x<video_num_columns)?x:video_num_columns;
                break;
		case 2: /* erase whole line */
                start = pos - (x<<1);
                count = video_num_columns;
                break;
		default:
                return;
        }
        __asm__("cld\n\t"
                "rep\n\t"
                "stosw\n\t"
                ::"c" (count),
                 "D" (start),"a" (video_erase_char)
                );
}

void csi_m(void)
{
        int i;

        for (i=0;i<=npar;i++)
                switch (par[i]) {
                case 0:attr=0x07;break;
                case 1:attr=0x0f;break;
                case 4:attr=0x0f;break;
                case 7:attr=0x70;break;
                case 27:attr=0x07;break;
                }
}

static inline void set_cursor(void)
{
        cli();
        outb_p(14, video_port_reg);
        outb_p(0xff&((pos-video_mem_start)>>9), video_port_val);
        outb_p(15, video_port_reg);
        outb_p(0xff&((pos-video_mem_start)>>1), video_port_val);
        sti();
}

static void respond(struct tty_struct * tty)
{
        char * p = RESPONSE;

        cli();
        while (*p) {
                PUTCH(*p,tty->read_q);
                p++;
        }
        sti();
        copy_to_cooked(tty);
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
