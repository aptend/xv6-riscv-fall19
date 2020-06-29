## Task: Memory allocator

专门为Lab lock增加了一个系统调用，`int ntas(int flag)`: 

- `flag`为0，把现有自旋锁的`n nts`统计数据清零。
- 非0开始打印统计信息。分为两个部分：

  1. 单独的锁信息：只打印名字为`kmem`和`bcache`的锁，分别属于`kalloc`和文件系统
  2. 所有自旋锁中`nts`最大的5个锁


默认初始状态：

```shell
$ kalloctest
start test0
test0 results:
=== lock kmem/bcache stats
lock: kmem: #test-and-set 161724 #acquire() 433008
lock: bcache: #test-and-set 0 #acquire() 812
=== top 5 contended locks:
lock: kmem: #test-and-set 161724 #acquire() 433008
lock: proc: #test-and-set 290 #acquire() 961
lock: proc: #test-and-set 240 #acquire() 962
lock: proc: #test-and-set 72 #acquire() 907
lock: proc: #test-and-set 68 #acquire() 907
```



任务要求，为每个CPU增加一个`kmem`锁，使得大部分时间都不产生竞争，`nts`为0，只有在窃取其他CPU的空闲列表时才会可能产生锁竞争



修改后的效果的可以看到，前5繁忙的锁中不再包含`kmem`

```shell
$ kalloctest
start test0
test0 results:
=== lock kmem/bcache stats
lock: kmem: #test-and-set 0 #acquire() 33167
lock: kmem: #test-and-set 0 #acquire() 200114
lock: kmem: #test-and-set 0 #acquire() 199752
lock: bcache: #test-and-set 0 #acquire() 28
=== top 5 contended locks:
lock: proc: #test-and-set 22303 #acquire() 180082
lock: proc: #test-and-set 4162 #acquire() 180130
lock: proc: #test-and-set 4115 #acquire() 180129
lock: proc: #test-and-set 342 #acquire() 180070
lock: proc: #test-and-set 39 #acquire() 180070
test0 OK
start test1
total allocated number of pages: 200000 (out of 32768)
test1 OK
```

### 有多少个CPU？

重新回顾一下系统启动的过程

`entry.S`

每个CPU都从这里的指令开始执行。主要工作是为每个CPU分配栈，每个4K，空间已经由`start.c`声明了
```c
__attribute__ ((aligned (16))) char stack0[4096 * NCPU]
```

然后调用`start.c/start`

```assembly
	# qemu -kernel starts at 0x1000. the instructions
    # there seem to be provided by qemu, as if it
    # were a ROM. the code at 0x1000 jumps to
    # 0x8000000, the _start function here,
    # in machine mode. each CPU starts here.
.section .data
.globl stack0
.section .text
.globl start
.section .text
.globl _entry
_entry:
		# set up a stack for C.
        # stack0 is declared in start.c,
        # with a 4096-byte stack per CPU.
        # sp = stack0 + (hartid * 4096)
        la sp, stack0
        li a0, 1024*4
	csrr a1, mhartid
        addi a1, a1, 1
        mul a0, a0, a1
        add sp, sp, a0
		# jump to start() in start.c，不再返回，最终会落脚到scheduler()
        call start
junk:
        j junk
```



`start`

在machine mode下，初始化CPU中的各个特权寄存器，比如`epc`被写入`main.c/main`的地址，比如启动时钟中断，之后使用`mret`跳转



`main`

到这里用一个CPU完成系统组件的初始化，在这一步才会再写入特殊寄存器，开启页表、trap、外围设备中断等。然后再开启启动其他的CPU，进入`scheduler`，到同一个`proc[]`店面去领取`RUNNABLE`的proc。

```c
volatile static int started = 0;

// start() jumps here in supervisor mode on all CPUs.
void main()
{
  if(cpuid() == 0){
    consoleinit();
    printfinit();
    printf("\n");
    printf("xv6 kernel is booting\n");
    printf("hart 0 starting\n");
    printf("\n");
    kinit();         // physical page allocator
    kvminit();       // create kernel page table
    kvminithart();   // turn on paging
    procinit();      // process table
    trapinit();      // trap vectors
    trapinithart();  // install kernel trap vector
    plicinit();      // set up interrupt controller
    plicinithart();  // ask PLIC for device interrupts
    binit();         // buffer cache
    iinit();         // inode cache
    fileinit();      // file table
    virtio_disk_init(minor(ROOTDEV)); // emulated hard disk
    userinit();      // first user process
    __sync_synchronize();
    started = 1;
  } else {
    while(started == 0)
      ;
    __sync_synchronize();
    printf("hart %d starting\n", cpuid());
    kvminithart();    // turn on paging
    trapinithart();   // install kernel trap vector
    plicinithart();   // ask PLIC for device interrupts
  }

  scheduler();        
}

```



从启动的打印来看，`xv6`共启动了3个CPU，编号为0,1,2

```shell
$ make qemu
#...
xv6 kernel is booting
hart 0 starting

virtio disk init 0
hart 1 starting
hart 2 starting
init: starting sh
```

`init: starting sh`后于`hart 1/2 starting`打印，因为`userinit`只是分配好了proc，把`p->state`设置了`RUNNABLE`，并没有真正执行，然后就放开了`started=1`，两个CPU立马执行，比`hart0`的`scheduler()`还快。



### allocator的实现计划

- `struct kmem kmems[NCPU];` NCPU个`kmem`结构的列表

- `kinit()`时将总体的空闲空间NCPU等分

- `kalloc()`先获取cpuid，在对应的`kmem`上操作，当无法分配时，反向扫描`kmems`列表，把空闲列表折半，添加到当前`kmem`上

- `kfree()`就在对应的`kmem`上操作，没有额外的逻辑
