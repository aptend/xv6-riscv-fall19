
#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "vma.h"


struct {
  struct spinlock lock;
  struct vma vma[NVMA];
} vtable;


void vmainit(void) {
  initlock(&vtable.lock, "vtable");
}


struct vma* vmaalloc(void) {
  struct vma *v;
  acquire(&vtable.lock);
  for(v = vtable.vma; v < vtable.vma + NVMA; v++) {
    if(!v->valid) {
      v->valid = 1;
      release(&vtable.lock);
      return v;
    }
  }
  release(&vtable.lock);
  return 0;
}

struct vma* vmadup(struct vma* v) {
  struct vma *nv;
  if((nv = vmaalloc()) == 0)
    return 0;
  *nv = *v;
  filedup(nv->file);
  return nv;
};



void vmafree(struct vma* v) {
  acquire(&vtable.lock);
  v->valid = 0;
  release(&vtable.lock);
  fileclose(v->file);
  return;
}
