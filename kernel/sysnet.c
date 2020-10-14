//
// network system calls.
//

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"
#include "net.h"

struct sock {
  struct sock *next; // the next socket in the list
  uint32 raddr;      // the remote IPv4 address
  uint16 lport;      // the local UDP port number
  uint16 rport;      // the remote UDP port number
  struct spinlock lock; // protects the rxq
  struct mbufq rxq;  // a queue of packets waiting to be received
};

static struct spinlock lock;
static struct sock *sockets;

void
sockinit(void)
{
  initlock(&lock, "socktbl");
}

int
sockalloc(struct file **f, uint32 raddr, uint16 lport, uint16 rport)
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

//
// Your code here.
//
// Add and wire in methods to handle closing, reading,
// and writing for network sockets.
//

int
sockwrite(struct sock *so, uint64 addr, int n) {
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

int
sockread(struct sock *so, uint64 addr, int n) {
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

void
sockclose(struct sock *so) {
  acquire(&lock); // protext sockets linked list
  struct sock *prev, *pos;
  prev = pos = sockets;
  while (pos) {
    if (pos == so) {
      if (prev == pos)
        sockets = pos->next; // delete head
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
  panic("sockclose: find no target socket");
}

// called by protocol handler layer to deliver UDP packets
void
sockrecvudp(struct mbuf *m, uint32 raddr, uint16 lport, uint16 rport)
{
  //
  // Your code here.
  //
  // Find the socket that handles this mbuf and deliver it, waking
  // any sleeping reader. Free the mbuf if there are no sockets
  // registered to handle it.
  //
  // printf("receive a udp: %s\n", m->head);
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
