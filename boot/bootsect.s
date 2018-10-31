!
! SYS_SIZE is the number of clicks (16 bytes) to be loaded.
! 0x3000 is 0x30000 bytes = 196kB, more than enough for current
! versions of linux
!
SYSSIZE = 0x3000
! 要加载的系统模块长度，单位是节，每节是16字节，如果以1024字节为1KB计算，总共是192KB,
! 对于当前的内核版本来说应该是足够的, 因为0x80000对应的是512KB，
! system模块最终被放置在0x0000处，所以不会覆盖到位于0x90000开始的bootsect和setup
!
!	bootsect.s		(C) 1991 Linus Torvalds
!
! bootsect.s is loaded at 0x7c00 by the bios-startup routines, and moves
! iself out of the way to address 0x90000, and jumps there.
!
! 首先bootsect.s 会被BIOS加载到 0x7c00 处(31KB处)，然后把自己移动到 0x90000 处(576KB)，最后跳跃到这里
!	
! It then loads 'setup' directly after itself (0x90200), and the system
! at 0x10000, using BIOS interrupts. 
!
!
! 接着会使用ROM提供的中断向量把 setup.s 加载到 0x90200 (576.5KB)处, 以及把 system 模块
! 加载到 0x10000 处
	
! NOTE! currently system is at most 8*65536 bytes long. This should be no
! problem, even in the future. I want to keep it simple. This 512 kB
! kernel size should be enough, especially as this doesn't contain the
! buffer cache as in minix
!
! The loader has been made as simple as possible, and continuos
! read errors will result in a unbreakable loop. Reboot by hand. It
! loads pretty fast by getting whole sectors at a time whenever possible.

	! .globl用于定义随后的标识符是外部的或者局部的，并且不使用也强制引入
	! 在链接多个目标时，ld86程序会根据不同的段的类型把各个目标模块中的类分别组合（合并）
	! 实际下面的段都在一个重复地址中，没有真正的分段
	! 
.globl begtext, begdata, begbss, endtext, enddata, endbss
.text 				!文本代码段
begtext:			! 标号，代表其所在位置的地址
.data				! 数据段
begdata:
.bss				! 未初始化代码段
begbss:				
.text

	! 定义常量
SETUPLEN = 4				! nr of setup-sectors
					! setup 占用的扇区数
BOOTSEG  = 0x07c0			! original address of boot-sector
					! ROM把 bootsect 加载的地址
INITSEG  = 0x9000			! we move boot here - out of the way
					! bootsect被移动到这里 0x90000
SETUPSEG = 0x9020			! setup starts here
					! setup开始的地址 0x90200
SYSSEG   = 0x1000			! system loaded at 0x10000 (65536).
					! sytem模块开始的地址 0x10000
ENDSEG   = SYSSEG + SYSSIZE		! where to stop loading
					! system模块结束的地址 0x40000

! ROOT_DEV:	0x000 - same type of floppy as boot.
!		0x301 - first partition on first drive etc
	! 设备号 = 主设备号 * 256 + 次设备号
	! dev_no = (major << 8) + minor
	! 主设备号：1-内存，2-软盘，3-硬盘，4-ttyx，5-tty，6-并行口，7-非命名管道
	! 0x300 - /dev/hd0 - 代表第一个硬盘
	! 0x301 - /dev/hd1 - 第一个硬盘第一个分区
	! 0x302 - /dev/hd2 - 第一个硬盘第二个分区
	! 0x303 - /dev/hd3 - 第一个硬盘第三个分区
	! 0x304 - /dev/hd4 - 第一个硬盘第四个分区
	! 0x305 - /dev/hd5 - 第二个硬盘
	! 0x306 - /dev/hd6 - 第二个硬盘第一个分区
	! .......
ROOT_DEV = 0x306	! 第二个硬盘第一个分区

	! entry 迫使链接程序生成的可执行文件中包含相应的标识符或标号，这里是程序的开始执行处
	! 把从 0x7c00 开始的 256 个字 (512字节) 移动到 0x90000处，然后跳到标号 go 处执行 
entry _start
_start:
	mov	ax,#BOOTSEG 		! ax = 0x07c0
	mov	ds,ax			! ds = ax = 0x07c0
	mov	ax,#INITSEG		! ax = 0x9000
	mov	es,ax			! es = 0x9000
	mov	cx,#256			! cx = 0x256 
	sub	si,si			! si = 0, 拷贝源地址  ds:si = 0x07c0:0000
	sub	di,di			! di = 0, 拷贝目的地址 es:di = 0x9000:0000
	rep				! 重复执行拷贝字，直到cs为0
	movw
	jmpi	go,INITSEG		! 段间长跳转，go为段内偏移地址，INITSEG为段地址：0x9000
go:	mov	ax,cs			! ax = cs = 0x9000
	! 从下面开始，CPU在已移动到0x90000位置处的代码执行
	! 这段代码设置几个段寄存器为0x9000
	! 因为从0x90200处开始还要放置setup程序，此时的setup程序大约是4个扇区的大小，
	! 所以栈顶sp要指向大于(200 + 200 *4 + 堆栈大小)处，这里使用的值是0x9FF00  
	mov	ds,ax			! ds = ax = 0x9000
	mov	es,ax			! es = ax = 0x9000
! put stack at 0x9ff00. 设置栈顶地址为 0x9FF00
	mov	ss,ax			! ss = ax = 0x9000
	mov	sp,#0xFF00		! arbitrary value >>512

