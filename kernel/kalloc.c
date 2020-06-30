// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"
#include "proc.h"


extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct kmem {
  struct spinlock lock;
  struct run *freelist;
};

struct kmem kmems[NCPU];

void freerange(struct kmem *k, void *pa_start, void *pa_end);

// void
// kinit()
// {
//   initlock(&kmem.lock, "kmem");
//   freerange(end, (void*)PHYSTOP);
// }

// void
// freerange(void *pa_start, void *pa_end)
// {
//   char *p;
//   p = (char*)PGROUNDUP((uint64)pa_start);
//   for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
//     kfree(p);
// }

void kinit()
{
  struct kmem *k;
  char *p;
  printf("---kinit start---\n");

  p = (char *)PGROUNDUP((uint64)end);
  uint64 per_seg = PGROUNDDOWN(((uint64)PHYSTOP - (uint64)p) / NCPU);
  printf("every segment has %d pages\n", per_seg / PGSIZE);

  for (k=kmems; k < kmems + NCPU - 1; k++) {
    initlock(&k->lock, "kmem");
    freerange(k, p, p+per_seg);
    p += per_seg;
  }
  // the last kmem takes care of all left pages.
  initlock(&k->lock, "kmem");
  printf("the last segment has %d pages\n", ((uint64)PHYSTOP - (uint64)p) / PGSIZE);
  freerange(k, p, (void *)PHYSTOP);

  printf("---kinit start---\n\n");
}

void freerange(struct kmem *k, void *pa_start, void *pa_end)
{
  // pa_start has been rounded
  char *p = (char *)pa_start;
  struct run *r;

  // no need to acquire any lock because we are running in one CPU (main.c::main)
  for (p = (char *)pa_start; p + PGSIZE <= (char *)pa_end; p += PGSIZE)
  {
    memset(p, 1, PGSIZE);
    r = (struct run *)p;
    r->next = k->freelist;
    k->freelist = r;
  }
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  push_off();
  struct kmem *k = &kmems[cpuid()];
  acquire(&k->lock);
  r->next = k->freelist;
  k->freelist = r;
  release(&k->lock);
  pop_off();
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  push_off();
  struct kmem *k = &kmems[cpuid()];
  acquire(&k->lock);
  r = k->freelist;
  if(r)
    k->freelist = r->next;
  release(&k->lock);
  pop_off();

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
