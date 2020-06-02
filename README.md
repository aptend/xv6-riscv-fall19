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

### exec 用户进程页表的替换

exec的分为新建页表、加载文件、建立用户栈、准备main函数调用、替换页表、释放旧页表

新建页表  
proc的结构已经有了，只用新建页表，所以直接调用`proc_pagetable`，申请一级页表目录，用当前的`p->tf`做`trapframe`的映射，里面的必要数据会在最后统一设置。

加载文件  
先申请、设置页表，后从文件中读取内容到指定位置。按照elf文件的提示，一轮轮地 `uvmalloc + loadseg`
```cpp
// 从空闲内存中分配页，一页页设置 pte，包含U权限。
// newsz不必对齐，最后的sz会超过newsz。这也是内部碎片的由来
uint64 uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  char *mem;
  uint64 a;

  // round up，从下一个页位置开始申请，直到超过 newsz 大小
  oldsz = PGROUNDUP(oldsz);
  a = oldsz;
  for(; a < newsz; a += PGSIZE){
    mem = kalloc();
    if(mem == 0){
      // 系统内存不足，无法满足 newsz，恢复到 oldsz 大小
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
    memset(mem, 0, PGSIZE);
    if(mappages(pagetable, a, PGSIZE, (uint64)mem, PTE_W|PTE_X|PTE_R|PTE_U) != 0){
      // 页表映射失败，释放刚申请的内存，然后恢复到 oldsz 大小，之前使用的物理内存在 `uvmdealloc`中释放
      kfree(mem);
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
  }
  return newsz;
}

static int loadseg(pagetable_t pagetable, uint64 va, struct inode *ip, uint offset, uint sz) {
  // ..省略声明和 va 对齐检查

  for(i = 0; i < sz; i += PGSIZE){
    pa = walkaddr(pagetable, va + i);
    if(sz - i < PGSIZE)
      n = sz - i;
    else
      n = PGSIZE;
    // 一批4096 bytes地读取，最后一批可能不足4096
    if(readi(ip, 0, (uint64)pa, offset+i, n) != n)
      return -1;
  }
  return 0;
}
```

