# The xv6 code



## NIC driver

### mbuf

mbuf是处理数据包的关键的工具，快速添加/剥离/替换各层的header

内存布局如下，buffer 2048，headroom 128

```shell
            <- push            <- trim
            -> pull            -> put
[-headroom-][------buffer------][-tailroom-]
|----------------MBUF_SIZE-----------------|
```

```cpp
struct mbuf {
  struct mbuf  *next; // the next mbuf in the chain
  char         *head; // the current start position of the buffer
  unsigned int len;   // the length of the buffer
  char         buf[MBUF_SIZE]; // the backing store
}

// 删除头部数据，返回删除部分的指针
// |     ooooooo      |
// |        oooo      |
//       |
//      ret
char *mbufpull(struct mbuf *m, unsigned int len)
{
  char *tmp = m->head;
  if (m->len < len)
    return 0;
  m->len -= len;
  m->head += len;
  return tmp;
}

// 向头部添加数据，返回添加部分的指针
// |     ooooooo      |
// |   nnooooooo      |
//     |
//    ret
char* mbufpush(struct mbuf *m, unsigned int len)
{
  m->head -= len;
  if (m->head < m->buf)
    panic("mbufpush");
  m->len += len;
  return m->head;
}

// 向尾部添加数据，返回添加部分的指针
// |     ooooooo      |
// |     ooooooonnn   |
//              |
//             ret
char * mbufput(struct mbuf *m, unsigned int len)
{
  char *tmp = m->head + m->len;
  m->len += len;
  if (m->len > MBUF_SIZE)
    panic("mbufput");
  return tmp;
}

// 从尾部删除一部分数据，返回删除部分的指针 
// |     ooooooo      |
// |     ooooo        |
//            |   
//           ret
char * mbuftrim(struct mbuf *m, unsigned int len)
{
  if (m->len < len)
    return 0;
  m->len -= len;
  return m->head + m->len;
}


// 分配出一个mbuffer
struct mbuf *
mbufalloc(unsigned int headroom)
{
  struct mbuf *m;
 
  if (headroom > MBUF_SIZE)
    return 0;
  m = kalloc();  // 申请4K内存直接当作mbuf使用，按照内存排布，此时m->buf已经自动填好了地址，应该是 m+20
  if (m == 0)
    return 0;
  m->next = 0;
  m->head = (char *)m->buf + headroom; // m+20+128
  m->len = 0;
  memset(m->buf, 0, sizeof(m->buf));
  return m;
}

void
mbuffree(struct mbuf *m)
{
  kfree(m);
}
```



多个mbuf，使用链表组织

```cpp
// 其实队列就一个脚手架，指针还是藏在mbuf内部，这种脚手架的模式在re2的教程中也出现
struct mbufq {
  struct mbuf *head;  // the first element in the queue
  struct mbuf *tail;  // the last element in the queue
}


// 把mbuf加入到队列尾端
void mbufq_pushtail(struct mbufq *q, struct mbuf *m)
{
  m->next = 0;
  if (!q->head){
    q->head = q->tail = m;
    return;
  }
  q->tail->next = m;
  q->tail = m;
}

// 弹出头部mbuf
struct mbuf *
mbufq_pophead(struct mbufq *q)
{
  struct mbuf *head = q->head;
  if (!head)
    return 0;
  q->head = head->next;
  return head;
}

int mbufq_empty(struct mbufq *q) {  return q->head == 0; }
void mbufq_init(struct mbufq *q) {  q->head = 0; }
```



### Proto headers

IP Header的实际内容

