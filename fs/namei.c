/*
 *  linux/fs/namei.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * Some corrections by tytso.
 */

#include <linux/sched.h> // 系统调度头文件
#include <linux/kernel.h> // 内核常用函数头文件
#include <asm/segment.h> // 段操作头文件

#include <string.h> // 字符串函数头文件
#include <fcntl.h> // 文件控制头文件：文件及描述符相关控制操作的常数定义
#include <errno.h> // 错误头文件
#include <const.h> // 常量头文件
#include <sys/stat.h> // 文件状态头文件

/*
 * 下面宏中右侧表达式是访问数组的一种特殊使用方法
 * 它基于这样一个事实：用数组名和下标所表示的数组项（例如a[b]）等同于使用数组的首指针（地址）加上该项偏移地址的形式的值 *(a + b)，而且数组项a[b]也可以表示为b[a]
 * 因此对于字符串数组形式为"LoveYou"[2]或者2["LoveYou"]等同于 *("LoveYou" + 2)（"LoveYou"在内存中被存储的位置就是其地址），
 * 再进一步说 "LoveYou"[2]对应的字符'v'的ASCII码值是0x76，如果用8进制来表示就是0166
 * 在C语言中字符可以用其ASCII码值来表示：方法是在ASCII码值前前面加上一个反斜杠，例如上面说的字符'v'可以表示成"\x76"或"\166"
 * 对于不可显示的字符（例如ASCII码值中0x00~0x1f的控制字符）就用ASCII码值来表示
 */

/**
 * 文件访问模式宏：根据文件访问标志x来索引双引号中对应的值
 * 
 * x是fcntl.h中文件访问（打开）标志
 * O_ACCMODE: 00003, 是索引值x的屏蔽码
 *
 * 双引号中有4个8进制的数值（实际上表示4个控制字符）："\004\002\006\377"，
 * 分别表示：r（读）, w（写）, rw（读写） 和 wxrwxrwx（读写执行）, 并且分别对应x的索引值：0~3
 *
 * 例如：x为2，则返回8进制值006，表示可读可写
 */
#define ACC_MODE(x) ("\004\002\006\377"[(x)&O_ACCMODE])


/*
 * comment out this line if you want names > NAME_LEN chars to be
 * truncated. Else they will be disallowed.
 */
/* #define NO_TRUNCATE */

#define MAY_EXEC 1 // 可执行（可进入）
#define MAY_WRITE 2 // 可写
#define MAY_READ 4 // 可读

/*
 *	permission()
 *
 * is used to check for read/write/execute permissions on a file.
 * I don't know if we should look at just the euid or both euid and
 * uid, but that should be easily changed.
 */

/*
 * 检查文件访问权限
 *
 * m_inode: 文件的i节点指针
 * mask: 访问属性屏蔽码
 *
 * 许可：返回 1，不许可：返回 0
 * 
 */
static int permission(struct m_inode * inode,int mask)
{
        int mode = inode->i_mode;

/* special case: not even root can read/write a deleted file */
        if (inode->i_dev && !inode->i_nlinks) // 如果该i节点有对应的设备，但是硬链接引用计数为0（该文件已经被删除）
                return 0; // 哪怕是 root 用户，也不允许访问被删除的文件
        else if (current->euid==inode->i_uid) // 当前进程的 euid 等于 文件的宿主用户id
                mode >>= 6; // 取文件宿主的访问权限
        else if (current->egid==inode->i_gid) // 当前进程的 egid 等于 文件的组id
                mode >>= 3; // 取文件的组访问权限
        // 判断获得的权限是否和屏蔽码匹配 或者“当前进程的euid”是root用户
        // 假设 mode 为5（可读，可执行），访问属性的屏蔽码是4（读取）
        // (5 & 4 & 0007) 的结果为4
        // 注意：0007用来屏蔽掉第4位开始的位！！！
        if (((mode & mask & 0007) == mask) || suser())
                return 1; // 允许访问
        return 0;
}

/*
 * ok, we cannot use strncmp, as the name is not in our data space.
 * Thus we'll have to use match. No big problem. Match also makes
 * some sanity tests.
 *
 * NOTE! unlike strncmp, match returns 1 for success, 0 for failure.
 */

