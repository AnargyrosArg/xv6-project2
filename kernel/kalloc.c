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

//table holding reference counters of each page
struct{
  struct spinlock lock;
  int ref_counter[PHYSTOP/PGSIZE];
} ref_counters;


void
kinit()
{
  initlock(&ref_counters.lock, "ref_counters");
  //lock ref counters just in case 
  acquire(&ref_counters.lock);
  for(int i=0;i<PHYSTOP/PGSIZE;i++){
    //initialize all table entries with 0
    ref_counters.ref_counter[i]=0;
  }
  //critical section done, release
  release(&ref_counters.lock);
  initlock(&kmem.lock, "kmem");
  freerange(end, (void*)PHYSTOP);
  printf("kinit done\n");
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
  if(ref_counters.ref_counter[(uint64)pa/PGSIZE]==1 || ref_counters.ref_counter[(uint64)pa/PGSIZE]==0){
    //the OS system calls kfree during initialization (? not sure) for some pages, without a respective kalloc call previously
    //this can mess with the ref counters, so if they are 1 or 0 we set them to 0 to prevent negative values
    struct run *r;
    if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
      panic("kfree");
    // Fill with junk to catch dangling refs.
    memset(pa, 1, PGSIZE);
    r = (struct run*)pa;
    acquire(&kmem.lock);
    r->next = kmem.freelist;
    kmem.freelist = r;
    release(&kmem.lock);
    acquire(&ref_counters.lock);
    ref_counters.ref_counter[(uint64)pa/PGSIZE]=0;
    release(&ref_counters.lock);
  }else if(ref_counters.ref_counter[(uint64)pa/PGSIZE]>1){
    //if the respective counter is >1 we simply decrement it, the page is still used by some other process
    acquire(&ref_counters.lock);
    ref_counters.ref_counter[(uint64)pa/PGSIZE]--;
    release(&ref_counters.lock);
  }else{
    //this should never happen, unless cosmic rays!
    panic("ref for page negative\n");
  }
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
  //locking the refcounter here might be unnecessary
  // we use the already existing kmem locks just in case
  ref_counters.ref_counter[(uint64)r/PGSIZE]=1;
  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
