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

简单的初始化时，就是申请一个2次幂大小的内存空间，然后把最高层的空闲列表加入地址0，表示可分配。

假设现在调用malloc(12)，知道应该从第0层分配一个block出去，比如分配第0块后，返回地址0，内存会变为如下图，蓝色表示已分配，图标标记分裂，绿色表示空闲列表中的地址

![t9TwT0.png](https://s1.ax1x.com/2020/05/25/t9TwT0.png)

L0中第0块被分配，也就表示第1、2、3层的第0块都之后都不能参与分配，因为他们的第0块已经不能承诺容纳32、48、128字节的内容。

