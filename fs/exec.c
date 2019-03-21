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

/**
 * 加载并执行子进程
 *
 * eip: 调用系统中断的程序代码指针
 * tmp: 系统中断在调用 _sys_execve()时候的返回地址，无用
 * filename: 被执行的文件名指针
 * argv: 命令行参数指针数组的指针
 * envp: 环境变量指针数组的指针
 *
 * 成功：无返回，失败：设置错误号，返回 -1
 * 
 * 该函数是系统中断调用(int 0x80) 功能号 __NR_execve 调用的函数
 * 函数的参数是“进入系统调用处理过程”后直到“本系统调用处理过程”和调用本函数前逐步压入栈中的值：
 * 1. system_call.s 第109行～第111行入栈的edx, ecx, ebx, 分别对应 **envp, **argv和 *filename
 * 2. system_call.s 第121行，调用sys_call_table中sys_execve函数指针时压入栈的返回地址tmp（无用）
 * 3. system_call.s 第258行，调用本函数前入栈的指向栈中调用系统中断的程序代码指针eip
 * 
 */
int do_execve(unsigned long * eip,long tmp,char * filename,
              char ** argv, char ** envp)
{
        struct m_inode * inode;
        struct buffer_head * bh;
        struct exec ex;
        unsigned long page[MAX_ARG_PAGES]; // “命令行参数/环境变量”空间页面指针数组
        int i,argc,envc;
        int e_uid, e_gid; // 有效用户ID，有效组ID 
        int retval;
        int sh_bang = 0; // 是否需要执行脚本
        unsigned long p=PAGE_SIZE*MAX_ARG_PAGES-4; // p 指向“命令行参数/环境变量”空间页面的最后一个长字 (4K * 128 - 4) 

        // 参数eip[1]是本次系统调用的原用户程序代码段寄存器CS值
        // 其中的代码段选择子必须是 0x000f，否则只可能是内核代码段的选择子 0x0008，但这是绝对不允许的，因为内核代码是常驻内存而不能被替换掉的！！！  
        if ((0xffff & eip[1]) != 0x000f) // eip 是内核代码段选择子
                panic("execve called from supervisor mode"); // 报错，死机
        for (i=0 ; i<MAX_ARG_PAGES ; i++)	// 初始化“命令行参数/环境变量”空间页面指针数组
                page[i]=0;
        // 根据执行文件名获得对应的i节点
        if (!(inode=namei(filename))) // 获取i节点失败		
                return -ENOENT; // 返回错误码 -ENOENT
        argc = count(argv); // 计算命令行参数的个数
        envc = count(envp); // 计算环境变量的个数
	
restart_interp:
        if (!S_ISREG(inode->i_mode)) {	// 可执行文件不是常规文件
                retval = -EACCES; // 设置错误号为 -EACCES 
                goto exec_error2; // 跳转到 exec_error2 作为出错处理
        }
        i = inode->i_mode; // 获得文件的属性
        // 如果“设置-用户-ID”位被置位，有效用户ID被设置为文件的宿主ID，反之为当前进程的有效用户ID 
        e_uid = (i & S_ISUID) ? inode->i_uid : current->euid; // 获取有效用户ID
        // 和有效用户ID逻辑一致
        e_gid = (i & S_ISGID) ? inode->i_gid : current->egid; // 获取有效组ID 
        if (current->euid == inode->i_uid) // 进程的有效用户ID == 文件的宿主ID 
                i >>= 6; // 文件属性右移6位，使得最后3位是文件宿主的权限
        else if (current->egid == inode->i_gid)
                i >>= 3; // 文件属性右移3位，使得最后3位是文件组的权限
        // 如果不满足上面2种情况，则最后三位本来就是该文件对应“其他用户”的权限
        if (!(i & 1) && // 文件对应的可执行权限位没有设置
            !((inode->i_mode & 0111) && suser())) { // “文件对应的其他用户没有任何权限”或者“当前进程的用户不是root用户”
                retval = -ENOEXEC; // 设置错误号为 -ENOEXEC 
                goto exec_error2; // 跳转到 exec_error2 作为出错处理 
        }
        // 有执行文件的权限，从执行文件读出头部数据
        // 读取可执行文件的第一个数据块（执行头）到高速缓冲区
        if (!(bh = bread(inode->i_dev,inode->i_zone[0]))) { // 读取执行文件第一个数据块到高速缓冲区失败
                retval = -EACCES; // 设置错误号为 -EACCES
                goto exec_error2; // 跳转到 exec_error2 作为出错处理
        }
        ex = *((struct exec *) bh->b_data);	// 读取可执行文件的执行头
        // 可执行文件的以 '#!' 开头：说明这是一个脚本文件，并且 sh_bang 不等于 0 
        // 注意：处理完脚本文件，会重新回到上面的 restart_interp标号执行，再次执行到这里的时候 sh_bang 已经被设置为1
        if ((bh->b_data[0] == '#') && (bh->b_data[1] == '!') && (!sh_bang)) {
                /*
                 * This section does the #! interpretation.
                 * Sorta complicated, but hopefully it will work.  -TYT
                 */

                // 下面的代码很不好理解，我需要调试工具可能才能完全理解 :-(
                char buf[1023], *cp, *interp, *i_name, *i_arg;
                unsigned long old_fs;

                // 从脚本文件提取解释器程序名及其参数，脚本文件名等组合放入环境参数表中
                // 首先提取脚本文件开始的'#!'之后的字符串到buf中，其中含有解释器执行文件名，可能还含有额外的参数等
                strncpy(buf, bh->b_data+2, 1022); 
                brelse(bh); // 释放高速缓存块
                iput(inode); // 释放i节点
                buf[1022] = '\0'; 
                if ((cp = strchr(buf, '\n'))) { // cp == buf中第一个 '\n'的下标
                        *cp = '\0'; // 第一个换行符用'\0'(NULL字符)代替：相当于 buf 变成了脚本第一行的内容（C语言的字符串以'\0'作为结尾）
                        // 1. cp 指向 buf
                        // 2. 去掉开头的空格和制表符（'#!'后面可能跟着空格）
                        for (cp = buf; (*cp == ' ') || (*cp == '\t'); cp++);
                }
                if (!cp || *cp == '\0') { // 脚本文件第一行没有内容
                        retval = -ENOEXEC; // 设置错误号为 -ENOEXEC 
                        goto exec_error1; // 执行 exec_error1 出错处理
                }
                interp = i_name = cp; // 得到解释器的名字
                i_arg = 0;
                // 继续遍历脚本文件，找到其中的第一个字符串，这应该是解释器程序名
                for ( ; *cp && (*cp != ' ') && (*cp != '\t'); cp++) {
                        if (*cp == '/') // 这时候只支持 #!/sh 这种，因此一旦找到'/'，就认为后面的'sh' 是解释器程序名
                                i_name = cp+1; // i_name指向解释器程序名 
                }
                if (*cp) {
                        *cp++ = '\0'; // 解释程序名的末尾加NULL字符
                        i_arg = cp; // i_arg指向解释器参数
                }
                /*
                 * OK, we've parsed out the interpreter name and
                 * (optional) argument.
                 */
                // 现在开始把’解释器程序名‘(i_name)和’解释器参数‘(i_arg)和‘脚本文件名’作为解释程序的参数放入“参数/环境变量”空间中去
                if (sh_bang++ == 0) { // 判断 sh_bang == 0， 然后 + 1 
                        p = copy_strings(envc, envp, page, p, 0); // 把本函数参数中的”环境变量“从用户空间放入“参数/环境变量”空间
                        // 注意：这里放入命令行参数比“调用参数中的argc”少一个，少掉的一个是原来的脚本文件名
                        p = copy_strings(--argc, argv+1, page, p, 0); // 把本函数参数中的”命令行参数“从用户空间放入“参数/环境变量”空间
                }
                /*
                 * Splice in (1) the interpreter's name for argv[0]
                 *           (2) (optional) argument to interpreter
                 *           (3) filename of shell script
                 *
                 * This is done in reverse order, because of how the
                 * user environment and arguments are stored.
                 */

                /*
                 * 拼接 (1) argv[0] 放入解释器的名字
                 *     (2) （可选）解释器参数
                 *     (3) 脚本程序的名称
                 *
                 * 由于环境变量和参数存放的顺序，导致这里是逆序处理的
                 * 
                 */
                // 注意：这里最后一个参数被设置为1，因为脚本文件名的指针来自于内核空间
                p = copy_strings(1, &filename, page, p, 1); // 逆向复制脚本文件名
                argc++; // argc 自增 1 
                if (i_arg) { // 脚本文件中存在参数
                        // 注意：这里是从内核空间中拷贝参数字符串（脚本文件的字符串已经被读入内核空间），所以这里的最后参数设置为2！！！
                        p = copy_strings(1, &i_arg, page, p, 2); // 拷贝脚本文件中的参数到“参数/环境变量”空间
                        argc++; // 增加 argc 
                }
                p = copy_strings(1, &i_name, page, p, 2); // 解释器程序名拷贝到“参数/环境变量”空间，同样的“解释器程序名”对应的字符串也已经在内核空间中
                argc++; // 继续增加 argc 
                if (!p) { // 前面的拷贝失败
                        retval = -ENOMEM; // 设置错误号 -ENOMEM 
                        goto exec_error1; // 执行 exec_error1 出错处理
                }
                /*
                 * OK, now restart the process with the interpreter's inode.
                 */
                // 最后取得解释器程序的i节点，并执行重新回到 restart_interp 来执行解释器
                // 注意：为了获得解释器的i节点，需要调用namei()函数，但是该函数所使用的参数（文件名）是根据“用户数据段”寄存器fs为段基址
                //      但是这里的interp的地址是以“内核数据段”寄存器ds为段基址！！！
                old_fs = get_fs(); // 临时保存“用户数据段”的段基址fs寄存器中的内容 
                set_fs(get_ds()); // 设置fs寄存器的内容为ds段寄存器的内容（内核数据段的段基址）
                // 获取解释器对应的i节点，并重新设置给inode变量
                if (!(inode=namei(interp))) { // 获取解释器i节点失败 
                        set_fs(old_fs); // 恢复fs寄存器
                        retval = -ENOENT; // 设置错误号为 -ENOENT 
                        goto exec_error1; // 执行 exec_error1 出错处理
                }
                set_fs(old_fs); // 恢复fs寄存器
                goto restart_interp; // 重新开始执行 restart_interp 标号
        }

        // 此时缓冲块中的可执行文件的执行头已经复制到内存中的ex结构内
        brelse(bh); // 释放高速缓冲块
        // 校验可执行文件的执行头结构
        // 1. 可执行文件格式不是“支持分页的可执行文件”(ZMAGIC)
        // 2. 代码重定位信息长度 （a_trsize域） == 0
        // 3. 数据重定位信息长度（a_drsize域） == 0
        // 4. 代码段长度(a_text) + 数据段长度(a_data) + bss段长度(a_bss) > 50MB (0x3000000)
        // 5. 可执行文件的大小 (i_size) < 代码段长度(a_text) + 数据段长度(a_text) + 符号表长度(a_syms) + 执行头部分(NTXTOFF(ex)) 
        if (N_MAGIC(ex) != ZMAGIC || ex.a_trsize || ex.a_drsize ||
            ex.a_text+ex.a_data+ex.a_bss>0x3000000 ||
            inode->i_size < ex.a_text+ex.a_data+ex.a_syms+N_TXTOFF(ex)) {
                retval = -ENOEXEC; // 设置错误号为 -ENOEXEC
                goto exec_error2; // 跳转到 exec_error2 作为出错处理
        }

        // 执行头必须以1024字节的边界处
        if (N_TXTOFF(ex) != BLOCK_SIZE) { // 执行头长度 != 1个逻辑块长度
                printk("%s: N_TXTOFF != BLOCK_SIZE. See a.out.h.", filename);
                retval = -ENOEXEC; // 设置错误号为 -ENOEXEC
                goto exec_error2; // 跳转到 exec_error2 作为出错处理
        }
        // 接下来处理非脚本文件时候的“命令行参数“/”环境变量“的放置
        if (!sh_bang) { // a.out格式的可执行文件
                p = copy_strings(envc,envp,page,p,0); // 把本函数参数中的 ”环境变量“从用户空间放入“参数/环境变量”空间
                p = copy_strings(argc,argv,page,p,0); // 把本函数参数中的 ”命令行参数“从用户空间放入“参数/环境变量”空间
                if (!p) { // 前面的拷贝失败
                        retval = -ENOMEM; // 设置错误号 -ENOMEM
                        goto exec_error2; // 跳转到 exec_error2 作为出错处理
                }
        }
/* OK, This is the point of no return */ // 现在开始do_execve函数没有返回值了， 开弓没有回头路 :-)
        if (current->executable) // 如果”当前进程“的”可执行文件i节点“已经被设置
                iput(current->executable); // 放回”当前进程“的”可执行文件i节点“
        current->executable = inode; // 设置”当前进程“的”可执行文件i节点“为‘inode’变量
        
        // 置空当前进程的所有信号处理句柄
        // 注意：这里的做得比较粗糙
        // 1. 仅仅处理了信号句柄，而对sa_flags, sa_mask 没有处理
        // 2. 信号句柄是 SIG_IGN 设置为NULL，无须复位
        for (i=0 ; i<32 ; i++) 
                current->sigaction[i].sa_handler = NULL;
        
        // 遍历当前进程打开所有文件的描述符表（这些文件文件描述符继承于父进程）
        for (i=0 ; i<NR_OPEN ; i++)
                if ((current->close_on_exec>>i)&1) // 如果文件描述符在”close_on_exec“位图中所对应的位被置位
                        sys_close(i); // 关闭对应的文件描述符
        current->close_on_exec = 0; // 重置close_on_exec位图

        // 注意：下面的内存释放完毕后，新执行文件并没有占用任何内存页面
        // 因此在处理器真正执行新执行文件代码时会触发”缺页异常中断“：
        // 1. 内存管理程序开始执行缺页处理，为新执行申请内存页面和设置相关页表项
        // 2. 把相关执行文件页面读入内存中
        free_page_tables(get_base(current->ldt[1]),get_limit(0x0f)); // 释放当前进程的代码段所对应的内存表映射的物理内存页面和页表本身
        free_page_tables(get_base(current->ldt[2]),get_limit(0x17)); // 释放当前进程的数据段所对应的内存表映射的物理内存页面和页表本身
        
        if (last_task_used_math == current) // 如果原来进程是最后一个使用数字协处理器的进程
                last_task_used_math = NULL; // 重置最后一个使用数字协处理器的进程指针
        current->used_math = 0; // 当前进程中是否使用“数字协处理器”设置为”假“

        // 1. 修改任务的局部描述符表
        // 2. p 指针从“环境/参数空间”调整为当前进程的“数据段“作为起始的偏移： 
        p += change_ldt(ex.a_text,page)-MAX_ARG_PAGES*PAGE_SIZE; // p += 64MB - 32 * 4KB = 64MB - 128KB  
        p = (unsigned long) create_tables((char *)p,argc,envc); // 在栈中放置”环境变量“和”命令行参数“的指针数组表
        
        // 1. 重新设置当前进程的代码段末尾指针： end_code = a_text
        // 2. 重新设置当前进程的数据段末尾指针： end_data = end_code + a_data 
        // 3. 计算当前进程的堆尾指针： brk = a_bss + end_data = a_bss + a_data + a_txt
        // 堆尾指针一般用于动态分配内存使用(malloc, free ...)
        current->brk = ex.a_bss +
                (current->end_data = ex.a_data +
                 (current->end_code = ex.a_text));
        
        // 虽然此时p指向的应该是当前栈顶了，但还是需要页面对齐：0x1000 = 4KB 
        current->start_stack = p & 0xfffff000; // 设置当前进程的栈开始指针

        // 如果可执行文件的”设置-用户-位”被设置，则可能改变进程的有效用户ID
        current->euid = e_uid; // 重新设置当前进程的有效用户ID 
        current->egid = e_gid; // 和上面类似：重新设置当前进程的有效组ID
        i = ex.a_text+ex.a_data; // 计算
        while (i&0xfff)
                put_fs_byte(0,(char *) (i++));
        // 将“系统中断”中的“处理程序”在堆栈中的“代码指针”(eip[0])替换为“新执行程序的入口点”(a_entry)
        eip[0] = ex.a_entry;		/* eip, magic happens :-) */
        // 将“系统中断”中的“处理程序”在堆栈中的“栈指针”(esp = eip[3])替换为p
        eip[3] = p; // 实际上 current->start_stark 有可能和 p 不一样，不明白为什么要这么设置 start_stack域？?
        // 下面的return指令：会弹出栈中的数据，并使得CPU去执行eip[0]位置的“新执行程序的入口点”
        // 所以实际上不会返回到原来系统中断调用的程序中去了，好比施加了魔法一样 :-)
        return 0;
        
        // 注意：exec_error1 和 exec_error2的区别在于： exec_error2 需要额外的释放可执行文件的i节点！！！
exec_error2:
        iput(inode); // 释放i节点
exec_error1:
        for (i=0 ; i<MAX_ARG_PAGES ; i++) // 释放保存“命令行参数和环境变量”字符串的内存页面
                free_page(page[i]);
        return(retval); // 返回出错码
}
