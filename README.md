# Lab-alloc

主要有两个任务：
1. 修改`kernel/file.c`， 创建文件时使用`buddy allocator`申请空间
2. 修改`kernel/buddy.c`，用xor运算减少记录每一层的分配和分裂信息的位向量

## Task I

原本的文件表是真正的列表，只有100的容量，也就是整个系统最多能支持打开100个文件。用refcount是否为0来表示该表项是否可用
> 文件表是全局表，项被文件描述符所指，项内容有指向inode的指针，包含offset，包含refcount  
> 每个进程持有的，是打开的文件描述符表，记录文件表项指针。


现在目的是去掉全局的文件列表，用动态分配的方式提供文件表项，在refcount为0时就回收内存。

`filealloc`时，`malloc`应该和`ref`的设置在锁中进行，保证创建中间不会插入`dup`之类的操作。

本来`fileclose`中的`ff`的作用是减少锁的作用范围。如果`ref`为0，重置必要信息后就释放锁，让当前的文件表项可以立即被重用。剩下的资源释放工作，基于复制到本地栈上的`ff`信息，慢慢清理。改动后使用动态内存分配，重用的压力没有那么大，所以不需要用`ff`复制。

## Task II

![t94mVK.png](https://s1.ax1x.com/2020/05/25/t94mVK.png)

第0层，看作把线性的内存分为固定的最小block，比如16字节，总共有8个，编号0～7  

第1层，把相同的内存看作size更大的block，也就是32字节，总共4个，编号0～3

以此类推

```cpp
#define LEAF_SIZE 16                          // 最小的块block，16字节
#define NSIZES 15                             // 有多少层，组织为15层
#define MAXSIZE (NSIZES - 1)                  // 最高层的0-based索引
#define BLK_SIZE(k) ((1L << (k)) * LEAF_SIZE) // 第k层的块大小，第0层，16字节，第1层，32字节，第2层，64字节…
#define HEAP_SIZE BLK_SIZE(MAXSIZE)                    // 整个线性内存的大小，也就是最高层(只有一个block)的大小
#define NBLK(k) (1 << (MAXSIZE - k))                   // 第k层总共有多少个块，最高层为1，次高层为2
#define ROUNDUP(n, sz) (((((n)-1) / (sz)) + 1) * (sz)) // 计算大于n的sz的倍数，求位向量的大小
```

每一层的记录结构如下，每个block是否分裂，是否已分配，由位向量记录，也就是char数组，每个char 8bit，举例roundup(23, 8)=24, 表示要记录23个block的状态，需要的是24bit

```cpp
struct sz_info {
  Bd_list free;  // 空闲列表，表示该层可供分配的地址
  char *alloc;   // 记录block是否已经分配
  char *split;   // 记录block是不是分裂，为下层的block腾空间
};
```

简单的初始化，就是申请一个2次幂大小的内存空间，然后把最高层的空闲列表加入地址0，表示可分配。

假设现在调用malloc(12)，知道应该从第0层分配一个block出去，比如分配第0块后，返回地址0，内存会变为如下图，蓝色表示已分配，图标标记分裂，绿色表示空闲列表中的地址

![t9TwT0.png](https://s1.ax1x.com/2020/05/25/t9TwT0.png)

L0中第0块被分配，也就表示第1、2、3层的第0块都之后都不能参与分配，因为他们的第0块已经不能承诺容纳32、48、128字节的内容。

逆过程，free(0)，就需要先知道这个地址是哪一层分配出去的。用到了`split`表，从最底层开始，查看**上一层**对应的block是否有分裂，如果有，就表示该地址的工作在当前层。有分裂标记，一定是已分配的，但是同时也说明当前block只是实际分配地址的过渡，不过是因为更小的`malloc`带来的"被迫分配"。

```cpp
// 根据地址起点和层数，计算该地址属于该层的几号block
int blk_index(int k, char *p) {
  int n = p - (char *) bd_base;
  return n / BLK_SIZE(k);
}

// 根据地址起点，推断当初分配出去的块大小，用层数表示
int size(char *p) {
  for (int k = 0; k < nsizes; k++) {
    if(bit_isset(bd_sizes[k+1].split, blk_index(k+1, p))) {
      return k;
    }
  }
  return 0;
}
```

从分配层开始，尝试取消分裂和分配的标记，以便支持更大block的分配，在free(0)的例子中，就是回到最初的元数据状态。取消的依据，就是看本层它的伙伴buddy是不是空闲的，如果是，就把伙伴从空闲列表中移除，继续往上层查看，如果不是，就停止取消的过程，在结束层加入把空出来的block加入到空闲列表中。

在上述过程中，alloc数组，是每个bit表示对应的block是否被分配，但是更好的办法是用一个异或的bit，表示一对block的分配状态。比如b0, b1是一对，最开始都没有分配，异或状态为0，现在b0分配出去了，状态变为1，之后b0释放，看到状态为1，就知道b1肯定是空闲的，可以继续合并。另一种情况，b0分配后，紧接着就b1分配，状态变为0，现在b0再释放，看到状态为0，就知道b1已分配，不会再向上合并。

所以需要改的地方，就是用到`alloc`的地方，`malloc`、`free`、`init`。

初始化的内存布局如下

```
      |------------------------------------------ HEAP size ----------------------------------------|
      |---------------------- meta allocated -------------------------|              |-- allocated--|
base  bd_base                                                         p          bd_end   end       |
+-----+--------------+--------------------------+---------------+-----+--------------+-----+--------+
| pad | L0 | L1 | .. | L0.alloc | L1.alloc | .. | L0.split | .. | pad | ..true mem.. | pad | unavail|
+-----+--------------+--------------------------+---------------+-----+--------------+-----+--------+
```

从图中可以看到，元数据是直接放在受管理的内存中的，所以需要把元数据段标记为allocated，另外为了把管理的内存规整为2次幂大小，也要把尾部的非受管理区域标记为allocated。这里的标记不能沿用`malloc`的逻辑，需要手段标记alloc、split，以及填充空闲列表，对应于`bd_init`中的这三句话

```cpp
// 标记元数据
// done allocating; mark the memory range [base, p) as allocated, so
// that buddy will not hand out that memory.
int meta = bd_mark_data_structures(p);

// 标记尾部区域
// mark the unavailable memory range [end, HEAP_SIZE) as allocated,
// so that buddy will not hand out that memory.
int unavailable = bd_mark_unavailable(end, p);

// 填充空闲列表
// initialize free lists for each size k
void *bd_end = bd_base+BLK_SIZE(MAXSIZE)-unavailable;
int free = bd_initfree(p, bd_end);
```

在填充空闲列表时，需要明确地知道哪个块为空闲，单单异或信息不足以给出判断。但是可以知道，元信息的分配是从左到右，所以如果出现buddy为空闲，那么一定是右手块(奇数编号)加入空闲列表，同理，尾部分配，出现空闲一定是左手块(偶数编号)加入空闲列表。`bd_initfree()`调用`bd_initfree_pair()`, 所以把这个方向信息传入`bd_init_free_pair()`就可以了
```cpp
int
bd_initfree_pair(int k, int bi, int direction) {
  int buddy = (bi % 2 == 0) ? bi+1 : bi-1;
  int free = 0;
  if(bit_isset(bd_sizes[k].alloc, bi >> 1)) {
    free = BLK_SIZE(k);
    // direction = 0, put right-hand block on free list
    // direction = 1, put left-hand block
    if(bi % 2 == direction) 
      lst_push(&bd_sizes[k].free, addr(k, buddy));   // put buddy on free list
    else
      lst_push(&bd_sizes[k].free, addr(k, bi));      // put bi on free list
  }
  return free;
}
```

# 总结

第一次见的操作：
1. 往受管区域加入元数据，线性铺开。
2. 利用自洽的基础逻辑(allocated)，从非二次幂规整到要求的二次幂，这种化归感觉很妙。
3. 学会buddy_alloc了么？还是没有，细节是魔鬼。