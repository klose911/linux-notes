/*
 *  linux/fs/bitmap.c
 *
 *  (C) 1991  Linus Torvalds
 */

/* bitmap.c contains the code that handles the inode and block bitmaps */
/*
 * bitmap.c 包含处理“inode”和“逻辑块”位图的代码
 */
#include <string.h>

#include <linux/sched.h>
#include <linux/kernel.h>

/**
 * 将指定地址(addr)处的y一块1024字节的内存清零
 *
 * 输入：eax = 0, ecx = 以长字（1个 long 类型为 4B）为单位的数据块长度（BLOCK_SIZE/4）, edi = 指定起始地址 addr 
 */
#define clear_block(addr)                                               \
        __asm__ __volatile__ ("cld\n\t"                                 \
                              "rep\n\t"                                 \
                              "stosl"                                   \
                              ::"a" (0),"c" (BLOCK_SIZE/4),"D" ((long) (addr)))

/**
 * 将指定地址(addr)处“第nr个位偏移处的位”置位（置为1），并返回原位值
 * 
 * 输入: %0: eax （返回值）, %1: eax = 0, %2: nr 位偏移值，%3: addr(内存地址处)
 * 
 */
// register int res : 用于定义一个局部寄存器变量，该变量将被保存在eax中，以便于高效访问和操作
// btsl 指令：测试并设置位，把基地址(%3)和位偏移值(%2)所指定的值先保存到CF标志位上，然后设置该位为1
// setb 指令：把CF标志位设置到 al寄存器中
#define set_bit(nr,addr) ({                                             \
                        register int res ;                              \
                        __asm__ __volatile__("btsl %2,%3\n\tsetb %%al": \
                                             "=a" (res):"0" (0),"r" (nr),"m" (*(addr))); \
                        res;})

/**
 * 将指定地址(addr)处“第nr个位偏移处的位”置位（置为1），并返回原位值的反码
 *
 * 输入: %0: eax （返回值）, %1: eax = 0, %2: nr 位偏移值，%3: addr(内存地址处)
 */
// btrl 指令：测试并复置位，把基地址(%3)和位偏移值(%2)所指定的值先保存到CF标志位上，然后设置该位为0
// setnb: 把CF标志位的反码设置给 al寄存器中
#define clear_bit(nr,addr) ({                                           \
                        register int res ;                              \
                        __asm__ __volatile__("btrl %2,%3\n\tsetnb %%al": \
                                             "=a" (res):"0" (0),"r" (nr),"m" (*(addr))); \
                        res;})

/**
 * 从 addr 开始寻找第一个 0值位
 *
 * 输入：%0: ecx(返回值), %1: ecx(0), %2: esi(addr)
 *
 * 在 addr 起始的内存地址开始的位图中寻找第一个是0的位，并将其距离addr的位偏移值返回
 * 扫描范围是1024字节（8192位）
 * 
 */
// 1. cld: 清方向位
// 2. 标号1: lodsl 取[esi] -> eax
// 3. notl: eax 中每一位取反
// 4. bsfl: 从0位开始扫描eax中是1的第一个位，并把偏移值 -> edx
// 5. j2 2f: 如果 eax 全是0，则向前跳转到标号为2处
// 6. ecx + edx -> ecx （计算结果）
// 7. 向前跳转到标号3处（结束）
// 8. 标号2: ecx + 32 -> ecx 因为每次 loadsl 都会载入32个字节的长字到 eax, 所以这里往 ecx + 32
// 9. cmpl: ecx 和 8192 比较
// 10. 如果 ecx 不等于 8192 ，则跳转到标号1处，继续加载下一个长字到 eax 做扫描
// 11. 标号3： 结束，返回 ecx （如果未找到对应，则ecx此时应该是"8192"）
#define find_first_zero(addr) ({                                        \
                        int __res;                                      \
                        __asm__ __volatile__ ("cld\n"                   \
                                              "1:\tlodsl\n\t"           \
                                              "notl %%eax\n\t"          \
                                              "bsfl %%eax,%%edx\n\t"    \
                                              "je 2f\n\t"               \
                                              "addl %%edx,%%ecx\n\t"    \
                                              "jmp 3f\n"                \
                                              "2:\taddl $32,%%ecx\n\t"  \
                                              "cmpl $8192,%%ecx\n\t"    \
                                              "jl 1b\n"                 \
                                              "3:"                      \
                                              :"=c" (__res):"c" (0),"S" (addr)); \
                        __res;})

/**
 * 释放指定设备dev上数据区中的逻辑块block
 *
 * dev: 设备号
 * block: 逻辑块号
 *
 * 无返回值
 *
 * 注意：这里仅仅修改了设备逻辑块位图在高速缓冲区中的映射，还没有真正对设备进行写请求
 */
