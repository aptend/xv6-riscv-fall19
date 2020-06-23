TYCS: Step2.5

class [Operating System Engineering(MIT 6.828)](https://pdos.csail.mit.edu/6.828/2019/schedule.html)


每个lab在对应分支，记录了一些实验的思考过程，传送门:

1. [Lab util](https://github.com/aptend/xv6-riscv-fall19/tree/util)
2. [Lab sh](https://github.com/aptend/xv6-riscv-fall19/tree/sh)
3. [Lab alloc](https://github.com/aptend/xv6-riscv-fall19/tree/alloc)  
    ⭐ 有关于`buddy allocator`非常直观的图，确定不看看吗？
4. [Lab lazy](https://github.com/aptend/xv6-riscv-fall19/tree/lazy)  
    利用pte的保留位标记了guard页，所以vmprint的测试没通过，但是问题不大，这个方案我觉得也还行。
    另一种就在exec时给proc更新一个ustack字段
5. [Lab cow](https://github.com/aptend/xv6-riscv-fall19/tree/cow)  
    父进程的pte和子进程的pte都要改标记COW，用引用计数回收共享页。  
    记录了一个实现copyout前的bug的过程，比较隐蔽  
    过程：kalloc.c中的引用计数 | uvmcopy uvmunmap修改 | usertrap() | 迁移到copyout