/*
 * 指定长度字符串比较函数
 *
 * len: 字符串长度
 * name: 文件名指针
 * de: 目录项结构指针
 *
 * 匹配：返回 1，不匹配：返回 0
 *
 * 注意：返回值和strncmp正好相反
 * 
 */
static int match(int len,const char * name,struct dir_entry * de)
{
        register int same ;

        // 判断参数的有效性
        if (!de || !de->inode || len > NAME_LEN) // 目录项指针为空 或者 目录项对应的i节点为空 或者 比较的字符串长度 > 文件名可允许长度
                return 0; // 直接返回0 
        if (len < NAME_LEN && de->name[len]) // 字符串长度小于 NAME_LEN 但是 de->name[len]是一个非 NULL的普通字符
                return 0; // 不匹配返回0
        // 使用嵌入式汇编进行快速比较操作
        // 在用户空间的 fs 段执行字符串比较操作
        // %0 - eax(比较结果 sum), %1 - eax初始值(0), %2 - esi(指针: name), %3 - edi（目录项的名字指针：de->name）, %4 - ecx（比较的字符串长度：len）
        // 1. 'cld;' : 清方向标志位
        // 'fs; repe; cmpsb;' : 用户空间执行循环比较 [esi++] 和 [edi++]
        // 'setz %al' : 如果比较结果一样，zf=0, 则置 al=1（也就是 same 为1）
        __asm__("cld\n\t"
                "fs ; repe ; cmpsb\n\t"
                "setz %%al"
                :"=a" (same)
                :"0" (0),"S" ((long) name),"D" ((long) de->name),"c" (len)
                );
        return same;
}

/*
 *	find_entry()
 *
 * finds an entry in the specified directory with the wanted name. It
 * returns the cache buffer in which the entry was found, and the entry
 * itself (as a parameter - res_dir). It does NOT read the inode of the
 * entry - you'll have to do that yourself if you want to.
 *
 * This also takes care of the few special cases due to '..'-traversal
 * over a pseudo-root and a mount point.
 */

/*
 * 查找“指定目录和文件名”的目录项
 *
 * *dir: 指定目录i节点的指针
 * name: 文件名
 * namelen: 文件名长度
 * *res_dir: 目录项结构的指针（作为结果返回）
 *
 * 查找成功：返回函数高速缓冲区的指针，并在 *res_dir处返回“目录项结构指针”，失败：返回NULL
 *
 * 该函数在“指定目录的数据”（文件）中搜索“指定文件名的目录项“，并对指定文件名为'..'根据当前进行的相关设置做特殊处理
 * 注意：这个函数并不会读取目录项对应的i节点，如果需要的化必须手动读取!!!
 * 
 */
