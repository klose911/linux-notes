/*
 *  linux/fs/exec.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * #!-checking implemented by tytso.
 */

/*
 * Demand-loading implemented 01.12.91 - no need to read anything but
 * the header into memory. The inode of the executable is put into
 * "current->executable", and page faults do the actual loading. Clean.
 *
 * Once more I can proudly say that linux stood up to being changed: it
 * was less than 2 hours work to get demand-loading completely implemented.
 */

#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <a.out.h> // a.out 头文件，定义了a.out执行文件格式和一些宏

#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <asm/segment.h>

extern int sys_exit(int exit_code); // 退出程序系统调用
extern int sys_close(int fd); // 关闭文件系统调用

/*
 * MAX_ARG_PAGES defines the number of pages allocated for arguments
 * and envelope for the new program. 32 should suffice, this gives
 * a maximum env+arg of 128kB !
 */
// 定义了为新程序分配的给参数(arg)和环境变量(env)使用的最大内存页数
// 32页，相当于 128KB
#define MAX_ARG_PAGES 32 

/*
 * create_tables() parses the env- and arg-strings in new user
 * memory and creates the pointer tables from them, and puts their
 * addresses on the "stack", returning the new stack pointer value.
 */

/*
 * 'create_tables' 函数在新进程的内存中解析环境变量和参数字符串，由此创建对应的指针表
 * 并将它们的地址放到“栈”上，然后返回栈顶的指针值
 * 
 */

/*
 * 在新进程的栈中创建环境变量和参数的指针表
 *
 * p: 参数/环境信息偏移指针
 * argc: 参数个数
 * envc: 环境变量个数
 *
 * 返回：栈顶指针值
 *
 * 注意：这里的p是已经调整过的，相对数据段的偏移，而不再是参数/环境变量空间中的偏移值！！！
 * 
 */
static unsigned long * create_tables(char * p,int argc,int envc)
{
        unsigned long *argv,*envp;
        unsigned long * sp;

        // 栈指针是以4字节（1节）为边界进行对齐的，因此这里需要让sp成为4的整数倍值
        sp = (unsigned long *) (0xfffffffc & (unsigned long) p); // 最后2位设为0
        sp -= envc+1; // sp向下移动，空出 envc + 1个环境变量指针(long *)占用的空间，因为环境变量表最后必须以NULL作为结束
        envp = sp; // 环境变量表指针指向sp当前位置
        sp -= argc+1; // sp向下移动，空出 argc + 1个参数指针(long *)占用的空间，同样参数表最后必须以NULL作为结束
        argv = sp; // 参数表指针指向sp当前位置
        put_fs_long((unsigned long)envp,--sp); // 环境变量表指针入栈
        put_fs_long((unsigned long)argv,--sp); // 参数表指针入栈
        put_fs_long((unsigned long)argc,--sp); // 参数个数入栈
        // 遍历参数字符串表
        while (argc-->0) {
                put_fs_long((unsigned long) p,argv++); // 把“当前参数字符串”的“地址”放入“参数指针表”
                while (get_fs_byte(p++)); // 移动到下一个参数字符串
        }
        put_fs_long(0,argv); // 在参数指针表的最后压入一个NULL指针作为结束
        // 遍历环境变量字符串表
        while (envc-->0) {
                put_fs_long((unsigned long) p,envp++); // 把“当前环境变量字符串”的“地址”放入“环境变量指针表”
                while (get_fs_byte(p++)); // 移动到下一个环境变量字符串
        }
        put_fs_long(0,envp); // 在环境变量指针表的最后压入一个NULL指针作为结束
        return sp; // 返回栈顶指针值
}

/*
 * count() counts the number of arguments/envelopes
 */

/*
 * 计算命令行参数/环境变量的个数
 *
 * argv: 参数指针数组，最后一项是NULL
 *
 * 返回：参数个数
 *
 * 注意：这里argv的指针是相对于数据段！！！
 * 
 */
static int count(char ** argv)
{
        int i=0;
        char ** tmp;

        if ((tmp = argv)) // 如果argv是NULL 直接返回0
                // 遍历指针数组，直到指针值为NULL 
                while (get_fs_long((unsigned long *) (tmp++))) 
                        i++; //递增参数个数

        return i; 
}

/*
 * 'copy_string()' copies argument/envelope strings from user
 * memory to free pages in kernel mem. These are in a format ready
 * to be put directly into the top of new user memory.
 *
 * Modified by TYT, 11/24/91 to add the from_kmem argument, which specifies
 * whether the string and the string array are from user or kernel segments:
 * 
 * from_kmem     argv *        argv **
 *    0          user space    user space
 *    1          kernel space  user space
 *    2          kernel space  kernel space
 * 
 * We do this by playing games with the fs segment register.  Since it
 * it is expensive to load a segment register, we try to avoid calling
 * set_fs() unless we absolutely have to.
 */


