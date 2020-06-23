根据Hints的基本路线：
  1. 先修改`fork`时调用的`uvmcopy`，避免复制物理内存，直接把父进程的物理内存映射到子进程的页表上，并且两方的页表都清除写入权限。
  2. 修改`usertrap`，识别因为COW造成的写入失败，复制该页到新的内存，并重新开启写入权限
  3. `kalloc.c`加入引用计数，最后一个引用消失时，`kfree()`才释放该页内存
  4. 修改`copyout`，和`usertrap`类似的方式处理COW的错误


但是实际做的时候，需要整体考虑，第3步kalloc中删除的反而应该先考虑，123都完成，才能一个可通过的测试


## `kalloc`

`kalloc`模块只支持4K大小的内存分配。
- 初始化，[PGROUNDUP(end), PHYSTOP]中的4K页起点加入空闲列表
- kfree(pa)，填充1到4K页，将pa加入空间列表
- kalloc()，从空闲列表中拿出一个pa返回

因为没有使用前置的元信息块，所以ref信息只能用数组来表示。
对xv6来说，只有最多64个进程，页最多只有64个ref，所以uint8就足够表示。
需要多少项，还是基于`PGROUNDUP(end)`和`PHYSTOP`计算



```cpp
void
kinit()
{
  initlock(&kmem.lock, "kmem");
  assert(PHYSTOP % PGSIZE == 0);
  kmem.rc = (uint8 *)end;
  // 分配rc数组的内存
  uint64 rc_bytes = ((PHYSTOP - PGROUNDUP((uint64)end)) >> 12) * sizeof(uint8);
  printf("kalloc supports %d 4K pages\n", rc_bytes / sizeof(uint8));
  kmem.page_start = (char *)(end + rc_bytes);
  freerange(kmem.page_start, (void *)PHYSTOP);
}

// pa_start: kernel代码数据映射之后的地址
// pa_end: KERNBASE+128M后的地址PHYSTOP
void freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

void kfree(void *pa)
{
  struct run *r;

  if (((uint64)pa % PGSIZE) != 0 || (char *)pa < kmem.page_start || (uint64)pa >= PHYSTOP)
    panic("kfree");
  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);
  uint64 idx = ((uint64)pa - (uint64)kmem.page_start) >> 12;
  r = (struct run*)pa;
  // rc直接置零，加入到空闲列表
  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  kmem.rc[idx] = 0;
  release(&kmem.lock);
}

void * kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r) {
    uint64 idx = ((uint64)r - (uint64)kmem.page_start) >> 12;
    kmem.freelist = r->next;
    kmem.rc[idx] = 1;
  }
  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}

uint8
kborrow(void *pa) {
  /* 引用计数+1, 返回引用计数 */
}
void
kdrop(void *pa) {
  /* 引用计数-1, 如果引用为0，调用kfree释放 */
}
```
使用方面：
- `uvmcopy`中要调用`kborrow`
- `uvmunmap`中`kfree`就应该替换为`kdrop`
- `trap.c`中复制新的页后，对原页要`kdrop`

## `uvmcopy`

用pte的第8-bit作为cow页的标记
`#define PTE_COW (1L << 8)`

```c
int
uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
  pte_t *pte;
  uint64 pa, i;

  for(i = 0; i < sz; i += PGSIZE){
    /* panic things */
    pa = PTE2PA(*pte);
    *pte &= ~PTE_W;   // 原表清除写入权限
    *pte |= PTE_COW;  // 原表增加COW标识
    kborrow(pa);      // 增加 pa 的引用
    if (mappages(new, i, PGSIZE, pa, PTE_FLAGS(*pte)) != 0)
      goto err;
  }
  return 0;

 err:
  uvmunmap(new, 0, i, 1);
  return -1;
}
```


## `trap`

这次因为没有lazy alloc，只用考虑写入的页错误，所以只处理错误号为15的`Store Page Fault`