static struct buffer_head * find_entry(struct m_inode ** dir,
                                       const char * name, int namelen, struct dir_entry ** res_dir)
{
        int entries;
        int block,i;
        struct buffer_head * bh;
        struct dir_entry * de;
        struct super_block * sb;

        // 文件名是否要截短
#ifdef NO_TRUNCATE
        if (namelen > NAME_LEN) // 不需要截短，而且文件名长度 > NAME_LEN，直接返回 NULL 
                return NULL;
#else
        if (namelen > NAME_LEN)
                namelen = NAME_LEN; // 反之文件名长度设置为 NAME_LEN 
#endif
        // 目录i节点的i_size含有本目录包含的数据长度，因此除以一个目录项的长度（16字节），即可得到该目录所包含有的”目录项数目“
        entries = (*dir)->i_size / (sizeof (struct dir_entry)); // 计算本目录中包含的目录项的项数
        *res_dir = NULL; // 设置”返回的目录项结构指针“(*res_dir)为 NULL 
        if (!namelen) // 如果文件名字符串长度为0，直接返回 NULL 
                return NULL;
/* check for '..', as we might have to do some "magic" for it */
        // 接下对文件名为 '..' 做特殊处理
        if (namelen==2 && get_fs_byte(name)=='.' && get_fs_byte(name+1)=='.') { // 
/* '..' in a pseudo-root results in a faked '.' (just change namelen) */
                // 如果”指定目录“等于”当前进程的根目录“，对于本进程来说指定目录就是它的伪根目录
                // 因为本进程无法访问它的工作根目录的父目录，所以这里只是简单的把 '..' 当做 '.'
                if ((*dir) == current->root) 
                        namelen=1;
                // 如果”指定目录的i节点“等于 ROOT_INO(1)的话，说明要指定目录是一个挂载点的父目录！！！ 
                else if ((*dir)->i_num == ROOT_INO) {
/* '..' over a mount-point results in 'dir' being exchanged for the mounted
   directory-inode. NOTE! We set mounted, so that we can iput the new dir */
                        sb=get_super((*dir)->i_dev); // 获得该文件系统的超级块
                        if (sb->s_imount) { // 如果该文件系统已经被挂载
                                iput(*dir); // 放回”指定目录的i节点“
                                (*dir)=sb->s_imount; // ”指定目录的i节点指针“指向”被挂载文件系统对应的目录i节点“上
                                (*dir)->i_count++; // 并且”被挂载文件系统对应的目录i节点“的引用计数加1（因为设置了mounted标志，所以可以进行”偷粱换柱“）
                        }
                }
        }

        // 查找文件名的目录项，为此需要读取目录的数据：取出”目录i节点“对应”块设备数据区“中信息（逻辑块信息）
        // 读取“目录i节点”对应的“数据区”的“第一个逻辑块号”
        if (!(block = (*dir)->i_zone[0])) // 第一个逻辑块号为0，该目录不包含任何数据,直接返回 NULL
                return NULL; 
        // 读取”目录i节点“对应的“数据区”中“第一个逻辑块”信息到高速缓冲区
        if (!(bh = bread((*dir)->i_dev,block))) // 读取逻辑块失败，直接返回 NULL 
                return NULL;

        // 在目录数据区查找对应文件名的目录项结构
        i = 0;
        de = (struct dir_entry *) bh->b_data; // 让 de 指向缓冲区的数据部分开头
        while (i < entries) { // 不超过该目录区所拥有的目录项数
                if ((char *)de >= BLOCK_SIZE+bh->b_data) { // de 已经超过一个逻辑块的数据长度（1024字节）：当前缓冲块已经搜索完毕
                        brelse(bh); // 释放缓冲块
                        bh = NULL; // 缓冲块指针指向空
                        
                        // 读取该目录数据区的下一个逻辑块
                        // i/DIR_ENTRIES_PER_BLOCK : 计算当前目录项对应的数据块号
                        // bmap: 根据数据块号来计算对应的逻辑块号
                        if (!(block = bmap(*dir,i/DIR_ENTRIES_PER_BLOCK)) || // 计算当前目录项的逻辑块号失败
                            !(bh = bread((*dir)->i_dev,block))) {  // 读取当前目录项的逻辑块对应的数据块到高速缓冲区失败
                                i += DIR_ENTRIES_PER_BLOCK; // 跳过一个数据块的目录项个数
                                continue; // 重新开始循环
                        }
                        // 成功读取目录数据区的下一个逻辑块到高速缓冲区
                        de = (struct dir_entry *) bh->b_data; // 重新让 de 指向缓冲区的数据部分开头
                }
                if (match(namelen,name,de)) { // 目录项结构指针de的文件名字段和 name 匹配
                        *res_dir = de; //  *res_dir 设置为 de 
                        return bh; // 返回对应的高速缓冲块指针
                }
                de++; // 指向下一个目录项
                i++; // 目录项的个数递增 1 
        }
        brelse(bh); // 没有找到对应文件名的目录项，释放高速缓冲块，返回 NULL 
        return NULL;
}

/*
 *	add_entry()
 *
 * adds a file entry to the specified directory, using the same
 * semantics as find_entry(). It returns NULL if it failed.
 *
 * NOTE!! The inode part of 'de' is left at 0 - which means you
 * may not sleep between calling this and putting something into
 * the entry, as someone else might have used it while you slept.
 */
