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
