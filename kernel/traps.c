/*
 *  linux/kernel/traps.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * 'Traps.c' handles hardware traps and faults after we have saved some
 * state in 'asm.s'. Currently mostly a debugging-aid, will be extended
 * to mainly kill the offending process (probably by giving it a signal,
 * but possibly by killing it outright if necessary).
 */

/*
 * 在 'asm.c' 中保存了一些状态（寄存器压栈）以后，本程序用来处理硬件陷阱和故障。
 * 目前主要用于调试目的，以后主要是用来杀死一些遭受损坏的进程（可能是发送一个信号，但如果有必要也会直接杀死）
 */
#include <string.h> // 字符串头文件，主要定义了一些字符串或内存的汇编嵌入函数

#include <linux/head.h> // 定义了段描述符，以及段选择子常量
#include <linux/sched.h> // 定义了进程结构 task_struct，初始任务为0的数据
#include <linux/kernel.h> //定义了一些常用函数的原型
#include <asm/system.h> // 定义了设置或修改“描述符”/“中断门”等汇编宏
#include <asm/segment.h> // 定义了有关“段寄存器”操作的汇编宏
#include <asm/io.h> // 定义了硬件“端口输入输出”的汇编宏

///以下定义了3个内嵌汇编语句，
// 小括号中的语句可以作为表达式使用，其中 __res 表示输出值
// register char __res 表示定义了一个寄存器变量，如果想指定寄存器，可以写作 "register char __res(ax);" 。


/**
 * 取段 seg 地址 addr 处的一个字节
 *
 * seg: 段选择符
 * addr: 段内指定地址
 *
 * 输出：%0 - eax(__res)
 * 输入：%1 - eax(seg), %2 - 内存地址 (*(addr))  
 */
#define get_seg_byte(seg,addr) ({                                       \
                        register char __res;                            \
                        __asm__("push %%fs;mov %%ax,%%fs;movb %%fs:%2,%%al;pop %%fs" \
                                :"=a" (__res):"0" (seg),"m" (*(addr))); \
                        __res;})

/**
 * 取段 seg 地址 addr 处的一个长字（4个字节）
 *
 * seg: 段选择符
 * addr: 段内指定地址
 *
 * 输出：%0 - eax(__res)
 * 输入：%1 - eax(seg), %2 - 内存地址 (*(addr))  
 */
#define get_seg_long(seg,addr) ({                                       \
                        register unsigned long __res;                   \
                        __asm__("push %%fs;mov %%ax,%%fs;movl %%fs:%2,%%eax;pop %%fs" \
                                :"=a" (__res):"0" (seg),"m" (*(addr))); \
                        __res;})

/**
 * 取 fs 段寄存器的值（选择符）
 *
 * 输出：%0 - eax(__res)
 */
#define _fs() ({                                                \
                        register unsigned short __res;          \
                        __asm__("mov %%fs,%%ax":"=a" (__res):); \
                        __res;})

int do_exit(long code); // 

void page_exception(void); //页异常，实际是 page_default 

void divide_error(void); // int 0 
void debug(void); // int 1 
void nmi(void); // int 2 
void int3(void); // int3 
void overflow(void); // int 4
void bounds(void); // int 5 
void invalid_op(void); // int 6 
void device_not_available(void); // int 7 
void double_fault(void); // int 8 
void coprocessor_segment_overrun(void); // int 9
void invalid_TSS(void); // int 10
void segment_not_present(void); // int 11 
void stack_segment(void); // int 12 
void general_protection(void); // int 13 
void page_fault(void); // int 14 
void coprocessor_error(void); // int 16 
void reserved(void); // int 15 
void parallel_interrupt(void); // int 39
void irq13(void); // int 45 协处理器中断处理 

/**
 * 打印出错中断的名称，出错号，调用程序的 EIP, EFLAGS, ESP, fs 段寄存器，
 * 段的基地址，段的长度，进程号 pid，任务号，10字节指令码。如果堆在用户栈，则
 * 还打印16字节的堆栈内容。这些信息可用于程序调试
 *
 * str：出错信息
 * esp_ptr: 堆栈指针
 * nr: 中断号
 */
static void die(char * str,long esp_ptr,long nr)
{
        long * esp = (long *) esp_ptr;
        int i;

        printk("%s: %04x\n\r",str,nr&0xffff);
        // 下面打印语句显示当前调用进程的 CS:EIP，EFLAGS 和 SS:ESP
        // EIP:\t%04x:%p\n -- esp[1] 是段选择符 cs , esp[0] 是 eip
        // EFLAGS:\t%p\n -- esp[2] 是 原eflags
        // ESP:\t%04x:%p\n -- esp[4] 是 原ss , esp[3] 是 原esp 
        printk("EIP:\t%04x:%p\nEFLAGS:\t%p\nESP:\t%04x:%p\n",
               esp[1],esp[0],esp[2],esp[4],esp[3]);
        printk("fs: %04x\n",_fs()); // 打印fs 段寄存器
        // 打印 段基址，段长度
        printk("base: %p, limit: %p\n",get_base(current->ldt[1]),get_limit(0x17));
        if (esp[4] == 0x17) { // 如果原 ss 值为 0x17 用户栈
                printk("Stack: "); // 打印出用户栈的4个长字值（16字节）
                for (i=0;i<4;i++)
                        printk("%p ",get_seg_long(0x17,i+(long *)esp[3]));
                printk("\n");
        }
        str(i); // 获得当前运行进程的任务号 
        printk("Pid: %d, process nr: %d\n\r",current->pid,0xffff & i); // 打印当前进程的进程号，任务号
        for(i=0;i<10;i++) // 打印10个指令字节码
                printk("%02x ",0xff & get_seg_byte(esp[1],(i+(char *)esp[0])));
        printk("\n\r");
        do_exit(11);		/* play segment exception */ //返回错误11，结束进程
}