static struct buffer_head * add_entry(struct m_inode * dir,
                                      const char * name, int namelen, struct dir_entry ** res_dir)
{
        int block,i;
        struct buffer_head * bh;
        struct dir_entry * de;

        *res_dir = NULL;
#ifdef NO_TRUNCATE
        if (namelen > NAME_LEN)
                return NULL;
#else
        if (namelen > NAME_LEN)
                namelen = NAME_LEN;
#endif
        if (!namelen)
                return NULL;
        if (!(block = dir->i_zone[0]))
                return NULL;
        if (!(bh = bread(dir->i_dev,block)))
                return NULL;
        i = 0;
        de = (struct dir_entry *) bh->b_data;
        while (1) {
                if ((char *)de >= BLOCK_SIZE+bh->b_data) {
                        brelse(bh);
                        bh = NULL;
                        block = create_block(dir,i/DIR_ENTRIES_PER_BLOCK);
                        if (!block)
                                return NULL;
                        if (!(bh = bread(dir->i_dev,block))) {
                                i += DIR_ENTRIES_PER_BLOCK;
                                continue;
                        }
                        de = (struct dir_entry *) bh->b_data;
                }
                if (i*sizeof(struct dir_entry) >= dir->i_size) {
                        de->inode=0;
                        dir->i_size = (i+1)*sizeof(struct dir_entry);
                        dir->i_dirt = 1;
                        dir->i_ctime = CURRENT_TIME;
                }
                if (!de->inode) {
                        dir->i_mtime = CURRENT_TIME;
                        for (i=0; i < NAME_LEN ; i++)
                                de->name[i]=(i<namelen)?get_fs_byte(name+i):0;
                        bh->b_dirt = 1;
                        *res_dir = de;
                        return bh;
                }
                de++;
                i++;
        }
        brelse(bh);
        return NULL;
}

/*
 *	get_dir()
 *
 * Getdir traverses the pathname until it hits the topmost directory.
 * It returns NULL on failure.
 */
static struct m_inode * get_dir(const char * pathname)
{
        char c;
        const char * thisname;
        struct m_inode * inode;
        struct buffer_head * bh;
        int namelen,inr,idev;
        struct dir_entry * de;

        if (!current->root || !current->root->i_count)
                panic("No root inode");
        if (!current->pwd || !current->pwd->i_count)
                panic("No cwd inode");
        if ((c=get_fs_byte(pathname))=='/') {
                inode = current->root;
                pathname++;
        } else if (c)
                inode = current->pwd;
        else
                return NULL;	/* empty name is bad */
        inode->i_count++;
        while (1) {
                thisname = pathname;
                if (!S_ISDIR(inode->i_mode) || !permission(inode,MAY_EXEC)) {
                        iput(inode);
                        return NULL;
                }
                for(namelen=0;(c=get_fs_byte(pathname++))&&(c!='/');namelen++)
                        /* nothing */ ;
                if (!c)
                        return inode;
                if (!(bh = find_entry(&inode,thisname,namelen,&de))) {
                        iput(inode);
                        return NULL;
                }
                inr = de->inode;
                idev = inode->i_dev;
                brelse(bh);
                iput(inode);
                if (!(inode = iget(idev,inr)))
                        return NULL;
        }
}

/*
 *	dir_namei()
 *
 * dir_namei() returns the inode of the directory of the
 * specified name, and the name within that directory.
 */
static struct m_inode * dir_namei(const char * pathname,
                                  int * namelen, const char ** name)
{
        char c;
        const char * basename;
        struct m_inode * dir;

        if (!(dir = get_dir(pathname)))
                return NULL;
        basename = pathname;
        while ((c=get_fs_byte(pathname++)))
                if (c=='/')
                        basename=pathname;
        *namelen = pathname-basename-1;
        *name = basename;
        return dir;
}

/*
 *	namei()
 *
 * is used by most simple commands to get the inode of a specified name.
 * Open, link etc use their own routines, but this is enough for things
 * like 'chmod' etc.
 */
struct m_inode * namei(const char * pathname)
{
        const char * basename;
        int inr,dev,namelen;
        struct m_inode * dir;
        struct buffer_head * bh;
        struct dir_entry * de;

        if (!(dir = dir_namei(pathname,&namelen,&basename)))
                return NULL;
        if (!namelen)			/* special case: '/usr/' etc */
                return dir;
        bh = find_entry(&dir,basename,namelen,&de);
        if (!bh) {
                iput(dir);
                return NULL;
        }
        inr = de->inode;
        dev = dir->i_dev;
        brelse(bh);
        iput(dir);
        dir=iget(dev,inr);
        if (dir) {
                dir->i_atime=CURRENT_TIME;
                dir->i_dirt=1;
        }
        return dir;
}

/*
 *	open_namei()
 *
 * namei for open - this is in fact almost the whole open-routine.
 */
