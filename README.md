
## Warmup

先做cs61c还是正确的，回答起来就很简单

## Thread switch

先看xv6的实现，再做题。

### 内核切换

`switch.S`中就是函数`swtch(old, new)`的指令。**每次调用时，就会把当前的返回地址放到`old->ra`，这是切换的关键**，剩下的，就是`sp、s0~s11`的callee保存寄存器，也保存到context。

> caller保存，跳转之后，callee可以随便读取覆盖
>
> callee保存，跳转之后，callee可以用，但是返回前必须恢复callee最初看到的样子
>
> 因为调用swtch前，C代码编译后的指令，已经将caller寄存器保存到自己的栈上。所以只主动保存callee寄存器，完成调用RISC-V的调用约定，之后重新调度当前进程时，相当于从swtch返回的时候，callee寄存器值不变。

```
swtch:
    sd ra, 0(a0)
    sd sp, 8(a0)
    sd s0, 16(a0)
    ...
    sd s11, 104(a0)

    ld ra, 0(a1)
    ld sp, 8(a1)
    ld s0, 16(a1)
    ...
    ld s11, 104(a1)

    ret
```

 恢复Context new的寄存器后，调用ret，将开始执行某个`swtch`后面的语句。



### 调度器和进程

使用场景上，`sleep`，`yield`， `exit`都会确保获取进程锁后，调用`sched()`，从而调用到`swtch(&p->context, &mycpu()->sheduler)`，返回到scheduler的for循环中，查找到下一个可供调度(`RUNNABLE`)的进程，`swtch(&mycpu()->scheduler, &p->context)`，切换到那个进程上。

调度就是在`scheduler`中的`swtch`，和`sched`中的`swtch `之间反复横跳。并且这中间对锁的有值得注意的合作关系：

1. 在当前工作线程为`p`，先获取锁`aquire(&p.lock)`,
2. 再调用`sched.swtch(p_ctx, scheduler_ctx)` 
3. 来到`scheduler`的`for`循环中，继续执行`scheduler.swtch(scheduler_ctx, p_ctx) `之后的语句，即`c->proc = 0;`，然后`release(&p.lock)`释放

可以看到，这里的锁是由工作线程上锁，但是由scheduler线程释放。

如果只有一个进程可用，上述过程由时钟中断，调用`yield`触发。接第3步，内层for循环不能找到可调度的进程，由外层循环再发起扫描，扫描到唯一的`RUNNABLE`进程`p`

4. 获取锁`acquire(&p.lock)`，设置状态为`RUNNING`
5. 调用`swtch(scheduler_ctx, p_ctx)`
6. 回到`sched()`中的`swtch`，最后返回到`yield`
7. `release(&p.lock)`

所以进程切换前后，由两个不同的线程合作，共同完成获取和释放动作，保证正确性。这种合作的线程，也可以称为`协程`。

大部分时间，都是`scheduler`和`sched`打乒乓球，由调用`sched`的路径返回用户态。但是对于fork出来的新进程，被`scheduler`选中后，没有`sched`可用，所以在`allocproc`函数中，会先`p->context.ra = (uint64)forkret`，子进程被调度后，就会由`forkret`函数来释放进程锁，并且返回到用户态(`usertrapret`)



```c
void yield(void)
{
  struct proc *p = myproc();
  acquire(&p->lock);    // 获取进程锁
  p->state = RUNNABLE;  // 设置为RUNNABLE
  sched();              // 调用sched，等待下次调度，从这里继续
  release(&p->lock);    // 释放进程锁
}

void sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&p->lock))     // 切换前，确定获取了进程锁
    panic("sched p->lock");
  if(mycpu()->noff != 1)     // 确定该CPU只有一个锁(进程锁)，有其他锁就可能死锁
    panic("sched locks");
  if(p->state == RUNNING)    // 确定切换前，状态已改
    panic("sched running");
  if(intr_get())             // 确定中断关闭(和noff冗余)
    panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&p->context, &mycpu()->scheduler);
  mycpu()->intena = intena;
}


void scheduler(void)
{
  for(;;) {
    for(p = proc; p < &proc[NPROC]; p++) {
      acquire(&p->lock);
      if(p->state == RUNNABLE) {
        // 切换到该进程执行
        // 进程自己负责释放进程锁
        // 等到进程自己执行够了，主动sleep或者响应时间中断yield
        // 重新获取好进程锁，然后swtch到回到这里，执行c->proc = 0;
        p->state = RUNNING;
        c->proc = p;
        swtch(&c->scheduler, &p->context);
    
        c->proc = 0;
      }
      release(&p->lock);
    } 
  }  
}
```



按照教材思考锁的思路，`p->lock`在维持调度的不变性：

1. 如果一个进程是`RUNNING`，那么CPU的通用寄存器必须是该进程所需要的值，并且`mycpu()->proc`必须指向该进程，这样的状态下，才能用`yield`将其切走。
2. 如果一个进程是`RUNNABLE`，那么CPU的通用寄存器不包含该进程所需的值，他们存在`p->context`中，并且没有CPU在内核栈上为该进程执行，也没有`mycpu()->proc`指向该进程，这样的状态下，一个CPU才能装载该进程开始执行。

当持有进程锁时，一般上述不变性都是被破坏的。在`yield`切换时，状态已改为`RUNNABLE`，但是寄存器还是当前进程的值，并且`mycpu()->proc`也是当前进程，所以要用`switch.S`切换寄存器内容，然后在scheduler中设置`c->proc = 0`后才能释放进程锁。

### 用户线程切换

完全仿照内核的进程切换，基本上没有改动。而且因为属于协程，不用加锁，难度降低。

注意create时的初始化，栈地址是从大向小增长的，初始化语句长这样。

```cpp
memset(&t->context, 0, sizeof(t->context));
t->context.ra = (uint64)func;
t->context.sp = (uint64)(&t->stack) + STACK_SIZE;
```

这里相当于已经写了一个简单的协程运行时。

## Alarm

把入口实现放在`sysproc.c/sys_sigalarm`和`sysproc.c/sys_sigreturn`

给proc增加一些字段
```cpp
void (* cb_handler)();          // handler in sigalarm
int cb_ticks;                   // ticks spent on CPU
int cb_interval;                // interval to call handler
int cb_running;                 // handler is running
struct trapframe cb_snapshot;   // trapframe to restore user code
```

只在`usertrap`中响应timer的中断，只有这才表明用户代码执行。保存中断现场，然后设置handler的地址，返回用户态

```cpp
// give up the CPU if this is a timer interrupt.
if(which_dev == 2) {
  // alarm enable && inc ticks && handler is not running
  if (p->cb_interval > 0 && p->cb_ticks++ >= p->cb_interval && !p->cb_running)
  {
    p->cb_snapshot = *p->tf; // store trapframe to restore in sigreturn
    p->cb_running = 1;
    p->tf->epc = (uint64)p->cb_handler;
  }
  yield();
}
```

返回的时候恢复现场，`cb_ticks`用减法而不是置0，可以不丢失handler运行过程中产生的执行计划。

```c
uint64 sys_sigreturn(void) {
  struct proc *p = myproc();
  p->cb_ticks -= p->cb_interval;
  p->cb_running = 0;
  *p->tf = p->cb_snapshot; // restore all registers
  return 0;
}
```