void free_block(int dev, int block)
{
        struct super_block * sb;
        struct buffer_head * bh;

        // 首先取设备的超级块信息
        if (!(sb = get_super(dev))) // 无法取到设备的超级块信息，异常退出
                panic("trying to free block on nonexistent device");
        // 校验逻辑块号是否有效
        if (block < sb->s_firstdatazone || block >= sb->s_nzones) // s_firstdatazone: 第一个开始写数据的逻辑块， s_nzones: 逻辑块总数
                panic("trying to free block not in datazone"); // 逻辑块号无效，出错停机
        bh = get_hash_table(dev,block); // 高速缓冲区中根据设备号和逻辑块号查找对应的缓冲块
        if (bh) { //查找到对应的缓冲块
                if (bh->b_count != 1) { // 如果该缓冲块的引用个数不为1,对应设备上的数据块无法被释放
                        printk("trying to free block (%04x:%d), count=%d\n",
                               dev,block,bh->b_count); // 打印出错信息，直接返回
                        return;
                }
                bh->b_dirt=0; // 设置修改标志为0
                bh->b_uptodate=0; // 设置有效标志为0
                brelse(bh); // 释放该缓冲块
        }
        
        // 复位 block 在逻辑块图中的位为0
        block -= sb->s_firstdatazone - 1 ; // 先计算block在数据区开始算起的逻辑块号（从1开始计数）
        // 清理逻辑块号在对应的逻辑块位图中的位
        // block & 8191 ：逻辑块在“逻辑块位图对应的缓冲区地址”中的偏移
        // s_zmap: ”逻辑块位图“对应的缓冲块指针数组
        // s_zmap[block/8192] : 因为每个缓冲块有1024字节，相当于8192位，所以 block/8192 可以计算处对应数组的下标，以此来获得“逻辑块位图”对应的“缓冲块头结构指针”
        // b_data: 缓冲块头结构对应的真实缓冲块内存指针
        if (clear_bit(block&8191,sb->s_zmap[block/8192]->b_data)) { // 返回值是：逻辑块图中对应位原来值的反码，如果是1的话，对应位原本是0，也就是被来就没有被使用
                printk("block (%04x:%d) ",dev,block+sb->s_firstdatazone-1);
                panic("free_block: bit already cleared");
        }
        sb->s_zmap[block/8192]->b_dirt = 1; // 置相应的逻辑块缓冲头结构的修改标志为1（需要同步给设备！！！）
}

/**
 * 向设备申请一块对应的逻辑块
 *
 * dev: 设备号
 *
 * 执行成功返回对应的逻辑块号（当前版本等于盘块号），失败为 0
 * 
 */
int new_block(int dev)
{
        struct buffer_head * bh;
        struct super_block * sb;
        int i,j;

        // 首先取设备的超级块信息
        if (!(sb = get_super(dev))) // 无法取到设备的超级块信息，异常退出
                panic("trying to get new block from nonexistant device");
        j = 8192;

        //扫描文件系统的8块逻辑块位图（对应的缓冲区）
        for (i=0 ; i<8 ; i++) {
                if ((bh=sb->s_zmap[i])) { // bh 为逻辑块位图对应的缓冲区头结构指针
                        if ((j=find_first_zero(bh->b_data))<8192) // 获得对应位图的第一个0位的偏移值
                                break; // 如果找到则中断扫描
                }
        }

        // i >= 8 : 全部扫描了所有8块逻辑块位图
        // !bh: 位图所在的缓冲块指针无效
        // j>= 8192: 没有找到一个0位，见 find_first_zero 宏注释
        if (i>=8 || !bh || j>=8192)
                return 0; // 没有找到空闲逻辑块，出错返回0 
        if (set_bit(j,bh->b_data)) // 设置新逻辑块在位图中对应的位为1, 这里的返回值是原来这位的位值
                panic("new_block: bit already set"); // 对应位的原值是1，那说明已经被使用了，出错停机
        bh->b_dirt = 1; // 逻辑块位图对应缓冲头的修改标志置为1
        j += i*8192 + sb->s_firstdatazone-1; // 计算实际的逻辑块号，刚才的j只是对应位图的偏移值
        if (j >= sb->s_nzones) // 校验逻辑块号是否超出可使用的最大值
                return 0; //出错返回 0 
        if (!(bh=getblk(dev,j))) // 高速缓冲区中申请一块对应的空闲缓冲块
                panic("new_block: cannot get block");
        if (bh->b_count != 1) // 新申请的高速缓冲块其引用计数必须为1
                panic("new block: count is != 1"); //出错，停机
        clear_block(bh->b_data); // 清空刚才申请的空闲缓冲块上（1024字节）的数据
        bh->b_uptodate = 1; // 有效标志置为1
        bh->b_dirt = 1; // 修改标志置为1 
        brelse(bh); //释放刚才申请的空闲缓冲块，以便其他程序使用
        return j; // 返回实际的逻辑块号
}

/**
 * 释放一个内存中的 i节点
 *
 * inode ： i节点指针
 *
 * 无返回值
 * 
 */