int open_namei(const char * pathname, int flag, int mode,
               struct m_inode ** res_inode)
{
        const char * basename;
        int inr,dev,namelen;
        struct m_inode * dir, *inode;
        struct buffer_head * bh;
        struct dir_entry * de;

        if ((flag & O_TRUNC) && !(flag & O_ACCMODE))
                flag |= O_WRONLY;
        mode &= 0777 & ~current->umask;
        mode |= I_REGULAR;
        if (!(dir = dir_namei(pathname,&namelen,&basename)))
                return -ENOENT;
        if (!namelen) {			/* special case: '/usr/' etc */
                if (!(flag & (O_ACCMODE|O_CREAT|O_TRUNC))) {
                        *res_inode=dir;
                        return 0;
                }
                iput(dir);
                return -EISDIR;
        }
        bh = find_entry(&dir,basename,namelen,&de);
        if (!bh) {
                if (!(flag & O_CREAT)) {
                        iput(dir);
                        return -ENOENT;
                }
                if (!permission(dir,MAY_WRITE)) {
                        iput(dir);
                        return -EACCES;
                }
                inode = new_inode(dir->i_dev);
                if (!inode) {
                        iput(dir);
                        return -ENOSPC;
                }
                inode->i_uid = current->euid;
                inode->i_mode = mode;
                inode->i_dirt = 1;
                bh = add_entry(dir,basename,namelen,&de);
                if (!bh) {
                        inode->i_nlinks--;
                        iput(inode);
                        iput(dir);
                        return -ENOSPC;
                }
                de->inode = inode->i_num;
                bh->b_dirt = 1;
                brelse(bh);
                iput(dir);
                *res_inode = inode;
                return 0;
        }
        inr = de->inode;
        dev = dir->i_dev;
        brelse(bh);
        iput(dir);
        if (flag & O_EXCL)
                return -EEXIST;
        if (!(inode=iget(dev,inr)))
                return -EACCES;
        if ((S_ISDIR(inode->i_mode) && (flag & O_ACCMODE)) ||
            !permission(inode,ACC_MODE(flag))) {
                iput(inode);
                return -EPERM;
        }
        inode->i_atime = CURRENT_TIME;
        if (flag & O_TRUNC)
                truncate(inode);
        *res_inode = inode;
        return 0;
}

int sys_mknod(const char * filename, int mode, int dev)
{
        const char * basename;
        int namelen;
        struct m_inode * dir, * inode;
        struct buffer_head * bh;
        struct dir_entry * de;
	
        if (!suser())
                return -EPERM;
        if (!(dir = dir_namei(filename,&namelen,&basename)))
                return -ENOENT;
        if (!namelen) {
                iput(dir);
                return -ENOENT;
        }
        if (!permission(dir,MAY_WRITE)) {
                iput(dir);
                return -EPERM;
        }
        bh = find_entry(&dir,basename,namelen,&de);
        if (bh) {
                brelse(bh);
                iput(dir);
                return -EEXIST;
        }
        inode = new_inode(dir->i_dev);
        if (!inode) {
                iput(dir);
                return -ENOSPC;
        }
        inode->i_mode = mode;
        if (S_ISBLK(mode) || S_ISCHR(mode))
                inode->i_zone[0] = dev;
        inode->i_mtime = inode->i_atime = CURRENT_TIME;
        inode->i_dirt = 1;
        bh = add_entry(dir,basename,namelen,&de);
        if (!bh) {
                iput(dir);
                inode->i_nlinks=0;
                iput(inode);
                return -ENOSPC;
        }
        de->inode = inode->i_num;
        bh->b_dirt = 1;
        iput(dir);
        iput(inode);
        brelse(bh);
        return 0;
}

