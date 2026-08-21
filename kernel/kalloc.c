// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

#define NPAGE ((PHYSTOP - KERNBASE) / PGSIZE)
#define PA2IDX(pa) (((uint64)(pa) - KERNBASE) / PGSIZE)

struct {
  struct spinlock lock;
  int cnt[NPAGE];
} refcnt;

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  initlock(&refcnt.lock, "refcnt");
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);

  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE){
    refcnt.cnt[PA2IDX(p)] = 1;
    kfree(p);
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

  if(((uint64)pa % PGSIZE) != 0 ||
     (char*)pa < end ||
     (uint64)pa >= PHYSTOP)
    panic("kfree");

  // 引用计数减 1
  acquire(&refcnt.lock);

  refcnt.cnt[PA2IDX(pa)]--;

  // 还有其他进程在使用这个物理页，不能真正释放
  if(refcnt.cnt[PA2IDX(pa)] > 0){
    release(&refcnt.lock);
    return;
  }

  // 出现负数说明引用计数逻辑有问题
  if(refcnt.cnt[PA2IDX(pa)] < 0){
    release(&refcnt.lock);
    panic("kfree ref");
  }

  release(&refcnt.lock);

  // 引用计数已经为 0，真正释放物理页
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r){
    memset((char*)r, 5, PGSIZE);

    acquire(&refcnt.lock);
    refcnt.cnt[PA2IDX(r)] = 1;
    release(&refcnt.lock);
  }
  return (void*)r;
}

void
krefinc(uint64 pa)
{
  acquire(&refcnt.lock);
  refcnt.cnt[PA2IDX(pa)]++;
  release(&refcnt.lock);
}

