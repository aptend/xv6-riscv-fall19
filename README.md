# Lab lazy


## Task0: Code Walk Through


### 内核页表初始化

```cpp
void kvminit()
{
  // 确定64bit的一级目录地址
  kernel_pagetable = (pagetable_t) kalloc();
  memset(kernel_pagetable, 0, PGSIZE);

  // 一对一的IO映射 1 page
  kvmmap(UART0, UART0, PGSIZE, PTE_R | PTE_W);
  kvmmap(VIRTION(0), VIRTION(0), PGSIZE, PTE_R | PTE_W);
  kvmmap(VIRTION(1), VIRTION(1), PGSIZE, PTE_R | PTE_W);

  // CLINT 16 pages
  kvmmap(CLINT, CLINT, 0x10000, PTE_R | PTE_W);
  // PLIC 1024 pages
  kvmmap(PLIC, PLIC, 0x400000, PTE_R | PTE_W);

  // memlayout.h
  // #define KERNBASE 0x80000000L
  // #define PHYSTOP (KERNBASE + 128*1024*1024)  所以内核共映射了128M的内存
  // #define TRAMPOLINE (MAXVA - PGSIZE)

  // etext 在kernel.ld中，编译器根据代码量确定的对齐地址，ALGIN(0x1000)，不懂意思
  kvmmap(KERNBASE, KERNBASE, (uint64)etext-KERNBASE, PTE_R | PTE_X);

  // 映射数据段和空闲内存
  kvmmap((uint64)etext, (uint64)etext, PHYSTOP-(uint64)etext, PTE_R | PTE_W);

  // 只有trampoline不是直接映射，实际的位置已经出现在代码段了，KERNBASE～etext。该段代码二次映射
  kvmmap(TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);
}
```

`kvmmap(VA-PA-SIZE-PERM)`直接调用`mappages(PDADDR-VA-SIZE-PA-PERM)`  

利用va找到对应的PTE的指针，用物理地址和perm设置PTE就好了

其中`PGROUNDDOWN`的意思如下
```
#byte  0     4096   8192  12288
       |      |      |   |  |  ...    
#PAGE  0      1      2   |  3
                         |
      rounddown=8192 <- 9022 -> roundup=12288
       rounddown=12288 <- 12288 -> roundup = 12288
```

```cpp
int mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm)
{
  uint64 a, last;
  pte_t *pte;

  a = PGROUNDDOWN(va);
  last = PGROUNDDOWN(va + size - 1);
  for(;;){
    // 找到负责a处的PTE的地址
    if((pte = walk(pagetable, a, 1)) == 0)
      return -1;
    if(*pte & PTE_V)
      panic("remap");
    // 设置
    *pte = PA2PTE(pa) | perm | PTE_V;
    if(a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}
```

如何找负责地址a的pte？ `walk`模仿硬件的查表过程，读取两级目录，得到最后目录的地址，用index最低的9位定到pte上。

参数：一级目录地址，规整化的虚拟地址，是否是新增映射条目

如果不是新增映射条目，遇到invalid的标志位就要报错，否则就新开辟内存，也就是新建目录，来存放pte

```cpp
static pte_t * walk(pagetable_t pagetable, uint64 va, int alloc)
{
  if(va >= MAXVA)
    panic("walk");

  for(int level = 2; level > 0; level--) {
    pte_t *pte = &pagetable[PX(level, va)];
    if(*pte & PTE_V) {
      pagetable = (pagetable_t)PTE2PA(*pte);
    } else {
      // 分配新的内存给目录，并且初始化目录
      if(!alloc || (pagetable = (pde_t*)kalloc()) == 0)
        return 0;
      memset(pagetable, 0, PGSIZE);
      *pte = PA2PTE(pagetable) | PTE_V;
    }
  }
  return &pagetable[PX(0, va)];
}
```

### 用户进程内核栈的分配

`proc.c/procinit` 给64个进程结构设置好内核栈的位置，系统启动时执行。
```cpp
for(p = proc; p < &proc[NPROC]; p++) {
    initlock(&p->lock, "proc");

    // 分配一个物理页
    char *pa = kalloc();
    if(pa == 0)
      panic("kalloc");
    // 倒数第一的page是trampoline，倒数第2是第一个进程的内核栈，倒数第4是第二个进程的内核栈
    uint64 va = KSTACK((int) (p - proc));
    kvmmap(va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
    // 设置进程的内核栈地址，这是虚拟地址，需要内核页表配合
    p->kstack = va;
}
```

### 用户进程页表的初始化

用户进程表的初始化，当然发生在新建用户进程时，两个地方，系统启动时，新建`init`进程，以及之后`fork`调用。这里就以`fork`为例了。

第一步：`allocproc`  
`allocproc` 主要工作找一个没有使用的proc结构，设置pid、设置trapframe地址和基础用户态页表
```cpp
static struct proc* allocproc(void)
{
  struct proc *p;

  // 找一个未使用的proc结构， p->state = UNUSED，**加锁**

found:
  p->pid = allocpid();  // 全局自增 pid

  // 进程和内核用trapframe交换信息。
  // 给进程传递内核页表位置、trap handler地址；
  // 给内核传递系统调用的参数
  // 此时存的是物理地址，之后该地址在后一句 proc_pagetable 中由TRAPFRAME虚拟地址所指
  if((p->tf = (struct trapframe *)kalloc()) == 0){
    release(&p->lock);
    return 0;
  }

  // 初始化用户态页表，映射通用的 trampoline 和 trapframe，程序代码之后再映射
  // proc_pagetable 的内容如下
  // pagetable = uvmcreate(); // 调用 kalloc
  // mappages(pagetable, TRAMPOLINE, PGSIZE, (uint64)trampoline, PTE_R | PTE_X);
  // mappages(pagetable, TRAPFRAME, PGSIZE, (uint64)(p->tf), PTE_R | PTE_W);
  // return pagetable;
  p->pagetable = proc_pagetable(p);

  // 设置context等信息...

  return p;
}
```

第二步：`uvmcopy`  

`uvmcopy`的主要工作是拷贝用户进程的页表(mappages)和内存数据(memmove)  
拷贝过程发生在内核态中，又因为页表以及用户进程所分配堆内存，都出自于内核**直接映射**的预留空间，(end, PHSYSTOP)，使用`kalloc`获得，所以在可以方便地memmove  

参数`sz`，是`old`页表的大小，单位bytes，因为xv6的内存布局是单向生长的，用户栈固定为4K，直接用`sz`就足够描述当前进程所使用的内存空间。
```cpp
// 省略了一些错误处理
int uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
  pte_t *pte;
  uint64 pa, i;
  uint flags;
  char *mem;

  for(i = 0; i < sz; i += PGSIZE){
    pte = walk(old, i, 0);
    pa = PTE2PA(*pte);
    flags = PTE_FLAGS(*pte);
    // 拷贝物理内存
    mem = kalloc();
    memmove(mem, (char*)pa, PGSIZE);
    // 设置相同的虚拟地址映射
    if(mappages(new, i, PGSIZE, (uint64)mem, flags) != 0){
      kfree(mem);
      goto err;
    }
  }
  return 0;
```

第三步：设置/复制其他信息

包括但不限于(np: new proc)
- 复制sz np->sz = p->sz
- 设置父进程 np->parent = p;
- 复制trapframe内容*(np->tf) = *(p->tf)，同样利用了内核内存直接映射
- 设置子进程的fork返回值，np->tf->a0 = 0
- 复制文件描述符表，全局文件表项的refcnt+1
- 子进程状态为可运行，np->state = RUNNABLE
- ……