int sys_mkdir(const char * pathname, int mode)
{
        const char * basename;
        int namelen;
        struct m_inode * dir, * inode;
        struct buffer_head * bh, *dir_block;
        struct dir_entry * de;

        if (!suser())
                return -EPERM;
        if (!(dir = dir_namei(pathname,&namelen,&basename)))
                return -ENOENT;
        if (!namelen) {
                iput(dir);
                return -ENOENT;
        }
        if (!permission(dir,MAY_WRITE)) {
                iput(dir);
                return -EPERM;
        }
        bh = find_entry(&dir,basename,namelen,&de);
        if (bh) {
                brelse(bh);
                iput(dir);
                return -EEXIST;
        }
        inode = new_inode(dir->i_dev);
        if (!inode) {
                iput(dir);
                return -ENOSPC;
        }
        inode->i_size = 32;
        inode->i_dirt = 1;
        inode->i_mtime = inode->i_atime = CURRENT_TIME;
        if (!(inode->i_zone[0]=new_block(inode->i_dev))) {
                iput(dir);
                inode->i_nlinks--;
                iput(inode);
                return -ENOSPC;
        }
        inode->i_dirt = 1;
        if (!(dir_block=bread(inode->i_dev,inode->i_zone[0]))) {
                iput(dir);
                free_block(inode->i_dev,inode->i_zone[0]);
                inode->i_nlinks--;
                iput(inode);
                return -ERROR;
        }
        de = (struct dir_entry *) dir_block->b_data;
        de->inode=inode->i_num;
        strcpy(de->name,".");
        de++;
        de->inode = dir->i_num;
        strcpy(de->name,"..");
        inode->i_nlinks = 2;
        dir_block->b_dirt = 1;
        brelse(dir_block);
        inode->i_mode = I_DIRECTORY | (mode & 0777 & ~current->umask);
        inode->i_dirt = 1;
        bh = add_entry(dir,basename,namelen,&de);
        if (!bh) {
                iput(dir);
                free_block(inode->i_dev,inode->i_zone[0]);
                inode->i_nlinks=0;
                iput(inode);
                return -ENOSPC;
        }
        de->inode = inode->i_num;
        bh->b_dirt = 1;
        dir->i_nlinks++;
        dir->i_dirt = 1;
        iput(dir);
        iput(inode);
        brelse(bh);
        return 0;
}

/*
 * routine to check that the specified directory is empty (for rmdir)
 */
static int empty_dir(struct m_inode * inode)
{
        int nr,block;
        int len;
        struct buffer_head * bh;
        struct dir_entry * de;

        len = inode->i_size / sizeof (struct dir_entry);
        if (len<2 || !inode->i_zone[0] ||
            !(bh=bread(inode->i_dev,inode->i_zone[0]))) {
                printk("warning - bad directory on dev %04x\n",inode->i_dev);
                return 0;
        }
        de = (struct dir_entry *) bh->b_data;
        if (de[0].inode != inode->i_num || !de[1].inode || 
            strcmp(".",de[0].name) || strcmp("..",de[1].name)) {
                printk("warning - bad directory on dev %04x\n",inode->i_dev);
                return 0;
        }
        nr = 2;
        de += 2;
        while (nr<len) {
                if ((void *) de >= (void *) (bh->b_data+BLOCK_SIZE)) {
                        brelse(bh);
                        block=bmap(inode,nr/DIR_ENTRIES_PER_BLOCK);
                        if (!block) {
                                nr += DIR_ENTRIES_PER_BLOCK;
                                continue;
                        }
                        if (!(bh=bread(inode->i_dev,block)))
                                return 0;
                        de = (struct dir_entry *) bh->b_data;
                }
                if (de->inode) {
                        brelse(bh);
                        return 0;
                }
                de++;
                nr++;
        }
        brelse(bh);
        return 1;
}