// 以下这些 do_ 开头的函数是 asm.s 中对应中断处理程序调用的 C 函数
void do_double_fault(long esp, long error_code)
{
        die("double fault",esp,error_code);
}

void do_general_protection(long esp, long error_code)
{
        die("general protection",esp,error_code);
}

void do_divide_error(long esp, long error_code)
{
        die("divide error",esp,error_code);
}

// 参数是进入中断后被依次压入栈的寄存器的值，参见 asm.s 中的 
void do_int3(long * esp, long error_code,
             long fs,long es,long ds,
             long ebp,long esi,long edi,
             long edx,long ecx,long ebx,long eax)
{
        int tr;

        __asm__("str %%ax":"=a" (tr):"0" (0)); // 取任务寄存器值 -> tr 变量
        // 依次打印被压栈的寄存器的值
        printk("eax\t\tebx\t\tecx\t\tedx\n\r%8x\t%8x\t%8x\t%8x\n\r",
               eax,ebx,ecx,edx);
        printk("esi\t\tedi\t\tebp\t\tesp\n\r%8x\t%8x\t%8x\t%8x\n\r",
               esi,edi,ebp,(long) esp);
        printk("\n\rds\tes\tfs\ttr\n\r%4x\t%4x\t%4x\t%4x\n\r",
               ds,es,fs,tr);
        printk("EIP: %8x   CS: %4x  EFLAGS: %8x\n\r",esp[0],esp[1],esp[2]);
}

void do_nmi(long esp, long error_code)
{
        die("nmi",esp,error_code);
}

void do_debug(long esp, long error_code)
{
        die("debug",esp,error_code);
}

void do_overflow(long esp, long error_code)
{
        die("overflow",esp,error_code);
}

void do_bounds(long esp, long error_code)
{
        die("bounds",esp,error_code);
}

void do_invalid_op(long esp, long error_code)
{
        die("invalid operand",esp,error_code);
}

void do_device_not_available(long esp, long error_code)
{
        die("device not available",esp,error_code);
}

void do_coprocessor_segment_overrun(long esp, long error_code)
{
        die("coprocessor segment overrun",esp,error_code);
}

void do_invalid_TSS(long esp,long error_code)
{
        die("invalid TSS",esp,error_code);
}

void do_segment_not_present(long esp,long error_code)
{
        die("segment not present",esp,error_code);
}

void do_stack_segment(long esp,long error_code)
{
        die("stack segment",esp,error_code);
}

void do_coprocessor_error(long esp, long error_code)
{
        if (last_task_used_math != current)
                return;
        die("coprocessor error",esp,error_code);
}

void do_reserved(long esp, long error_code)
{
        die("reserved (15,17-47) error",esp,error_code);
}

/**
 *  下面是异常（陷阱）中断程序初始化函数。设置他们的中断调用门（中断向量选择符）
 *  set_trap_gate 与 set_system_gate 都使用了”中断描述符表“ IDT 中的”陷阱门“
 *  主要的区别在于前者设置的特权级是0，后者是3，因此断点陷阱，溢出中断，以及边界出错中断可以被任何程序调用
 *  这两个均是汇编宏，定义在 <asm/system.h> 中
 */
void trap_init(void)
{
        int i;

        set_trap_gate(0,&divide_error); // 设置除操作出错的中断向量值，以下类似
        set_trap_gate(1,&debug);
        set_trap_gate(2,&nmi);
        set_system_gate(3,&int3);	/* int3-5 can be called from all */ // int 3 ~ int 5 可以被任何程序调用
        set_system_gate(4,&overflow);
        set_system_gate(5,&bounds);
        set_trap_gate(6,&invalid_op);
        set_trap_gate(7,&device_not_available);
        set_trap_gate(8,&double_fault);
        set_trap_gate(9,&coprocessor_segment_overrun);
        set_trap_gate(10,&invalid_TSS);
        set_trap_gate(11,&segment_not_present);
        set_trap_gate(12,&stack_segment);
        set_trap_gate(13,&general_protection);
        set_trap_gate(14,&page_fault);
        set_trap_gate(15,&reserved);
        set_trap_gate(16,&coprocessor_error);
        // 把 int 17 ~ int 48 暂时设置成 reservered，以后硬件初始化时候会自己重新设置自己的陷阱门
        for (i=17;i<48;i++)
                set_trap_gate(i,&reserved);
    
        set_trap_gate(45,&irq13);  // 设置协处理器 0x2d (45) 的陷阱门描述符
        outb_p(inb_p(0x21)&0xfb,0x21); // 允许 8259A 主芯片的 IRQ2 中断请求
        outb(inb_p(0xA1)&0xdf,0xA1); // 允许 8259A 从芯片的 IRQ13 中断请求
        set_trap_gate(39,&parallel_interrupt); // 设置”并行口1“的中断0x27陷阱门描述符号
}