/*
 * 'copy_string()'函数从用户空间复制 “参数/环境”字符串到内核空间的空闲页中
 *
 * TYT 于 1991.11.24日修改，增加了'from_kmem'参数，指明了字符串或字符串数组来自用户段或内核段：
 *
 * from_kmem      指针 argv*      字符串 argv**
 *   0            用户空间         用户空间
 *   1            内核空间         用户空间
 *   2            内核空间         内核空间
 *
 * 通过巧妙处理fs段寄存器来操作的。由于加载一个寄存器的代价太高，因此尽量避免每次调用 set_fs(), 除非实在必要！！！
 * 
 */

/*
 * 复制指定个数的参数字符串到参数和环境空间中
 *
 * argc: 需要添加的参数个数
 * argv: 参数字符串指针数组
 * page: 参数/环境页面指针数组
 * p: 参数表空间中偏移指针，初始化为指向参数表128KB空间中的最后一个长字处(128KB - 4B) 
 * from_kmem: 支持执行脚本功能的新参数，当没有运行脚本时候，所有的参数字符串都在用户空间中
 *
 * 成功:返回”参数和环境空间“的当前头指针，失败：返回 0
 *
 * 注意：参数字符串是以堆栈方式逆向往其中复制的，因此p指针会随着复制信息的增加而逐渐减小，并始终指向参数字符串的头部！！！
 * 
 */
static unsigned long copy_strings(int argc,char ** argv,unsigned long *page,
                                  unsigned long p, int from_kmem)
{
        char *tmp, *pag=NULL;
        int len, offset = 0;
        unsigned long old_fs, new_fs;

        // 校验偏移指针的初始值
        if (!p) // 如果为NULL，直接报错返回
                return 0;	/* bullet-proofing */
        new_fs = get_ds(); // 取当前寄存器ds，指向内核数据段
        old_fs = get_fs(); // 取当前寄存器fs，指向用户数据段
        if (from_kmem==2) // 如果字符串和字符串数组皆来自内核
                set_fs(new_fs); // fs寄存器指向内核
        // 遍历所需要拷贝的所有参数，从最后一个字符串开始逆向复制，结束条件 argc == 0 
        while (argc-- > 0) {
                if (from_kmem == 1) // 如果字符串指针来自内核，则fs寄存器指向内核
                        set_fs(new_fs);
                // 取的需要复制的字符串的指针 ((unsigned long *)argv) + argc
                // 注意：这个内存地址需要使用fs寄存器作为段寄存器！！！
                if (!(tmp = (char *)get_fs_long(((unsigned long *)argv)+argc))) // 要复制的字符串的指针为 NULL 
                        panic("argc is wrong"); // 内核报错，死机
                if (from_kmem == 1) // 参数字符串在用户空间，所以恢复fs寄存器
                        set_fs(old_fs); // fs寄存器重新指向用户空间

                // 计算当前参数/环境字符串所需要拷贝的字节数
                len=0;		
                do {
                        len++;
                } while (get_fs_byte(tmp++));
                // 如果当前需要拷贝的字符串长度(len) > 此时参数/字符串空间剩余的长度(p)
                if (p-len < 0) {	/* this shouldn't happen - 128kB */ // 当然这种情况不太可能发生
                        set_fs(old_fs); // fs寄存器重新指向用户空间
                        return 0; // 空间不够，返回0：失败
                }
                // 一个字节一个字节地复制“参数/环境”字符串
                while (len) {
                        --p; --tmp; --len;
                        // 首先判断参数和环境空间中相应位置处是否已经有内存页面，如果还没有则为其申请一页内存
                        // offset：在一个页面中当前指针p的偏移值
                        // 第一次执行到这里的时候 offset 肯定为0，因此必定会触发下面逻辑
                        if (--offset < 0) {
                                offset = p % PAGE_SIZE; // 重新设置为p指针在当前页面范围内的偏移值
                                if (from_kmem==2) // 如果字符串在内核空间中
                                        set_fs(old_fs); // fs重新指向用户空间
                                // 如果p指向的页面在内存不存在：page[p/PAGE_SIZE] == 0 
                                // 则重新申请一页新的页面 (unsigned long *) get_free_page()，并把该地址放入“参数/空间页面指针”数组中
                                // 如果无法申请到新的内存页面，则 pag = 0 
                                // 注意：get_free_page 返回的是内存的物理地址！！！
                                if (!(pag = (char *) page[p/PAGE_SIZE]) &&
                                    !(pag = (char *) (page[p/PAGE_SIZE] =
                                                      (unsigned long *) get_free_page())))  // p指向的页面在内存中不存在，并且无法申请到新的一页
                                        return 0; // 失败：返回0
                                if (from_kmem==2) // 如果字符串在内核空间中
                                        set_fs(new_fs); // fs寄存器指向内核

                        }
                        // 注意：pag使用的是内存的物理地址！！！
                        *(pag + offset) = get_fs_byte(tmp); // 从fs段中复制一个字节到参数/环境空间内存页面的pag+offset处
                }
        }
        if (from_kmem==2) // 如果字符串指针和字符串数组在内核空间中
                set_fs(old_fs); // fs重新指向用户空间
        return p; // 返回“参数/环境“空间中”已复制“的”头部偏移值“
}