int sys_rmdir(const char * name)
{
        const char * basename;
        int namelen;
        struct m_inode * dir, * inode;
        struct buffer_head * bh;
        struct dir_entry * de;

        if (!suser())
                return -EPERM;
        if (!(dir = dir_namei(name,&namelen,&basename)))
                return -ENOENT;
        if (!namelen) {
                iput(dir);
                return -ENOENT;
        }
        if (!permission(dir,MAY_WRITE)) {
                iput(dir);
                return -EPERM;
        }
        bh = find_entry(&dir,basename,namelen,&de);
        if (!bh) {
                iput(dir);
                return -ENOENT;
        }
        if (!(inode = iget(dir->i_dev, de->inode))) {
                iput(dir);
                brelse(bh);
                return -EPERM;
        }
        if ((dir->i_mode & S_ISVTX) && current->euid &&
            inode->i_uid != current->euid) {
                iput(dir);
                iput(inode);
                brelse(bh);
                return -EPERM;
        }
        if (inode->i_dev != dir->i_dev || inode->i_count>1) {
                iput(dir);
                iput(inode);
                brelse(bh);
                return -EPERM;
        }
        if (inode == dir) {	/* we may not delete ".", but "../dir" is ok */
                iput(inode);
                iput(dir);
                brelse(bh);
                return -EPERM;
        }
        if (!S_ISDIR(inode->i_mode)) {
                iput(inode);
                iput(dir);
                brelse(bh);
                return -ENOTDIR;
        }
        if (!empty_dir(inode)) {
                iput(inode);
                iput(dir);
                brelse(bh);
                return -ENOTEMPTY;
        }
        if (inode->i_nlinks != 2)
                printk("empty directory has nlink!=2 (%d)",inode->i_nlinks);
        de->inode = 0;
        bh->b_dirt = 1;
        brelse(bh);
        inode->i_nlinks=0;
        inode->i_dirt=1;
        dir->i_nlinks--;
        dir->i_ctime = dir->i_mtime = CURRENT_TIME;
        dir->i_dirt=1;
        iput(dir);
        iput(inode);
        return 0;
}

int sys_unlink(const char * name)
{
        const char * basename;
        int namelen;
        struct m_inode * dir, * inode;
        struct buffer_head * bh;
        struct dir_entry * de;

        if (!(dir = dir_namei(name,&namelen,&basename)))
                return -ENOENT;
        if (!namelen) {
                iput(dir);
                return -ENOENT;
        }
        if (!permission(dir,MAY_WRITE)) {
                iput(dir);
                return -EPERM;
        }
        bh = find_entry(&dir,basename,namelen,&de);
        if (!bh) {
                iput(dir);
                return -ENOENT;
        }
        if (!(inode = iget(dir->i_dev, de->inode))) {
                iput(dir);
                brelse(bh);
                return -ENOENT;
        }
        if ((dir->i_mode & S_ISVTX) && !suser() &&
            current->euid != inode->i_uid &&
            current->euid != dir->i_uid) {
                iput(dir);
                iput(inode);
                brelse(bh);
                return -EPERM;
        }
        if (S_ISDIR(inode->i_mode)) {
                iput(inode);
                iput(dir);
                brelse(bh);
                return -EPERM;
        }
        if (!inode->i_nlinks) {
                printk("Deleting nonexistent file (%04x:%d), %d\n",
                       inode->i_dev,inode->i_num,inode->i_nlinks);
                inode->i_nlinks=1;
        }
        de->inode = 0;
        bh->b_dirt = 1;
        brelse(bh);
        inode->i_nlinks--;
        inode->i_dirt = 1;
        inode->i_ctime = CURRENT_TIME;
        iput(inode);
        iput(dir);
        return 0;
}

int sys_link(const char * oldname, const char * newname)
{
        struct dir_entry * de;
        struct m_inode * oldinode, * dir;
        struct buffer_head * bh;
        const char * basename;
        int namelen;

        oldinode=namei(oldname);
        if (!oldinode)
                return -ENOENT;
        if (S_ISDIR(oldinode->i_mode)) {
                iput(oldinode);
                return -EPERM;
        }
        dir = dir_namei(newname,&namelen,&basename);
        if (!dir) {
                iput(oldinode);
                return -EACCES;
        }
        if (!namelen) {
                iput(oldinode);
                iput(dir);
                return -EPERM;
        }
        if (dir->i_dev != oldinode->i_dev) {
                iput(dir);
                iput(oldinode);
                return -EXDEV;
        }
        if (!permission(dir,MAY_WRITE)) {
                iput(dir);
                iput(oldinode);
                return -EACCES;
        }
        bh = find_entry(&dir,basename,namelen,&de);
        if (bh) {
                brelse(bh);
                iput(dir);
                iput(oldinode);
                return -EEXIST;
        }
        bh = add_entry(dir,basename,namelen,&de);
        if (!bh) {
                iput(dir);
                iput(oldinode);
                return -ENOSPC;
        }
        de->inode = oldinode->i_num;
        bh->b_dirt = 1;
        brelse(bh);
        iput(dir);
        oldinode->i_nlinks++;
        oldinode->i_ctime = CURRENT_TIME;
        oldinode->i_dirt = 1;
        iput(oldinode);
        return 0;
}