! load the setup-sectors directly after the bootblock.
! Note that 'es' is already set up.
	! 在 bootsect 程序块后紧跟着加载 setup 模块的代码数据
	! 注意：此时的es 已经设置为 0x9000
	! 利用ROM BIOS 的中断向量 INT 0x13 将 setup 模块从磁盘的第二个扇区读到 0x90200 处
load_setup:
	! dh = 磁头号，		dl = 驱动器号（如果是硬盘则位7需要置位）
	mov	dx,#0x0000		! drive 0, head 0
	! ch = 磁道号的低8位，	cl = 开始扇区(0~5)，磁道号高2位(6~7) 
	mov	cx,#0x0002		! sector 2, track 0
	! es:bx -> 指向数据缓存区，如果出错则 CF 置位，ah 中保留出错码
	mov	bx,#0x0200		! address = 512, in INITSEG
	! ah = 0x02: 读磁盘，	al = 需要读出的扇区数量
	mov	ax,#0x0200+SETUPLEN	! service 2, nr of sectors
	! ROM BIOS 中读取磁盘到内存的中断向量
	int	0x13			! read it
	jnc	ok_load_setup		! ok - continue
	
	! 在读操作过程中，如果出错，则复位驱动器，并重试，没有退路
	mov	dx,#0x0000
	mov	ax,#0x0000		! reset the diskette
	int	0x13
	j	load_setup

ok_load_setup:

! Get disk drive parameters, specifically nr of sectors/track

	mov	dl,#0x00
	mov	ax,#0x0800		! AH=8 is get drive parameters
	int	0x13
	mov	ch,#0x00
	seg cs
	mov	sectors,cx
	mov	ax,#INITSEG
	mov	es,ax

! Print some inane message

	mov	ah,#0x03		! read cursor pos
	xor	bh,bh
	int	0x10
	
	mov	cx,#24
	mov	bx,#0x0007		! page 0, attribute 7 (normal)
	mov	bp,#msg1
	mov	ax,#0x1301		! write string, move cursor
	int	0x10

! ok, we've written the message, now
! we want to load the system (at 0x10000)
	!把 system模块加载到 0x10000处
	mov	ax,#SYSSEG
	mov	es,ax		! segment of 0x010000
	call	read_it
	call	kill_motor

! After that we check which root-device to use. If the device is
! defined (!= 0), nothing is done and the given device is used.
! Otherwise, either /dev/PS0 (2,28) or /dev/at0 (2,8), depending
! on the number of sectors that the BIOS reports currently.
	! 获得 root_dev 根文件区的设备号
	seg cs
	mov	ax,root_dev
	cmp	ax,#0
	jne	root_defined
	seg cs
	mov	bx,sectors
	mov	ax,#0x0208		! /dev/ps0 - 1.2Mb
	cmp	bx,#15
	je	root_defined
	mov	ax,#0x021c		! /dev/PS0 - 1.44Mb
	cmp	bx,#18
	je	root_defined
undef_root:
	jmp undef_root
root_defined:
	seg cs
	mov	root_dev,ax

! after that (everyting loaded), we jump to
! the setup-routine loaded directly after
! the bootblock:

	jmpi	0,SETUPSEG

! This routine loads the system at address 0x10000, making sure
! no 64kB boundaries are crossed. We try to load it as fast as
! possible, loading whole tracks whenever we can.
!
! in:	es - starting address segment (normally 0x1000)
!
sread:	.word 1+SETUPLEN	! sectors read of current track
head:	.word 0			! current head
track:	.word 0			! current track

read_it:
	mov ax,es
	test ax,#0x0fff
die:	jne die			! es must be at 64kB boundary
	xor bx,bx		! bx is starting address within segment
rp_read:
	mov ax,es
	cmp ax,#ENDSEG		! have we loaded all yet?
	jb ok1_read
	ret
ok1_read:
	seg cs
	mov ax,sectors
	sub ax,sread
	mov cx,ax
	shl cx,#9
	add cx,bx
	jnc ok2_read
	je ok2_read
	xor ax,ax
	sub ax,bx
	shr ax,#9
ok2_read:
	call read_track
	mov cx,ax
	add ax,sread
	seg cs
	cmp ax,sectors
	jne ok3_read
	mov ax,#1
	sub ax,head
	jne ok4_read
	inc track
ok4_read:
	mov head,ax
	xor ax,ax
ok3_read:
	mov sread,ax
	shl cx,#9
	add bx,cx
	jnc rp_read
	mov ax,es
	add ax,#0x1000
	mov es,ax
	xor bx,bx
	jmp rp_read

read_track:
	push ax
	push bx
	push cx
	push dx
	mov dx,track
	mov cx,sread
	inc cx
	mov ch,dl
	mov dx,head
	mov dh,dl
	mov dl,#0
	and dx,#0x0100
	mov ah,#2
	int 0x13
	jc bad_rt
	pop dx
	pop cx
	pop bx
	pop ax
	ret
bad_rt:	mov ax,#0
	mov dx,#0
	int 0x13
	pop dx
	pop cx
	pop bx
	pop ax
	jmp read_track

!/*
! * This procedure turns off the floppy drive motor, so
! * that we enter the kernel in a known state, and
! * don't have to worry about it later.
! */
kill_motor:
	push dx
	mov dx,#0x3f2
	mov al,#0
	outb
	pop dx
	ret

sectors:
	.word 0

msg1:
	.byte 13,10
	.ascii "Loading system ..."
	.byte 13,10,13,10

.org 508
root_dev:
	.word ROOT_DEV
boot_flag:
	.word 0xAA55

.text
endtext:
.data
enddata:
.bss
endbss:
