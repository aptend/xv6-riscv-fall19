根据Hints的基本路线：
  1. 先修改`fork`时调用的`uvmcopy`，避免复制物理内存，直接把父进程的物理内存映射到子进程的页表上，并且两方的页表都清除写入权限。
  2. 修改`usertrap`，识别因为COW造成的写入失败，复制该页到新的内存，并重新开启写入权限
  3. `kalloc.c`加入引用计数，最后一个引用消失时，`kfree()`才释放该页内存
  4. 修改`copyout`，和`usertrap`类似的方式处理COW的错误


但是实际做的时候，需要整体考虑，第3步kalloc中删除的反而应该先考虑，123都完成，才能一个可通过的测试


## `uvmcopy`

用pte的第8-bit作为cow页的标记
`#define PTE_COW (1L << 8)`


## `trap`

这次因为没有lazy alloc，所以只用考虑入的页错误，所以只处理错误号为15的`Store Page Fault`

`r_stval`只提供了出错的虚拟地址，但是需要pte来判断COW，并获得PA，所以需要一个`ptewalk`，本质调用`walk(pg, va, 0)`


