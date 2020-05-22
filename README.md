# Lab-sh


先从`testnsh.sh`中收集一些命令
- `echo hello goodbye`
- `grep constitute README`
- `echo xxdata > xxfile`
- `cat < xxfile`
- `grep pointers < README > testsh.out`
- `cat xxfile | cat`
- `grep suggestions < README | wc > testsh.out`

测试用的命令都不复杂，`|`、`>` 和 `<` 操作符定义行为像二元运算符。

大概的流程：

1. 从`stdin`拿到输入行，可以用`gets`
2. 按空格分为`token`，把行处理成命令的结构体`cmd_t {命令本身，参数，in_fd, out_fd}`，其中`[in|out]_fd`默认为-1，表示没有任何重定向
3. 用`current_cmd`表示当前正在构建的命令
4. 遇到`<`，打开文件，设置`in_fd`；遇到`>`，打开文件，设置`out_fd`
5. 遇到`|`，建立一个pipe，设置`out_fd`，运行`current_cmd`，等待并关闭资源，之后重置`current_cmd`，设置`in_fd`
6. `\n`，执行最后一个`current_cmd`，等待进程结束，关闭所有打开文件和管道。



和Lab-util有区别，`exit`和`wait`带参数了，`wait(0) = wait(NULL) = waitpid(-1, NULL, 0)`