void free_inode(struct m_inode * inode)
{
        struct super_block * sb;
        struct buffer_head * bh;
        
        if (!inode) // 首先判断“i节点”指针是否有效
                return;
        if (!inode->i_dev) { // 判断 i节点的设备域是否为空
                memset(inode,0,sizeof(*inode)); // 如果该字段为0，说明i节点没有被使用，用'0'来清空'i节点'所占的内存区
                return;
        }
        
        if (inode->i_count>1) { // 如果该 i节点还有其他进程引用，则不能释放
                printk("trying to free inode with count=%d\n",inode->i_count); // 打印错误信息，内核出错
                panic("free_inode");
        }
        if (inode->i_nlinks) // 还有其他文件目录项再使用该节点，也不能释放
                panic("trying to free inode with links");

        // 获得该 i节点对应设备的超级块信息
        if (!(sb = get_super(inode->i_dev))) // 无法获得对应设备的超级块信息，内核出错，退出
                panic("trying to free inode on nonexistent device");
        if (inode->i_num < 1 || inode->i_num > sb->s_ninodes) // 校验该 i节点号是否有效  
                panic("trying to free inode 0 or nonexistant inode");
        // 每个缓冲块对应 8192个 i节点，因此 i_num>>13 (i_num / 8192) 可以得到对应的 s_imap[] 数组项的小标
        if (!(bh=sb->s_imap[inode->i_num>>13])) // 位图对应的缓冲块头指针为空，内核出错，退出
                panic("nonexistent imap in superblock");

        // 清理 i节点号在对应的i节点位图中的位
        // inode->i_num & 8191 ：i节点在“i节点位图对应的缓冲区地址”中的偏移
        // s_imap: ”i节点位图“对应的缓冲块指针数组
        // s_imap[inode->i_num>>13] : 因为每个缓冲块有1024字节，相当于8192位，所以以此来获得“i节点块位图”对应的“缓冲块头结构指针”
        // b_data: 缓冲块头结构对应的真实缓冲块内存指针
        if (clear_bit(inode->i_num&8191,bh->b_data)) 
                printk("free_inode: bit already cleared.\n\r"); // 如果位图中该位本来就是0,无法清空一个本来就为空的i节点，因此内核报错，退出
        bh->b_dirt = 1; // “i节点位图”对应的缓冲块头结构中的“修改标志”置为“真”
        memset(inode,0,sizeof(*inode)); // 用‘0’来清空 i节点所占的内存！！！
}

/**
 * 为设备dev新建一个i节点
 *
 * dev： 设备号
 *
 * 返回： 成功返回新建并初始化过的 i节点，失败：返回 NULL 
 * 
 */
struct m_inode * new_inode(int dev)
{
        // 注意：这里只分配了 m_inode 指针的内存，这个结构真实的内存，在下面 get_empty_inode() 中才会分配
        struct m_inode * inode; 
        struct super_block * sb;
        struct buffer_head * bh;
        int i,j;

        // 从“内存空闲i节点表”中获取一个“空闲i节点项”
        if (!(inode=get_empty_inode())) // 无法从“内存i节点表”中获取到“空闲i节点项”
                return NULL; // 返回 NULL
        // 从设备中读入超级块信息
        if (!(sb = get_super(dev)))
                panic("new_inode with unknown device");

        j = 8192;
        // 扫描超级块中8块对应的“i节点位图”
        for (i=0 ; i<8 ; i++) {
                if ((bh=sb->s_imap[i])) {
                        // 寻找第一个是‘0’的位
                        if ((j=find_first_zero(bh->b_data))<8192) {
                                break; //找到退出循环
                        }
                }
        }

        // !bh: 位图无效， j>= 8192: i节点位无效，j+i*8192 > sb->s_ninodes：实际的i节点位超出设备最大的i节点位
        if (!bh || j >= 8192 || j+i*8192 > sb->s_ninodes) {
                iput(inode); // 放回先前i节点表中申请的i节点
                return NULL; // 返回 NULL 
        }
        // 设置i节点位图中对应的位为１
        if (set_bit(j,bh->b_data)) // 该位上面的原值如果是１，内核报错，退出
                panic("new_inode: bit already set");
        bh->b_dirt = 1; // ”i节点位图“对应的”缓冲块头结构“的”修改标志“置为真
        inode->i_count=1; // i节点被使用次数 = 1
        inode->i_nlinks=1; // i节点的文件目录项链接数 = １
        inode->i_dev=dev; // i节点的设备号 = dev 
        inode->i_uid=current->euid; // i节点的有效用户ID = 当前进程的有效用户ID 
        inode->i_gid=current->egid; // i节点的有效组ID = 当前进程的有效组ID
        inode->i_dirt=1; // i节点的已修改标志置为”真“
        inode->i_num = j + i*8192; // i节点的节点号　= j + i*8192 
        inode->i_mtime = inode->i_atime = inode->i_ctime = CURRENT_TIME; // i节点的文件修改时间 = i节点自身修改时间 = i节点自身创建时间 = 当前 UNIX 时间（秒数）
        return inode;
}