![user stack](https://s1.ax1x.com/2020/06/01/tJNvRI.png)

建立用户栈  
经过loadseg，text和data已经有了，所以会再申请两个页，上一个作为用户栈，下面一个uvmclear后作为guard。

```cpp
sz = PGROUNDUP(sz);
if((sz = uvmalloc(pagetable, sz, sz + 2*PGSIZE)) == 0)
  goto bad;
uvmclear(pagetable, sz-2*PGSIZE);
sp = sz;
stackbase = sp - PGSIZE;
```

准备main函数调用  
再参照上图，把argc、argv放到用户栈上，代码就略过了

替换并旧页表  
```cpp
oldpagetable = p->pagetable;
p->pagetable = pagetable;
p->sz = sz;
p->tf->epc = elf.entry;  // initial program counter = main
p->tf->sp = sp; // initial stack pointer
proc_freepagetable(oldpagetable, oldsz);
```

### sbrk调用

`sysproc.c/sys_sbrk` 简单调用了 `proc.c/growproc`，再根据n的正负调用 `uvmalloc` 或者 `uvmdemalloc`。
```cpp
int growproc(int n)
{
  uint sz;
  struct proc *p = myproc();

  sz = p->sz;
  if(n > 0){
    if((sz = uvmalloc(p->pagetable, sz, sz + n)) == 0) {
      return -1;
    }
  } else if(n < 0){
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  p->sz = sz;
  return 0;
}
```

## Task1: vmprint

加入一个表示层级的整型参数，递归打印就可以
```cpp
void _vmprint(pagetable_t pagetable, int lv) {
  for(int i = 0; i < 512; i++){
    pte_t pte = pagetable[i];
    if (pte & PTE_V) {
      printf("..");
      for(int j=0; j < lv; j++) {
        printf(" ..");
      }
      uint64 next = PTE2PA(pte);
      printf("%d: pte %p pa %p\n", i, (char *)pte, (char *)next);
      if (lv < 2) // 0、1集都是目录，需要递归
        _vmprint((pagetable_t)next, lv+1);
    }
  }
}
```

## Task2: lazy sbrk

按照说明，去掉`sys_sbrk`中的`growproc`代码，只保留sz的变化，运行`echo hi` 会显示
```shell
$ echo hi
usertrap(): unexpected scause 0x000000000000000f pid=3
            sepc=0x0000000000001258 stval=0x0000000000004008
va=0x0000000000004000 pte=0x0000000000000000
panic: uvmunmap: not mapped

```
1. `panic`结尾是无限循环，所以xv6就卡在那里不动了。
2. `usertrap()`未处理，打印错误信息后`p->killed = 1`，会调用`exit()`，不返回
3. 子进程退出，父进程会调用`wait`，回收proc资源，会导致调用uvmunmap销毁页表，但是因为只增加了sz，没有实际映射，在卸载时出错。

只增加sz用的是`sbrk`。对用户程序来说，调用`malloc`空间不足时，会触发`sbrk`，一次性申请64K的内存空间。

```cpp
static Header* morecore(uint nu)
{
  char *p;

  if(nu < 4096)
    nu = 4096;
  // 一次申请，4 * 16 = 64K
  p = sbrk(nu * sizeof(Header));
  
  // 略
}
```

也就解释了，为什么`exec(echo)`中释放旧页表时会出现卸载未映射的页，因为sz我们加了64K，但是实际分配映射并未发生。

## Task2~N: 完善lazy alloc

### `echo hi` 不报错

要让`echo`正常工作，只需要如下改动
- 修改`uvmunmap`在卸载未映射页时不报错，只打印信息  
- 修改`usertrap`，捕捉page fault，然后直接申请+映射

这时 `echo hi` 会输出
```sh
$ echo hi
uvmunmap: not mapped: va=0x0000000000005000 pte=0x0000000000000000
uvmunmap: not mapped: va=0x0000000000006000 pte=0x0000000000000000
uvmunmap: not mapped: va=0x0000000000007000 pte=0x0000000000000000
uvmunmap: not mapped: va=0x0000000000008000 pte=0x0000000000000000
uvmunmap: not mapped: va=0x0000000000009000 pte=0x0000000000000000
uvmunmap: not mapped: va=0x000000000000a000 pte=0x0000000000000000
uvmunmap: not mapped: va=0x000000000000b000 pte=0x0000000000000000
uvmunmap: not mapped: va=0x000000000000c000 pte=0x0000000000000000
uvmunmap: not mapped: va=0x000000000000d000 pte=0x0000000000000000
uvmunmap: not mapped: va=0x000000000000e000 pte=0x0000000000000000
uvmunmap: not mapped: va=0x000000000000f000 pte=0x0000000000000000
uvmunmap: not mapped: va=0x0000000000010000 pte=0x0000000000000000
uvmunmap: not mapped: va=0x0000000000011000 pte=0x0000000000000000
uvmunmap: not mapped: va=0x0000000000012000 pte=0x0000000000000000
..0: pte 0x0000000021fd4401 pa 0x0000000087f51000
.. ..0: pte 0x0000000021fd4001 pa 0x0000000087f50000
.. .. ..0: pte 0x0000000021fd481f pa 0x0000000087f52000
.. .. ..1: pte 0x0000000021fd3c0f pa 0x0000000087f4f000
.. .. ..2: pte 0x0000000021fd381f pa 0x0000000087f4e000
..255: pte 0x0000000021fd5001 pa 0x0000000087f54000
.. ..511: pte 0x0000000021fd4c01 pa 0x0000000087f53000
.. .. ..510: pte 0x0000000021fd9007 pa 0x0000000087f64000
.. .. ..511: pte 0x000000002000200b pa 0x0000000080008000
hi
```

可以看出sh实际只用了8K的堆内存，56K的页都未映射


### 处理负数参数

负数表示是释放内存，直接在`sys_sbrk`中调用`uvmdemalloc`，不需要lazy