/*
 * 修改任务的局部描述符表
 *
 * text_size: 执行文件头中a_text字段对应的代码段长度值
 * page: 参数/环境变量空间中页面指针数组
 *
 * 返回：数据段限长值(64MB)
 * 
 */
static unsigned long change_ldt(unsigned long text_size,unsigned long * page)
{
        unsigned long code_limit,data_limit,code_base,data_base;
        int i;

        // 计算代码段长度: a_text长度以页(4KB)对齐
        code_limit = text_size+PAGE_SIZE -1;
        code_limit &= 0xFFFFF000;
        // 数据段长度设置为64MB 
        data_limit = 0x4000000;
        code_base = get_base(current->ldt[1]); // 获取当前进程的局部描述符表中代码段的段基址
        data_base = code_base; // 代码段和数据段的段地址应该相同
        set_base(current->ldt[1],code_base); // 这里其实不需要再次设置局部描述符表中代码段的基址
        set_limit(current->ldt[1],code_limit); // 设置局部描述符表中代码段的限长
        set_base(current->ldt[2],data_base); // 这里其实也不需要再次设置局部描述符表中数据段的基址
        set_limit(current->ldt[2],data_limit); // 设置局部描述符表中数据段的限长
/* make sure fs points to the NEW data segment */
        // 确信fs寄存器已经指向新的任务数据段 
        __asm__("pushl $0x17\n\tpop %%fs"::); // fs段寄存器放入”局部描述符表“的”任务数据段“对应的选择子'0x17'
        // ”参数/环境变量空间“中的内存页”映射“到”数据段“的末尾
        data_base += data_limit; // database 指向数据段的末尾
        // 从数据段末尾开始一页一页地放入 MAX_ARG_PAGES - 1 页
        for (i=MAX_ARG_PAGES-1 ; i>=0 ; i--) {
                data_base -= PAGE_SIZE; // data_base指向下一页
                if (page[i]) // 如果该页面存在，绝大部分情况实际上不会用满 MAX_ARG_PAGES 个内存页
                        put_page(page[i],data_base); // 把物理地址 page[i] 映射到 线性空间地址 data_base 上  
        }
        return data_limit; // 返回段限长 64MB 
}

/*
 * 'do_execve()' executes a new program.
 */
