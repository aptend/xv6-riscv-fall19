## Task I: 更大的文件

![image-20200823192247439](https://i.loli.net/2020/08/23/5P8m4HDkfjdlnIJ.png)

当前inode的addrs共12个直接地址，1间接地址，需要修改为11个直接地址，1个一级间接地址，
1个二级间接地址。

实现很直接。

## Task II: 符号连接

hard link的实际是目录中的entry，增加的是另一个inode的nlink，缺点是：
1. old必须是文件，不能是目录
2. new 和 old 必须在同一个文件系统中，否则i-number没有意义

soft link的实际上一个单独的文件，文件内容是target代表的path name，所以没有目录限制，也没有文件系统的限制。

实现很简单，主要是对inode组件的使用，总结如下
- `iget()`: 增加inode的ref。出现在`ialloc`和`namex`中
- `iput()`: 减少inode的ref。和`iget`成对出现。减少到0会尝试删除`dinode`，当然会要求`nlink = 0`
- `ilock()`: 读写开始前加锁，保证从磁盘中加载了`dinode`的信息
- `iunlock()`: 读写结束释放锁，和`ilock`成对出现
- `iupdate()`: 对`dinode`中的字段做了修改，需要把更新从内存`inode`刷入磁盘`dinode`
