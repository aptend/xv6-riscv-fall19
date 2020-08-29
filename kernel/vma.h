
struct vma {
  uint64 ostart; // immutable original start address, used for offset
  uint64 start;
  uint64 end;
  int valid; // redundant, but used for clarity
  int prot;
  int flags;
  struct file* file;
};

#define PROT_READ 0x02  /* pages can be read */
#define PROT_WRITE 0x01 /* pages can be written */

#define MAP_SHARED 0x02  /* share changes */
#define MAP_PRIVATE 0x01 /* changes are private */
