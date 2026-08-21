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
} kmem[NCPU];

void
kinit()
{
  for(int i = 0; i < NCPU; i++){
    initlock(&kmem[i].lock, "kmem");
    kmem[i].freelist = 0;
  }

  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
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

  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  push_off();

  int id = cpuid();

  acquire(&kmem[id].lock);

  r->next = kmem[id].freelist;
  kmem[id].freelist = r;

  release(&kmem[id].lock);

  pop_off();
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r = 0;

  push_off();

  int id = cpuid();

  // 先从当前 CPU 的 freelist 获取
  acquire(&kmem[id].lock);

  r = kmem[id].freelist;

  if(r){
    kmem[id].freelist = r->next;
    r->next = 0;
  }

  release(&kmem[id].lock);

  // 当前 CPU 没有空闲页，去其它 CPU 偷
  if(r == 0){

    for(int i = 0; i < NCPU; i++){

      if(i == id)
        continue;

      acquire(&kmem[i].lock);

      if(kmem[i].freelist){

        // 统计 victim CPU 有多少空闲页
        int count = 0;

        struct run *p = kmem[i].freelist;

        while(p){
          count++;
          p = p->next;
        }

        // 偷一半
        int steal = count / 2;

        if(steal < 1)
          steal = 1;

        struct run *head = kmem[i].freelist;
        struct run *last = head;

        for(int j = 1; j < steal; j++)
          last = last->next;

        // 从 victim freelist 中摘下来
        kmem[i].freelist = last->next;
        last->next = 0;

        release(&kmem[i].lock);

        // 加入当前 CPU freelist
        acquire(&kmem[id].lock);

        last->next = kmem[id].freelist;
        kmem[id].freelist = head;

        // 当前这次 kalloc 直接拿走一个
        r = kmem[id].freelist;
        kmem[id].freelist = r->next;
        r->next = 0;

        release(&kmem[id].lock);

        break;
      }

      release(&kmem[i].lock);
    }
  }

  pop_off();

  if(r)
    memset((char*)r, 5, PGSIZE);

  return (void*)r;
}