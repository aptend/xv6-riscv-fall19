TYCS: Step2.5

class [Operating System Engineering(MIT 6.828)](https://pdos.csail.mit.edu/6.828/2019/schedule.html)


每个lab在对应分支，记录了一些实验的思考过程，传送门:

1. [Lab util](https://github.com/aptend/xv6-riscv-fall19/tree/util)
2. [Lab sh](https://github.com/aptend/xv6-riscv-fall19/tree/sh)
3. [Lab alloc](https://github.com/aptend/xv6-riscv-fall19/tree/alloc)  
    ⭐ 有关于`buddy allocator`非常直观的图
4. [Lab lazy](https://github.com/aptend/xv6-riscv-fall19/tree/lazy)  
    利用pte的保留位标记了guard页，所以vmprint的测试没通过，但是问题不大，这个方案我觉得也还行。
    另一种就在exec时给proc更新一个ustack字段
5. [Lab cow](https://github.com/aptend/xv6-riscv-fall19/tree/cow)  
    父进程的pte和子进程的pte都要改标记COW，用引用计数回收共享页。  
    记录了一个实现copyout前的bug的过程，比较隐蔽  
    过程：kalloc.c中的引用计数 | uvmcopy uvmunmap修改 | usertrap() | 迁移到copyout
6. [Lab syscall](https://github.com/aptend/xv6-riscv-fall19/tree/syscall)  
    终于明白切换是怎么回事了。  
    看懂了内核的进程调度，这期的lab还比较简单
7. [Lab lock](https://github.com/aptend/xv6-riscv-fall19/tree/lock)  
    捋了一遍粗略开机过程，`freerange`时不用加锁  
    最初理解的`stealing`很麻烦，但是用`proxying`的角度来实现就简单很多了

8. [Lab fs](https://github.com/aptend/xv6-riscv-fall19/tree/fs)  
    底层实现比较绕，特别是锁的应用，但是lab本身用的知识不多，比较容易。  
    Lab中没有logging的内容有点可惜。

9. [Lab mmap](https://github.com/aptend/xv6-riscv-fall19/tree/mmap)  
    和需要看的论文关系不大，主要还是参考之前的`Lab lazy`。最主要收获是对Linux虚拟内存
    分段的作用有实感了。

10. [Lab net](https://github.com/aptend/xv6-riscv-fall19/tree/net)  
    只做基础Lab的话，论文可以先放。因为要看E1000的手册，有点麻烦，还好圈定了范围，
    也有代码帮忙定位。主要收获网卡驱动如何交换数据ring desc buffer，协议头部的拆装mbuf，
    socket的读写实现