`r_stval()`只提供了出错的虚拟地址，但是需要pte来判断COW，并获得PA，所以需要一个`ptewalk`，本质调用`walk(pg, va, 0)`
```cpp
else if (r_scause() == 15) {
  uint64 va = r_stval();
  uint64 a = PGROUNDDOWN(va);
  pte_t *pte = walkpte(p->pagetable, a);
  if (pte == 0) {
    printf("usertrap@15: no pte found\n");
    p->killed = 1;
    goto stop_early;
  }

  if ((*pte & PTE_COW) && (*pte & PTE_V) && (*pte & PTE_U)) {
    char *mem = kalloc();
    if (mem == 0) {
      p->killed = 1;
      goto stop_early;
    }
    uint64 pa = PTE2PA(*pte);
    memmove(mem, (char *)pa, PGSIZE);

    uint flags = PTE_FLAGS(*pte);
    flags &= ~PTE_COW;
    flags |= PTE_W;
    // 利用do_free=1 自动kdrop一次对pa的引用
    uvmunmap(p->pagetable, a, PGSIZE, 1);
    if (mappages(p->pagetable, a, PGSIZE, (uint64)mem, flags) != 0)
    {
      kfree(mem);
      p->killed = 1;
      goto stop_early;
    }
  }
} 
```
> 莫名奇妙出现一个大于MAXVA的va，导致Load Page Fault，最后发现trap中的  
> mappages(pagetable, va, size, pa, flags) 被写成  
> mappages(pagetable, va, pa, size, flags)
> 可气死我了！


## Pipe/Read

上面的改造完成后，直接运行cowtest，通过`simple`和`three`测试，但是`file`无法通过。

经过调试，发现是`sys_read`返回-1，来自`argfd`返回-1，来自`p->ofile[5]`为0，也就是不能定位到打开的文件。

原因是主进程`cowtest`循环第一次时，打开pipe，占用3，4文件描述符，写入fds
```sh
cowtest assign a fd @ 3
cowtest assign a fd @ 4
pipe: 3: 0x0x0000000080024ef8
pipe: 4: 0x0x0000000080024f20
loop 0: fds[3, 4]
```
然后调用fork，子进程sleep(1)。子进程睡眠时，主进程循环第二次，打开pipe，占用5，6描述符，写入fds

```sh
cowtest assign a fd @ 5
cowtest assign a fd @ 6
pipe: 5: 0x0x0000000080024f48
pipe: 6: 0x0x0000000080024f70
loop 1: fds[5, 6]
```

因为没有为`copyout`复制页，`sys_pipe`调用`copyout`会直接修改fds，造成子进程和父进程共用fds，当子进程醒来，就会尝试读fds[0]，此时为5，当然自己的`p->ofile`中并没有5号文件，因此出错。

所以接下来就改`copyout`，发现COW页就复制。

在`Lab lazy`中是把缺页的处理放到了`walkaddr`，这里不能因为`copyin`不用处理COW，但是它也用`walkaddr`

```cpp
int
copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
  if (dstva >= MAXVA)
    return -1;
  uint64 n, va0, pa0;
  uint flags;
  pte_t *pte;
  char *mem;
  while(len > 0){
    va0 = PGROUNDDOWN(dstva);
    pte = walkpte(pagetable, va0);
    if ((*pte & PTE_V) == 0)
      return -1;
    if ((*pte & PTE_U) == 0)
      return -1;

    pa0 = PTE2PA(*pte);
    if ((*pte & PTE_COW)) {
      // 和 usertrap 中的一样
      mem = kalloc();
      if (mem == 0)
        return -1;
      flags = PTE_FLAGS(*pte);
      flags &= ~PTE_COW;
      flags |= PTE_W;
      memmove(mem, (char *)pa0, PGSIZE);
      uvmunmap(pagetable, va0, PGSIZE, 1);
      if (mappages(pagetable, va0, PGSIZE, (uint64)mem, flags) != 0)
      {
        kfree(mem);
        return -1;
      }
      pa0 = (uint64)mem;
    }
    /* 不变 */
  }
  return 0;
}
```
