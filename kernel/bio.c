// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"
#define NBUCKET 13

struct {
  // 只有 cache miss / buffer replacement 时用
  struct spinlock lock;

  // 每个 hash bucket 一把锁
  struct spinlock bucket_lock[NBUCKET];

  struct buf buf[NBUF];

  // 每个 bucket 自己一条链表
  struct buf head[NBUCKET];

} bcache;
static int
bhash(uint blockno)
{
  return blockno % NBUCKET;
}
void
binit(void)
{
  struct buf *b;

  initlock(&bcache.lock, "bcache");

  // 初始化 hash buckets
  for(int i = 0; i < NBUCKET; i++){

    initlock(&bcache.bucket_lock[i], "bcache.bucket");

    bcache.head[i].prev = &bcache.head[i];
    bcache.head[i].next = &bcache.head[i];
  }

  // 初始化所有 buffer
  for(int i = 0; i < NBUF; i++){

    b = &bcache.buf[i];

    initsleeplock(&b->lock, "buffer");

    b->refcnt = 0;

    int h = i % NBUCKET;

    // 插入对应 bucket
    b->next = bcache.head[h].next;
    b->prev = &bcache.head[h];

    bcache.head[h].next->prev = b;
    bcache.head[h].next = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf *
bget(uint dev, uint blockno)
{
  struct buf *b;
  int h = bhash(blockno);

  // ==================================================
  // 第一次查找
  // ==================================================

  acquire(&bcache.bucket_lock[h]);

  for(b = bcache.head[h].next;
      b != &bcache.head[h];
      b = b->next){

    if(b->dev == dev && b->blockno == blockno){

      b->refcnt++;

      release(&bcache.bucket_lock[h]);

      acquiresleep(&b->lock);

      return b;
    }
  }

  release(&bcache.bucket_lock[h]);



  // ==================================================
  // cache miss
  // 使用全局锁串行化 replacement
  // ==================================================

  acquire(&bcache.lock);



  // ==================================================
  // 必须再次查找
  // ==================================================

  acquire(&bcache.bucket_lock[h]);

  for(b = bcache.head[h].next;
      b != &bcache.head[h];
      b = b->next){

    if(b->dev == dev && b->blockno == blockno){

      b->refcnt++;

      release(&bcache.bucket_lock[h]);
      release(&bcache.lock);

      acquiresleep(&b->lock);

      return b;
    }
  }

  release(&bcache.bucket_lock[h]);



  // ==================================================
  // 找一个 refcnt == 0 的 buffer
  // ==================================================

  for(int i = 0; i < NBUCKET; i++){

    acquire(&bcache.bucket_lock[i]);

    for(b = bcache.head[i].next;
        b != &bcache.head[i];
        b = b->next){

      if(b->refcnt == 0){

        // 从原来的 bucket 中删除
        b->prev->next = b->next;
        b->next->prev = b->prev;

        if(i == h){

          // 新旧 bucket 一样
          b->dev = dev;
          b->blockno = blockno;
          b->valid = 0;
          b->refcnt = 1;

          // 重新插入
          b->next = bcache.head[h].next;
          b->prev = &bcache.head[h];

          bcache.head[h].next->prev = b;
          bcache.head[h].next = b;

          release(&bcache.bucket_lock[i]);

        }else{

    acquire(&bcache.bucket_lock[h]);


    b->dev = dev;
    b->blockno = blockno;
    b->valid = 0;
    b->refcnt = 1;


    b->next = bcache.head[h].next;
    b->prev = &bcache.head[h];

    bcache.head[h].next->prev = b;
    bcache.head[h].next = b;


    release(&bcache.bucket_lock[h]);
    release(&bcache.bucket_lock[i]);

}

        release(&bcache.lock);

        acquiresleep(&b->lock);

        return b;
      }
    }

    release(&bcache.bucket_lock[i]);
  }

  release(&bcache.lock);

  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  int h = bhash(b->blockno);

  acquire(&bcache.bucket_lock[h]);

  b->refcnt--;

  release(&bcache.bucket_lock[h]);
}

void
bpin(struct buf *b)
{
  int h = bhash(b->blockno);

  acquire(&bcache.bucket_lock[h]);

  b->refcnt++;

  release(&bcache.bucket_lock[h]);
}

void
bunpin(struct buf *b)
{
  int h = bhash(b->blockno);

  acquire(&bcache.bucket_lock[h]);

  b->refcnt--;

  release(&bcache.bucket_lock[h]);
}