![ipheader](https://i.loli.net/2020/08/30/7p6lvJSAdTP1F5f.png)



```cpp
#define ETHADDR_LEN 6
struct eth {
  uint8  dhost[ETHADDR_LEN]; // 48-bit的MAC地址
  uint8  shost[ETHADDR_LEN];
  uint16 type;               // 类型 IP 或者 ARP
}

struct ip {
  uint8  ip_vhl; // version << 4 | header length >> 2 = (4 << 4 | 20 >> 2)
  uint8  ip_tos; // type of service 区分服务
  uint16 ip_len; // total length 
  uint16 ip_id;  // identification 分片表示，不支持
  uint16 ip_off; // 分片偏移，实际当前不支持分片，固定为0
  uint8  ip_ttl; // time to live
  uint8  ip_p;   // payload的类型 ICMP TCP UDP
  uint16 ip_sum; // checksum
  uint32 ip_src, ip_dst;
};

struct udp {
  uint16 sport; // source port
  uint16 dport; // destination port
  uint16 ulen;  // length, including udp header, not including IP header; 没啥用，header固定，都可以推算
  uint16 sum;   // checksum
};

struct arp {
  uint16 hrd; // 硬件地址的格式，默认是MAC 0x0001表示
  uint16 pro; // 协议地址的格式，默认是IP  0x0800表示
  uint8  hln; // 默认6，48bit MAX
  uint8  pln; // 默认4，32bit IP
  // 上面4个在本lab就是校验用
  uint16 op;  // Request or Reply

  char   sha[ETHADDR_LEN]; // sender hardware address
  uint32 sip;              // sender IP address
  char   tha[ETHADDR_LEN]; // target hardware address
  uint32 tip;              // target IP address
};


// 主要在nettest.c中使用，发送接受dns消息，并且解析
// 冒号是bitfield语法，rd访问还是用dns.rd，但是值只会是0/1，赋值也只接受0/1，在内存里占1 bit
struct dns {
  uint16 id;  // request ID

  uint8 rd: 1;  // recursion desired
  uint8 tc: 1;  // truncated
  uint8 aa: 1;  // authoritive
  uint8 opcode: 4; 
  uint8 qr: 1;  // query/response
  uint8 rcode: 4; // response code
  uint8 cd: 1;  // checking disabled
  uint8 ad: 1;  // authenticated data
  uint8 z:  1;  
  uint8 ra: 1;  // recursion available
  
  uint16 qdcount; // number of question entries
  uint16 ancount; // number of resource records in answer section
  uint16 nscount; // number of NS resource records in authority section
  uint16 arcount; // number of resource records in additional records
} __attribute__((packed));


struct dns_data {
  uint16 type;
  uint16 class;
  uint32 ttl;
  uint16 len;
} __attribute__((packed));
```



### parse

接下来就是接受发送数据包的代码，实际上都挺模板的



```cpp
// 被e1000的驱动的中断处理程序调用，把收到的buffer送到协议栈中
void net_rx(struct mbuf *m)
{
  struct eth *ethhdr;
  uint16 type;

  ethhdr = mbufpullhdr(m, *ethhdr); // 去掉以太帧header
  if (!ethhdr) {
    mbuffree(m);
    return;
  }

  type = ntohs(ethhdr->type);       // 根据类型分发
  if (type == ETHTYPE_IP)
    net_rx_ip(m);
  else if (type == ETHTYPE_ARP)
    net_rx_arp(m);
  else
    mbuffree(m);
}

// 调用e1000驱动，向网卡发送数据包
static void net_tx_eth(struct mbuf *m, uint16 ethtype)
{
  struct eth *ethhdr;

  ethhdr = mbufpushhdr(m, *ethhdr); // 添加以太帧的header
  memmove(ethhdr->shost, local_mac, ETHADDR_LEN);
  // 实际的协议栈，目标mac实际需要先使用arp来探测，但是我们还有实现完整的arp(我们只支持被动响应)，
  // 所以这里做广播来间简化
  // TODO，这里不可以发现type是ARP而从ARP包中得到目标mac吗？
  memmove(ethhdr->dhost, broadcast_mac, ETHADDR_LEN);
  ethhdr->type = htons(ethtype);
  if (e1000_transmit(m)) {
    mbuffree(m);
  }
}

```



#### ARP

读内容 - 校验 - 对比 - 响应/drop

```cpp
static void net_rx_arp(struct mbuf *m)
{
  struct arp *arphdr;
  uint8 smac[ETHADDR_LEN]; // sender‘s mac
  uint32 sip, tip;

  // 读出Arp的内容，此时buf基本上没有内容了
  arphdr = mbufpullhdr(m, *arphdr); 
  if (!arphdr)
    goto done;

  // 验证arp内容
  if (ntohs(arphdr->hrd) != ARP_HRD_ETHER ||
      ntohs(arphdr->pro) != ETHTYPE_IP ||
      arphdr->hln != ETHADDR_LEN ||
      arphdr->pln != sizeof(uint32)) {
    goto done;
  }

  // 目前只支持响应，所以取出target ip，请求的是不是自己
  tip = ntohl(arphdr->tip); // target IP address
  if (ntohs(arphdr->op) != ARP_OP_REQUEST || tip != local_ip)
    goto done;

  // 确定响应
  memmove(smac, arphdr->sha, ETHADDR_LEN);
  sip = ntohl(arphdr->sip); // sender是qemu模拟的SLiRP
  net_tx_arp(ARP_OP_REPLY, smac, sip);

done:
  mbuffree(m);
}

static int net_tx_arp(uint16 op, uint8 dmac[ETHADDR_LEN], uint32 dip)
{
  struct mbuf *m;
  struct arp *arphdr;

  m = mbufalloc(MBUF_DEFAULT_HEADROOM); // 申请一个新的mbuf
  if (!m)
    return -1;

  // 向buf后端增加内容
  arphdr = mbufputhdr(m, *arphdr);
  // 元数据
  arphdr->hrd = htons(ARP_HRD_ETHER);
  arphdr->pro = htons(ETHTYPE_IP);
  arphdr->hln = ETHADDR_LEN;
  arphdr->pln = sizeof(uint32);
  arphdr->op = htons(op);

  // 实际地址内容
  memmove(arphdr->sha, local_mac, ETHADDR_LEN);
  arphdr->sip = htonl(local_ip);
  memmove(arphdr->tha, dmac, ETHADDR_LEN);
  arphdr->tip = htonl(dip);

  // header is ready, send the packet
  net_tx_eth(m, ETHTYPE_ARP);
  return 0;
}
```





#### IP

```cpp
static void net_rx_ip(struct mbuf *m)
{
  struct ip *iphdr;
  uint16 len;

  iphdr = mbufpullhdr(m, *iphdr);
  if (!iphdr)
	  goto fail;

  // 校验版本ipv4
  if (iphdr->ip_vhl != ((4 << 4) | (20 >> 2)))
    goto fail;
  // validate IP checksum
  if (in_cksum((unsigned char *)iphdr, sizeof(*iphdr)))
    goto fail;
  // 不支持分片
  if (htons(iphdr->ip_off) != 0)
    goto fail;
  // 确定该数据包确实是发给自己的
  if (htonl(iphdr->ip_dst) != local_ip)
    goto fail;
  // 支持UDP
  if (iphdr->ip_p != IPPROTO_UDP)
    goto fail;

  len = ntohs(iphdr->ip_len) - sizeof(*iphdr); // udp+数据的长度
  net_rx_udp(m, len, iphdr);
  return;

fail:
  mbuffree(m);
}


static void net_tx_ip(struct mbuf *m, uint8 proto, uint32 dip)
{
  struct ip *iphdr;

  // 新增header
  iphdr = mbufpushhdr(m, *iphdr);
  memset(iphdr, 0, sizeof(*iphdr));
  iphdr->ip_vhl = (4 << 4) | (20 >> 2);
  iphdr->ip_p = proto;
  iphdr->ip_src = htonl(local_ip);
  iphdr->ip_dst = htonl(dip);
  iphdr->ip_len = htons(m->len);
  iphdr->ip_ttl = 100;
  iphdr->ip_sum = in_cksum((unsigned char *)iphdr, sizeof(*iphdr));

  // now on to the ethernet layer
  net_tx_eth(m, ETHTYPE_IP);
}
```



#### UDP

其中发送`sockrecvudp`是自己需要实现的函数。

```cpp
static void net_rx_udp(struct mbuf *m, uint16 len, struct ip *iphdr)
{
  struct udp *udphdr;
  uint32 sip;
  uint16 sport, dport;


  udphdr = mbufpullhdr(m, *udphdr);
  if (!udphdr)
    goto fail;

  // TODO: validate UDP checksum

  // 之前说没用，人家可以校验长度，但是实现了checksum之后，那岂不是真没用了
  if (ntohs(udphdr->ulen) != len)
    goto fail;
  len -= sizeof(*udphdr); // 数据长度
  if (len > m->len) // 一个buf可以完全hold住一个udp包的数据
    goto fail;
  // 然后把buf调整为实际数据的长度，shrink一下
  mbuftrim(m, m->len - len);

  // parse the necessary fields
  sip = ntohl(iphdr->ip_src);
  sport = ntohs(udphdr->sport);
  dport = ntohs(udphdr->dport);
  // 放入协议栈
  sockrecvudp(m, sip, dport, sport);
  return;

fail:
  mbuffree(m);
}


void net_tx_udp(struct mbuf *m, uint32 dip, uint16 sport, uint16 dport)
{
  struct udp *udphdr;
  // 写入UDP头部
  udphdr = mbufpushhdr(m, *udphdr);
  udphdr->sport = htons(sport);
  udphdr->dport = htons(dport);
  udphdr->ulen = htons(m->len);
  udphdr->sum = 0; // 未提供udp的校验
  net_tx_ip(m, IPPROTO_UDP, dip);
}
```



### E1000

`main.c`增加了两个调用`pci_init`、`sockinit`





#### 3.2 Packet Reception

主体步骤：

1. 识别出wire上的packet存在
2. 地址过滤
3. 内部FIFO储存，64K
4. 转写到host memory buffer
5. 更新接收描述符状态



地址过滤Tips：

1. 地址过滤可以按`unicast / multicast`做过滤
2. 默认丢弃校验出错的包，可以通过设置控制位保留，并提示错误`rx_desc->errors`
3. ARP可由硬件控制器响应，而不经过host memory



初始化的寄存器选项：

- `RA (Receive Address)`, 用来过滤数据包的MAC，48bit，用两个32bit的寄存器保存，共16个u64来做对比：

  - `RAL`: MAC的低16位
  - `RAH`：
    - AV[31]，是否valid，置1才对比
    - Reserved[30:18]
    - AS[17:16] Address Select，source 01 destination 00
    - RAH[15:0]，高16位

  初始化时，lab使用该第一项，只接受`qemu`模拟的MAC地址

- `MTA (Multicast Table Array)`，16个完美过滤用光后，使用这个不完美的过滤器，用4096bit，也就是128个u32，大概过滤4096个相似的MAC地址，也就是每个bit过滤一个。计算过程如下：

  1. 检查`RCTL`中MO的设置，也就是使用MAC的哪几位参与计算。默认00，使用[47:36]
  2. 将12bit取出，作为一个u32的低12位，`12:34:56:78:9A:BC -> 0BC9 `
  3. u32的[11:5]的7个bit，是MTA u32数组的下表，定位到一个u32
  4. u32的[4:0]的5个bit，确定到u32中的特定bit，1就通过，0就drop

  lab中全部设置为0，不使用这个MTA

- `ICR (Interrupt Cause Read)`：当某种中断发生时，该寄存器中对应bit会被写为1，只读，每次读取时(`e1000_intr`中发生)，会清除该寄存器，之后的中断才会继续

- `ICR (Interrupt Mask Set)`：设置是否开启某种中断，对应为1的开启。lab开启的时7号，`RXT0: Receiver Timer Interrupt `





RX描述符

格式：

![image-20200831132414281](https://i.loli.net/2020/08/31/RmO9sNwcaxjdYku.png)



```cpp
struct rx_desc
{
  uint64 addr;       /* Address of the descriptor's data buffer */
  uint16 length;     /* Length of data DMAed into data buffer */
  uint16 csum;       /* Packet checksum */
  uint8 status;      /* Descriptor status */
  uint8 errors;      /* Descriptor Errors */
  uint16 special;
};
```

`csum`是16bit补码，可能需要经过整理才能传递给上层。

当NIC收到packet时，硬件把数据储存到指定的buffer，并且写入字段中的其他信息。`length`包含数据和可能存在的`CRC bytes`，lab中已经让NIC删除了CRC。OS可能需要读取多个描述符才能获取一个横跨多个buffer的packet，这种packet只会在第一个描述符中写入`csum`

主要的`status`:

- `DD：Descriptor Done`，当`DD`和`EOP`同时设置，说明已经已经把内容放到主存
- `EOP：End of Packet`，是否是packet的最后一个描述符



收取用的Ring Buffer

`Head~Tail`中间是软件给硬件提供的，可用于写数据的描述符。硬件利用Head描述符写入数据后，将Head指针前移，如果Head等于Tail，表示软件现在处理不过来了，硬件停止写入，等待软件准备更多的描述符，并且移动Tail。Ring Buffer中的其他描述符，就是属于软件，等待处理的。



RX的寄存器：

- `Receive Descriptor Base Address registers (RDBAL and RDBAH)`，Ring Buffer的基地址，16-byte对齐
- `Receive Descriptor Length register (RDLEN)  `，Ring Buffer分配内存，byte的个数，必须是128的倍数。因为`rx_desc`是16bytes，所以Ring Buffer的容量必须是8的倍数
- `Receive Descriptor Head register (RDH)  `  Head`rx_desc`的数组索引
- `Receive Descriptor Tail register (RDT)  `  Tail`rx_desc`的数组索引

- `RCTL (Receive Control)`：设置接收器的属性，lab使用到的如下：

  - `EN`：开启接收器
  - `BAM`：接收Broadcast包
  - `BSIZE`：2-bit[17:16], 00表示2048，正好是`mbuf`中的设置
  - `SECRC`：删除数据包的尾部CRC数据，不写入主存

- `RDTR (Receive Delay Time Reg)`：设置接收中断通知延时，设置有助于增大一次中断处理的buffer数量
  
  - `Delay Timer`：16bit[15:0]，1.024us的递增，推荐不要使用，有需要用`ITR`代替。因此设置为0，立即通知
- `RADV (Receive Interrupt Absolute Delay Timer)`：
  
  - `Delay Timer`：16bit[15:0]，1.024us的递增，推荐不要使用，有需要用`ITR`代替。因此设置为0，关闭该功能

- 初始化Rx Ring Buffer相关寄存器

  ```cpp
  // 16个rx_desc初始化为0，
  // 这里相当于把status清0了，硬件可以使用
  memset(rx_ring, 0, sizeof(rx_ring));
  for (i = 0; i < RX_RING_SIZE; i++) {
      // 读入用的mbuf，只会剥离header，所以不需要headroom
      rx_mbufs[i] = mbufalloc(0);
      if (!rx_mbufs[i])
          panic("e1000");
      // 写好16个描述符的地址
      rx_ring[i].addr = (uint64) rx_mbufs[i]->head;
  }
  // 记录基地址
  regs[E1000_RDBAL] = (uint64) rx_ring;
  if(sizeof(rx_ring) % 128 != 0)
      panic("e1000");
  // ring的其他参数
  regs[E1000_RDH] = 0;
  regs[E1000_RDT] = RX_RING_SIZE - 1;
  regs[E1000_RDLEN] = sizeof(rx_ring);
  ```



#### 3.3 Packet Transmit

格式

```cpp
struct tx_desc
{
  uint64 addr;    // 地址
  uint16 length;  // buffer长度
  uint8 cso;      // check sum offset
  uint8 cmd;      
  uint8 status;
  uint8 css;      // check sum start
  uint16 special;
};
```

主要的`status`:

- `DD[0]: Descriptor Done`，只有在`cmd::RS`设置时，硬件在发送完buffer后，会将DD置为1

主要的`cmd`:

- `EOP[0]: End of Packet`，当设置时，说明说明该描述符时packet的结束，lab中每个buffer都是packet，所以每次都设置
- `RS[3]: Report Status`，当设置时，让硬件适时设置`status::DD`



主要的其他设置：

- `Tx Ring Buffer`的设置和Rx类似

- `TCTL: (Transmit Control)`：设置发送器的属性，lab使用到的如下，来自[14.5]：
  - `EN`：开启发送器
  - `PSP - Padding Short Packet`：将packet填充到64bytes，否则最小32bytes
  - `CT - Collision Threshold`：CSMA/CD相关，冲突后的重复次数，sheet推荐15，lab使用16
  - `COLD - Collision Threshold`：CSMA/CD相关，推荐值64
- `TIPG: (Inner Packet Gap)`：按照[14.5]设置

- 初始化Tx Ring Buffer相关寄存器

    ```cpp
    memset(tx_ring, 0, sizeof(tx_ring));
    for (i = 0; i < TX_RING_SIZE; i++) {
        // 每个描述符的stats都为非0，表示属于软件
        // 软件可以填充准备发送的buffer
        tx_ring[i].status = E1000_TXD_STAT_DD;
        tx_mbufs[i] = 0;
    }
    regs[E1000_TDBAL] = (uint64) tx_ring;
    if(sizeof(tx_ring) % 128 != 0)
        panic("e1000");
    regs[E1000_TDLEN] = sizeof(tx_ring);
    // 初始化时，没有属于硬件，可以发送的描述符
    regs[E1000_TDH] = regs[E1000_TDT] = 0;
    ```
    





## Socket

socket是操作系统对网络的抽象，和文件类似，和底层的Ring Buffer相关联：

- 从socket读，收取数据包，没有数据包时阻塞
- 向socket写，发送数据包



总体要参考[[xv6-pipes.md]]



### struct sock

和`struct pipe / inode`一样，作为file外衣的里子。

```cpp
struct sock {
  struct sock *next; // socket链表指针
  uint32 raddr;      // the remote IPv4 address
  uint16 lport;      // the local UDP port number
  uint16 rport;      // the remote UDP port number
  struct spinlock lock; // 保护rxq
  struct mbufq rxq;     // 已经经过udp接收，等待用户态读取，rx buffer queue
};

static struct spinlock lock; // 保护sockets链表
static struct sock *sockets; // socket链表
```



### sockalloc

申请内存：外file，内sock

初始化两个对象，把sock组合进file

将sock加入链表，如果有重复就panic



`sys_connect`系统调用就简单地使用`sockalloc`，创建一个`fd`给用户空间。

```cpp
int sockalloc(struct file **f, uint32 raddr, uint16 lport, uint16 rport)
{
  struct sock *si, *pos;

  si = 0;
  *f = 0;
  if ((*f = filealloc()) == 0)
    goto bad;
  if ((si = (struct sock*)kalloc()) == 0)
    goto bad;

  // initialize objects
  si->raddr = raddr;
  si->lport = lport;
  si->rport = rport;
  initlock(&si->lock, "sock");
  mbufq_init(&si->rxq);
  (*f)->type = FD_SOCK;
  (*f)->readable = 1;
  (*f)->writable = 1;
  (*f)->sock = si;

  // add to list of sockets
  acquire(&lock);
  pos = sockets;
  while (pos) {
    if (pos->raddr == raddr &&
        pos->lport == lport &&
	pos->rport == rport) {
      release(&lock);
      goto bad;
    }
    pos = pos->next;
  }
  si->next = sockets;
  sockets = si;
  release(&lock);
  return 0;

bad:
  if (si)
    kfree((char*)si);
  if (*f)
    fileclose(*f);
  return -1;
}
```



### sock-r/w/c

剩下的接口属于[Lab](#Task II: Network sockets)部分



# The lab

## Task I: Network device driver

### 发送接口

- 初始化时，`TDH和TDT`都是0，所以硬件会发现没有描述符可以使用，此时`TDT`站住的，是第一个可用的软件槽位，所以实现时会先填充再移动指针
- 按照lab的提示和手册的[3.3.3]设置相关的flag

```cpp
int e1000_transmit(struct mbuf *m)
{

  acquire(&e1000_lock);
  uint32 idx = regs[E1000_TDT];
  // find the tail
  struct tx_desc *d = &tx_ring[idx];
  struct mbuf* old_m = tx_mbufs[idx];

  // this tail is not ready for software yet.
  if ((d->status & E1000_TXD_STAT_DD) == 0) {
    release(&e1000_lock);
    return -1;
  }

  // old_buf has been transmitted, free it
  if (old_m)
    mbuffree(old_m);
  // fill descriptor to send packet
  d->addr = (uint64)m->head;
  d->length = m->len;
  d->cmd = E1000_TXD_CMD_RS | E1000_TXD_CMD_EOP;
  d->status = 0;
  // store buf for later freeing
  tx_mbufs[idx] = m;

  regs[E1000_TDT] = (idx + 1) % TX_RING_SIZE;
  release(&e1000_lock);
  return 0;
}
```

### 接收接口

- 初始化时，`RDH=0; RDT=15`，每个描述符都标记为硬件可用(`status=0`)，所以收取时，应该先位移再填充使用
- 值得一提的是这里锁的使用，当多个中断来时，可能有多个线程在`e1000_recv`，所以要把每次把共享数据的访问都处在锁的保护之下。而`net_rx`接收数据就可以开启并行。

```cpp
static void e1000_recv(void)
{
  uint32 idx; 
  struct rx_desc *d;
  struct mbuf *buf;
  while (1) {
    acquire(&e1000_lock);
    idx = (1 + regs[E1000_RDT]) % RX_RING_SIZE;
    d = &rx_ring[idx];
    if ((d->status & E1000_RXD_STAT_DD) == 0)
      break;

    buf = rx_mbufs[idx];
    // update buf's len, core logic: buf->len += d->length;
    mbufput(buf, d->length);

    struct mbuf *new_buf = mbufalloc(0);
    if (new_buf == 0)
      return
    d->addr = (uint64)new_buf->head;
    d->status = 0;
    regs[E1000_RDT] = idx;
    release(&e1000_lock);

    net_rx(buf);
  }
}
```



## Task II: Network sockets

在操作系统层面，实现一种新的file——socket。在`file.c`中添加`FD_SOCK`的分支判断就略过了。

### sockwrite

主要考虑是写NIC的ring buffer时，每个描述符是一个包，没有分组的参数，所以在这里限制了UDP的发送容量。

```cpp
int sockwrite(struct sock *so, uint64 addr, int n) {
  // Splitting Udp packet into multiple mbufs is not supported
  if (n > MAX_UDP_PAYLOAD)
    return -1;
  struct mbuf *buf = mbufalloc(MBUF_DEFAULT_HEADROOM);
  if(copyin(myproc()->pagetable, buf->head, addr, n) == -1) {
    mbuffree(buf);
    return -1;
  }
  mbufput(buf, n);
  net_tx_udp(buf, so->raddr, so->lport, so->rport);
  return n;
}
```



### sockread

主要考虑了两点：

1. 如果希望读取的字符小于第一个`mbuf`的容量，不从`rxq`中弹出，下次可以继续读；如果超过了，弹出，把第一个`mbuf`读完返回
2. 并发。上锁期间从`rxq`中获取需要读取的内存片段，然后释放锁，并发地向用户空间拷贝

```cpp
int sockread(struct sock *so, uint64 addr, int n) {
  acquire(&so->lock);
  // block read process
  while (mbufq_empty(&so->rxq)) {
    sleep(&so->rxq, &so->lock);
  }

  struct mbuf *buf;
  char *read_from;
  int do_free = 0;
  if (n >= so->rxq.head->len) {
    // read all from the first mbuf
    buf = mbufq_pophead(&so->rxq);
    n = buf->len;
    read_from = buf->head;
    do_free = 1;
  } else {
    // read a part of the first mbuf
    buf = so->rxq.head;
    read_from = mbufpull(buf, n);
  }

  // we've done with the rxq linked list
  release(&so->lock);

  if (copyout(myproc()->pagetable, addr, read_from, n) == -1) {
    if(do_free)
      mbuffree(buf);
    return -1;
  }

  if(do_free)
    mbuffree(buf);
  return n;
}
```





### sockclose

如注释里所写，在释放`struct sock`前，需要释放`rxq`中的`mbuf`，这部分可以在锁外执行，因为当有且仅有一个进程持有当前`file`时才会调用`sockclose`，所以这部分代码执行一定是单线程的，不必担心会释放正在读取的`mbuf`。不要因为`pipeclose`中有`wakeup`就想在这里也使用，实际上没有效果。

```cpp
void sockclose(struct sock *so) {
  acquire(&lock); // protext `sockets` linked list
  struct sock *prev, *pos;
  prev = pos = sockets;
  while (pos) {
    if (pos == so) {
      if (prev == pos)
        sockets = 0; // delete head
      else
        prev->next = pos->next; // delete target socket
      release(&lock);
      // free mbuf before freeing socket
      // it's impossible to free a mbuf being read,
      // because the process calling `sockclose` is the only one
      // who is able to access the current `struct sock`
      struct mbuf *m = pos->rxq.head;
      struct mbuf *to_free;
      while (m) {
        to_free = m;
        m = m->next;
        mbuffree(to_free);
      }
      kfree((void *)so);
      return;
    }
    prev = pos;
    pos = pos->next;
  }
  release(&lock);
  panic("sockclose: no target socket");
}
```



### sockrecvudp

主要考虑还是并发，之前的想法是找到可以处理的`struct sock`后，释放掉`lock`，然后再上内部锁`so->lock`，插入buffer，避免同时上锁的成本。但是如果`sockclose`和`sockrecvudp`同时运行，先找到sock后，释放了`lock`，还没来得及插入`mbuf`，就被`sockclose`执行完毕，释放`struct sock`，再插入就会造成内存不安全。所以最终还是选择同时持有两把锁。

```cpp
void sockrecvudp(struct mbuf *m, uint32 raddr, uint16 lport, uint16 rport)
{
  struct sock *pos;
  acquire(&lock);
  pos = sockets;
  while (pos) {
    if (pos->raddr == raddr &&
        pos->lport == lport &&
        pos->rport == rport) {
      acquire(&pos->lock);
      mbufq_pushtail(&pos->rxq, m);
      release(&pos->lock);
      release(&lock);
      wakeup(&pos->rxq);
      return;
    }
    pos = pos->next;
  }
  
  mbuffree(m);
  release(&lock); 
}
```
