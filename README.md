# Lab-Util

## sleep

对着`ln`的格式抄就可以了

## pingpong

pipe的基础用法

## primes

伪代码
```pl
p = get a number from left neighbor
print p
loop:
    n = get a number from left neighbor
    if (p does not divide n)
        send n to right neighbor
```

每个进程负责打印一个质数，并且筛除该质数的倍数。无法筛除的，就往右传递。现存的每个进程都处理不了，是一个新的质数，需要创建新的进程和pipe。

- 原来write和read可以直接写int哦，中间的buf参数类型是`void *`
- 注意使用wait回收子进程，不然下一次执行primes可能会失败，猜是因为耗尽了进程号的资源，还没验证。

## find

读目录没有`readdir`，而是直接用`struct dirent`结构从目录的block里循环读

目录中的文件名结构是`char name[DIRSIZ];`，定义在`fs.h`中，`DIRSIZ`为14，所以文件名最长就为14

## xargs

读行的函数还是在`grep.c`中，可以直接使用

`exec(char *path, char **argv)` 其中参数个数不能超过`MAXARGS`， 10个。`argv[0]`按照约定，是子进程的二进制文件名。

实现时就直接把`stdin`的输入行当作一个参数追加到`argv`没有按空格分割。