int do_execve(unsigned long * eip,long tmp,char * filename,
              char ** argv, char ** envp)
{
        struct m_inode * inode;
        struct buffer_head * bh;
        struct exec ex;
        unsigned long page[MAX_ARG_PAGES];
        int i,argc,envc;
        int e_uid, e_gid;
        int retval;
        int sh_bang = 0;
        unsigned long p=PAGE_SIZE*MAX_ARG_PAGES-4;

        if ((0xffff & eip[1]) != 0x000f)
                panic("execve called from supervisor mode");
        for (i=0 ; i<MAX_ARG_PAGES ; i++)	/* clear page-table */
                page[i]=0;
        if (!(inode=namei(filename)))		/* get executables inode */
                return -ENOENT;
        argc = count(argv);
        envc = count(envp);
	
restart_interp:
        if (!S_ISREG(inode->i_mode)) {	/* must be regular file */
                retval = -EACCES;
                goto exec_error2;
        }
        i = inode->i_mode;
        e_uid = (i & S_ISUID) ? inode->i_uid : current->euid;
        e_gid = (i & S_ISGID) ? inode->i_gid : current->egid;
        if (current->euid == inode->i_uid)
                i >>= 6;
        else if (current->egid == inode->i_gid)
                i >>= 3;
        if (!(i & 1) &&
            !((inode->i_mode & 0111) && suser())) {
                retval = -ENOEXEC;
                goto exec_error2;
        }
        if (!(bh = bread(inode->i_dev,inode->i_zone[0]))) {
                retval = -EACCES;
                goto exec_error2;
        }
        ex = *((struct exec *) bh->b_data);	/* read exec-header */
        if ((bh->b_data[0] == '#') && (bh->b_data[1] == '!') && (!sh_bang)) {
                /*
                 * This section does the #! interpretation.
                 * Sorta complicated, but hopefully it will work.  -TYT
                 */

                char buf[1023], *cp, *interp, *i_name, *i_arg;
                unsigned long old_fs;

                strncpy(buf, bh->b_data+2, 1022);
                brelse(bh);
                iput(inode);
                buf[1022] = '\0';
                if ((cp = strchr(buf, '\n'))) {
                        *cp = '\0';
                        for (cp = buf; (*cp == ' ') || (*cp == '\t'); cp++);
                }
                if (!cp || *cp == '\0') {
                        retval = -ENOEXEC; /* No interpreter name found */
                        goto exec_error1;
                }
                interp = i_name = cp;
                i_arg = 0;
                for ( ; *cp && (*cp != ' ') && (*cp != '\t'); cp++) {
                        if (*cp == '/')
                                i_name = cp+1;
                }
                if (*cp) {
                        *cp++ = '\0';
                        i_arg = cp;
                }
                /*
                 * OK, we've parsed out the interpreter name and
                 * (optional) argument.
                 */
                if (sh_bang++ == 0) {
                        p = copy_strings(envc, envp, page, p, 0);
                        p = copy_strings(--argc, argv+1, page, p, 0);
                }
                /*
                 * Splice in (1) the interpreter's name for argv[0]
                 *           (2) (optional) argument to interpreter
                 *           (3) filename of shell script
                 *
                 * This is done in reverse order, because of how the
                 * user environment and arguments are stored.
                 */
                p = copy_strings(1, &filename, page, p, 1);
                argc++;
                if (i_arg) {
                        p = copy_strings(1, &i_arg, page, p, 2);
                        argc++;
                }
                p = copy_strings(1, &i_name, page, p, 2);
                argc++;
                if (!p) {
                        retval = -ENOMEM;
                        goto exec_error1;
                }
                /*
                 * OK, now restart the process with the interpreter's inode.
                 */
                old_fs = get_fs();
                set_fs(get_ds());
                if (!(inode=namei(interp))) { /* get executables inode */
                        set_fs(old_fs);
                        retval = -ENOENT;
                        goto exec_error1;
                }
                set_fs(old_fs);
                goto restart_interp;
        }
        brelse(bh);
        if (N_MAGIC(ex) != ZMAGIC || ex.a_trsize || ex.a_drsize ||
            ex.a_text+ex.a_data+ex.a_bss>0x3000000 ||
            inode->i_size < ex.a_text+ex.a_data+ex.a_syms+N_TXTOFF(ex)) {
                retval = -ENOEXEC;
                goto exec_error2;
        }
        if (N_TXTOFF(ex) != BLOCK_SIZE) {
                printk("%s: N_TXTOFF != BLOCK_SIZE. See a.out.h.", filename);
                retval = -ENOEXEC;
                goto exec_error2;
        }
        if (!sh_bang) {
                p = copy_strings(envc,envp,page,p,0);
                p = copy_strings(argc,argv,page,p,0);
                if (!p) {
                        retval = -ENOMEM;
                        goto exec_error2;
                }
        }
/* OK, This is the point of no return */
        if (current->executable)
                iput(current->executable);
        current->executable = inode;
        for (i=0 ; i<32 ; i++)
                current->sigaction[i].sa_handler = NULL;
        for (i=0 ; i<NR_OPEN ; i++)
                if ((current->close_on_exec>>i)&1)
                        sys_close(i);
        current->close_on_exec = 0;
        free_page_tables(get_base(current->ldt[1]),get_limit(0x0f));
        free_page_tables(get_base(current->ldt[2]),get_limit(0x17));
        if (last_task_used_math == current)
                last_task_used_math = NULL;
        current->used_math = 0;
        p += change_ldt(ex.a_text,page)-MAX_ARG_PAGES*PAGE_SIZE;
        p = (unsigned long) create_tables((char *)p,argc,envc);
        current->brk = ex.a_bss +
                (current->end_data = ex.a_data +
                 (current->end_code = ex.a_text));
        current->start_stack = p & 0xfffff000;
        current->euid = e_uid;
        current->egid = e_gid;
        i = ex.a_text+ex.a_data;
        while (i&0xfff)
                put_fs_byte(0,(char *) (i++));
        eip[0] = ex.a_entry;		/* eip, magic happens :-) */
        eip[3] = p;			/* stack pointer */
        return 0;
exec_error2:
        iput(inode);
exec_error1:
        for (i=0 ; i<MAX_ARG_PAGES ; i++)
                free_page(page[i]);
        return(retval);
}
