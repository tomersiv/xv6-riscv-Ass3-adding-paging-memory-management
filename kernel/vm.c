#include "param.h"
#include "types.h"
#include "memlayout.h"
#include "elf.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "spinlock.h"
#include "proc.h"
#include "queue.h"

/*
 * the kernel's page table.
 */
pagetable_t kernel_pagetable;

extern char etext[]; // kernel.ld sets this to end of kernel code.

extern char trampoline[]; // trampoline.S

// Make a direct-map page table for the kernel.
pagetable_t
kvmmake(void)
{
  pagetable_t kpgtbl;

  kpgtbl = (pagetable_t)kalloc();
  memset(kpgtbl, 0, PGSIZE);

  // uart registers
  kvmmap(kpgtbl, UART0, UART0, PGSIZE, PTE_R | PTE_W);

  // virtio mmio disk interface
  kvmmap(kpgtbl, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);

  // PLIC
  kvmmap(kpgtbl, PLIC, PLIC, 0x400000, PTE_R | PTE_W);

  // map kernel text executable and read-only.
  kvmmap(kpgtbl, KERNBASE, KERNBASE, (uint64)etext - KERNBASE, PTE_R | PTE_X);

  // map kernel data and the physical RAM we'll make use of.
  kvmmap(kpgtbl, (uint64)etext, (uint64)etext, PHYSTOP - (uint64)etext, PTE_R | PTE_W);

  // map the trampoline for trap entry/exit to
  // the highest virtual address in the kernel.
  kvmmap(kpgtbl, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);

  // map kernel stacks
  proc_mapstacks(kpgtbl);

  return kpgtbl;
}

// Initialize the one kernel_pagetable
void kvminit(void)
{
  kernel_pagetable = kvmmake();
}

// Switch h/w page table register to the kernel's page table,
// and enable paging.
void kvminithart()
{
  w_satp(MAKE_SATP(kernel_pagetable));
  sfence_vma();
}

// Return the address of the PTE in page table pagetable
// that corresponds to virtual address va.  If alloc!=0,
// create any required page-table pages.
//
// The risc-v Sv39 scheme has three levels of page-table
// pages. A page-table page contains 512 64-bit PTEs.
// A 64-bit virtual address is split into five fields:
//   39..63 -- must be zero.
//   30..38 -- 9 bits of level-2 index.
//   21..29 -- 9 bits of level-1 index.
//   12..20 -- 9 bits of level-0 index.
//    0..11 -- 12 bits of byte offset within the page.
pte_t *
walk(pagetable_t pagetable, uint64 va, int alloc)
{
  if (va >= MAXVA)
    panic("walk");

  for (int level = 2; level > 0; level--)
  {
    pte_t *pte = &pagetable[PX(level, va)];
    if (*pte & PTE_V)
    {
      pagetable = (pagetable_t)PTE2PA(*pte);
    }
    else
    {
      if (!alloc || (pagetable = (pde_t *)kalloc()) == 0)
        return 0;
      memset(pagetable, 0, PGSIZE);
      *pte = PA2PTE(pagetable) | PTE_V;
    }
  }
  return &pagetable[PX(0, va)];
}

// Look up a virtual address, return the physical address,
// or 0 if not mapped.
// Can only be used to look up user pages.
uint64
walkaddr(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  uint64 pa;

  if (va >= MAXVA)
    return 0;

  pte = walk(pagetable, va, 0);
  if (pte == 0)
    return 0;
  if ((*pte & PTE_V) == 0)
    return 0;
  if ((*pte & PTE_U) == 0)
    return 0;
  pa = PTE2PA(*pte);
  return pa;
}

// add a mapping to the kernel page table.
// only used when booting.
// does not flush TLB or enable paging.
void kvmmap(pagetable_t kpgtbl, uint64 va, uint64 pa, uint64 sz, int perm)
{
  if (mappages(kpgtbl, va, sz, pa, perm) != 0)
    panic("kvmmap");
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa. va and size might not
// be page-aligned. Returns 0 on success, -1 if walk() couldn't
// allocate a needed page-table page.
int mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm)
{
  uint64 a, last;
  pte_t *pte;

  a = PGROUNDDOWN(va);
  last = PGROUNDDOWN(va + size - 1);
  for (;;)
  {
    if ((pte = walk(pagetable, a, 1)) == 0)
      return -1;
    if (*pte & PTE_V)
      panic("remap");
    *pte = PA2PTE(pa) | perm | PTE_V;
    if (a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

// Remove npages of mappings starting from va. va must be
// page-aligned. The mappings must exist.
// Optionally free the physical memory.
void uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free)
{
  uint64 a;
  pte_t *pte;

  if ((va % PGSIZE) != 0)
    panic("uvmunmap: not aligned");

  for (a = va; a < va + npages * PGSIZE; a += PGSIZE)
  {
    if ((pte = walk(pagetable, a, 0)) != 0)
    {
      if (PTE_FLAGS(*pte) == PTE_V)
        panic("uvmunmap: not a leaf");
      if (do_free && (*pte & PTE_V))
      {
        uint64 pa = PTE2PA(*pte);
        kfree((void *)pa);
      }
      // TODO: maybe need to change the order
#if SELECTION != NONE
      struct proc *p = myproc()

          if (a / PGSIZE < 32)
      {
        struct page_data *page = p->paging_meta_data[a / PGSIZE];

        // remove item from main memory queue
        remove_item(p->queue, a / PGSIZE);

        // set the appropriate fields to their default
        page->stored = 0;
        page->offset = -1;
      }
#endif
      *pte = 0;
    }
  }
}

// create an empty user page table.
// returns 0 if out of memory.
pagetable_t
uvmcreate()
{
  pagetable_t pagetable;
  pagetable = (pagetable_t)kalloc();
  if (pagetable == 0)
    return 0;
  memset(pagetable, 0, PGSIZE);
  return pagetable;
}

// Load the user initcode into address 0 of pagetable,
// for the very first process.
// sz must be less than a page.
void uvminit(pagetable_t pagetable, uchar *src, uint sz)
{
  char *mem;

  if (sz >= PGSIZE)
    panic("inituvm: more than a page");
  mem = kalloc();
  memset(mem, 0, PGSIZE);
  mappages(pagetable, 0, PGSIZE, (uint64)mem, PTE_W | PTE_R | PTE_X | PTE_U);
  memmove(mem, src, sz);
}

// Allocate PTEs and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
uint64
uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  char *mem;
  uint64 a;

  if (newsz < oldsz)
    return oldsz;

  oldsz = PGROUNDUP(oldsz);
  for (a = oldsz; a < newsz; a += PGSIZE)
  {
    #if SELECTION != NONE
      int num_of_pages = 0;
      struct proc *p = myproc();
      struct page_data *page;

      for (page = p->paging_info; page < &p->paging_info[MAX_TOTAL_PAGES]; page++)
      {
        if (page->stored)
        {
          num_of_pages++;
        }
      }

      if (num_of_pages < MAX_PSYC_PAGES)
      {
        goto not_none
      }
      else
      {
        if (mappages(pagetable, a, PGSIZE, 0,
                     PTE_W | PTE_R | PTE_X | PTE_U | PTE_PG) != 0)
        {
          uvmdealloc(pagetable, newsz, oldsz);
          return 0;
        }

        pte_t *pte = walk(pagetable, a, 0);
        *pte &= (~PTE_V);

        // now, find the next available offset index in Swapfile to write the page to
        // TODO: maybe need to change this
        int offset;
        for (int i = 0; i < p->sz; i += PGSIZE)
        {
          int found = 1;
          for (int j = 0; j < MAX_TOTAL_PAGES; j++)
          {
            if(p->paging_info[j].offset == i)
            {
              found = 0;
              break;
            }
          }
          if(found)
          {
            offset = i;
            break;
          }
        }
        p->paging_info[a / PGSIZE].offset = offset;
        goto finish;
      }
      not_none:
    #endif

    mem = kalloc();
    if (mem == 0)
    {
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
    memset(mem, 0, PGSIZE);
    if (mappages(pagetable, a, PGSIZE, (uint64)mem, PTE_W | PTE_X | PTE_R | PTE_U) != 0)
    {
      kfree(mem);
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }

    #if SELECTION != NONE
      finish:
      uint age = 0;
      struct proc *p = myproc();
      struct page_data *page = p->paging_info[a / PGSIZE];

      #if SELECTION == LAPA
        age = 0xFFFFFFFF;
      #endif

      #if SELECTION == SCFIFO
        enqueue(&p->queue, a / PGSIZE);
      #endif

      page->age = age;
      page->stored = 1;
  #endif
  }
  return newsz;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
uint64
uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  if (newsz >= oldsz)
    return oldsz;

  if (PGROUNDUP(newsz) < PGROUNDUP(oldsz))
  {
    int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
    uvmunmap(pagetable, PGROUNDUP(newsz), npages, 1);
  }

  return newsz;
}

// Recursively free page-table pages.
// All leaf mappings must already have been removed.
void freewalk(pagetable_t pagetable)
{
  // there are 2^9 = 512 PTEs in a page table.
  for (int i = 0; i < 512; i++)
  {
    pte_t pte = pagetable[i];
    if ((pte & PTE_V) && (pte & (PTE_R | PTE_W | PTE_X)) == 0)
    {
      // this PTE points to a lower-level page table.
      uint64 child = PTE2PA(pte);
      freewalk((pagetable_t)child);
      pagetable[i] = 0;
    }
    else if (pte & PTE_V)
    {
      panic("freewalk: leaf");
    }
  }
  kfree((void *)pagetable);
}

// Free user memory pages,
// then free page-table pages.
void uvmfree(pagetable_t pagetable, uint64 sz)
{
  if (sz > 0)
    uvmunmap(pagetable, 0, PGROUNDUP(sz) / PGSIZE, 1);
  freewalk(pagetable);
}

// Given a parent process's page table, copy
// its memory into a child's page table.
// Copies both the page table and the
// physical memory.
// returns 0 on success, -1 on failure.
// frees any allocated pages on failure.
int uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
  pte_t *pte;
  uint64 pa, i;
  uint flags;
  char *mem;

  for (i = 0; i < sz; i += PGSIZE)
  {
    // TODO: if we get those 2 panics bellow replace them both for:
    // if((pte = walk(old, i, 0)) !=0 && (*pte & PTE_V) != 0)
    if ((pte = walk(old, i, 0)) == 0)
      panic("uvmcopy: pte should exist");
    if ((*pte & PTE_V) == 0)
      panic("uvmcopy: page not present");
    pa = PTE2PA(*pte);
    flags = PTE_FLAGS(*pte);
    if ((mem = kalloc()) == 0)
      goto err;
    memmove(mem, (char *)pa, PGSIZE);
    if (mappages(new, i, PGSIZE, (uint64)mem, flags) != 0)
    {
      kfree(mem);
      goto err;
    }
  }
  return 0;

err:
  uvmunmap(new, 0, i / PGSIZE, 1);
  return -1;
}

// mark a PTE invalid for user access.
// used by exec for the user stack guard page.
void uvmclear(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;

  pte = walk(pagetable, va, 0);
  if (pte == 0)
    panic("uvmclear");
  *pte &= ~PTE_U;
}

// Copy from kernel to user.
// Copy len bytes from src to virtual address dstva in a given page table.
// Return 0 on success, -1 on error.
int copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
  uint64 n, va0, pa0;

  while (len > 0)
  {
    va0 = PGROUNDDOWN(dstva);
    pa0 = walkaddr(pagetable, va0);
    if (pa0 == 0)
      return -1;
    n = PGSIZE - (dstva - va0);
    if (n > len)
      n = len;
    memmove((void *)(pa0 + (dstva - va0)), src, n);

    len -= n;
    src += n;
    dstva = va0 + PGSIZE;
  }
  return 0;
}

// Copy from user to kernel.
// Copy len bytes to dst from virtual address srcva in a given page table.
// Return 0 on success, -1 on error.
int copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len)
{
  uint64 n, va0, pa0;

  while (len > 0)
  {
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if (pa0 == 0)
      return -1;
    n = PGSIZE - (srcva - va0);
    if (n > len)
      n = len;
    memmove(dst, (void *)(pa0 + (srcva - va0)), n);

    len -= n;
    dst += n;
    srcva = va0 + PGSIZE;
  }
  return 0;
}

// Copy a null-terminated string from user to kernel.
// Copy bytes to dst from virtual address srcva in a given page table,
// until a '\0', or max.
// Return 0 on success, -1 on error.
int copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max)
{
  uint64 n, va0, pa0;
  int got_null = 0;

  while (got_null == 0 && max > 0)
  {
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if (pa0 == 0)
      return -1;
    n = PGSIZE - (srcva - va0);
    if (n > max)
      n = max;

    char *p = (char *)(pa0 + (srcva - va0));
    while (n > 0)
    {
      if (*p == '\0')
      {
        *dst = '\0';
        got_null = 1;
        break;
      }
      else
      {
        *dst = *p;
      }
      --n;
      --max;
      p++;
      dst++;
    }

    srcva = va0 + PGSIZE;
  }
  if (got_null)
  {
    return 0;
  }
  else
  {
    return -1;
  }
}

int calculate_NFUA_index()
{
  struct proc *p = myproc();
  int page_num = -1;
  uint age = -1;

  for (int i = 3; i < MAX_TOTAL_PAGES; i++) //TODO: check why start from 3
  {
    struct page_data page = p->paging_info[i];
    if ((page.stored == 1) && ((page.age < age) || (age == -1)))
    {
      page_num = i;
      age = page.age;
    }
  }

  return page_num;
}

int calculate_LAPA_index()
{
  // TODO: delete this commented section
  // struct proc *p = myproc();
  // uint curr_age = -1;
  // int minimum_page_index = 0;
  // int minimum_ones = -1;
  // int curr_ones = -1;
  // uint minimum_age = -1;
  // int ones_counter;
  // uint temp_age = -1;
  // for(int i = 3; i < MAX_TOTAL_PAGES; i++)
  // {
  //   if(p->paging_info[i].stored)
  //   {
  //     curr_age = p->paging_info[i].age;
  //     temp_age = curr_age;

  //     // count 1 bits
  //     ones_counter = 0;
  //     while(curr_age)
  //     {
  //       ones_counter += curr_age & 1;
  //       curr_age >>= 1;
  //     }
  //     curr_age = temp_age;
  //     if((minimum_ones == ones_counter && curr_age < minimum_age) || (ones_counter < minimum_ones) || (minimum_ones == -1))
  //     {
  //       minimum_page_index = i;
  //       minimum_ones = ones_counter;
  //       minimum_age = curr_age;
  //     }
  //   }
  // }

  // if(minimum_ones != -1)
  // {
  //   return minimum_page_index;
  // }
  // else
  // {
  //   panic("failed to replace pages");
  // }

  uint age = -1;
  int ones = -1;
  int page_num = 0;
  struct proc *p = myproc();

  for (int i = 3; i < MAX_TOTAL_PAGES; i++)
  {
    struct page_data page = p->paging_info[i];
    if (page.stored == 1)
    {
      // count 1 bits
      int counter = 0;
      uint idx_mask = 1;

      // 110101110100101
      // 000000000000100

      while (idx_mask != 0)
      {
        if ((page.age & idx_mask) != 0)
        {
          counter++;
        }
        idx_mask = idx_mask << 1;
      }

      if ((ones == counter && page.age < age) ||
          counter < ones || ones == -1)
      {
        page_num = i;
        ones = counter;
        age = page.age;
      }
    }
  }
  return page_num;
}

int calculate_SCFIFO_index()
{
  struct proc *p = myproc();
  int initial_size = p->queue.size;
  for (int i = 0; i < initial_size; i++)
  {
    pte_t *pte = walk(p->pagetable, PGSIZE * (p->queue.q[p->queue.front]), 0);

    // if the access bit is turend on, give the page a second chance
    if (PTE_A & PTE_FLAGS(*pte)) // TODO: check if works without PTE_FLAGS
    {
      front_to_rear(&p->queue); // move the front item to rear
      *pte &= ~PTE_A;           // turn off access bit
    }
    else
    {
      break;
    }
  }
  return dequeue(&p->queue);
}

void handle_page_fault()
{
  struct proc *p = myproc();
  uint64 addr = r_stval();                               // TODO check if needed
  pte_t *pte = walk(p->pagetable, PGROUNDDOWN(addr), 0); //TODO maybe doesn't need to PGROUNDDOWN

  if (pte != 0)
  {
    if (((*pte & PTE_V) == 0) &&
        (*pte & PTE_PG) != 0) // TODO: check PTE_PG
    {
      // valid is off and page is in secondary memory - import page from Swapfile
      swap_pages(addr, pte);
    }
  }
  else if (addr > p->sz)
  {
    exit(-1);
  }
  else
  {
    // lazy allocation
    uvmalloc(p->pagetable, PGROUNDDOWN(addr), PGROUNDDOWN(addr) + PGSIZE);
  }
}

void swap_pages(uint64 faulting_address, pte_t *faulting_address_entry)
{
  struct proc *p = myproc();
  int faulted_page_index = PGROUNDDOWN(faulting_address) / PGSIZE;
  int offset = p->paging_info[faulted_page_index].offset; //offset of faulted page is swapfile

  // TODO: if everything works, remove this if-else block
  if (offset != -1)
  {
    char *faulted_page = kalloc();

    // TODO: if everything works, remove this if block
    if (faulted_page == 0)
    {
      panic("kalloc");
    }

    // TODO: if everything works, replace this if block with:
    // readFromSwapFule(p, faulted_page, faulting_address_offsed, PGSIZE);
    if (readFromSwapFile(p, faulted_page, offset, PGSIZE) == -1)
    {
      printf("failed to read from Swapfile");
    }

    // get number of pages in main memory
    int pages_in_main = 0;
    struct page_data *page;

    for (page = p->paging_info; page < &p->paging_info[MAX_TOTAL_PAGES]; page++)
    {
      if (page->stored == 1)
      {
        pages_in_main++;
      }
    }

    if (pages_in_main < MAX_PSYC_PAGES)
    {
      *faulting_address_entry = PA2PTE((uint64)faulted_page) | PTE_V;
    }
    else // swap pages from main memory to Swapfile
    {
      // now we write the page to the secondary memory (Swapfile).

      // TODO: we need to swap pages according to replacement algorithms,
      // now we will replace the first page only for checking task 1.
      int swapped_page_index = 0;

#if SELECTION == NFUA // Todo: change to ifdef
      swapped_page_index = calculate_NFUA_index();
#endif

#if SELECTION == LAPA // Todo: change to ifdef
      swapped_page_index = calculate_LAPA_index();
#endif

#if SELECTION == SCFIFO // Todo: change to ifdef
      swapped_page_index = calculate_SCFIFO_index();
#endif

      uint64 swapped_page_va = swapped_page_index * PGSIZE;

      pte_t *swapped_page_entry = walk(p->pagetable, swapped_page_va, 0); //TODO: minimize to one line
      uint64 swapped_page_PA = PTE2PA(*swapped_page_entry);
      if (writeToSwapFile(p, (char *)swapped_page_PA, offset, PGSIZE) == -1)
      {
        printf("failed to write the swapped page to Swapfile");
      }
      *swapped_page_entry = (*swapped_page_entry & (~PTE_V)) | PTE_PG; // TODO: check PTE_PG
      p->paging_info[swapped_page_index].stored = 0;
      p->paging_info[swapped_page_index].offset = offset;

      // set the faulted address entry in the main memory
      *faulting_address_entry = PA2PTE((uint64)faulted_page) | (PTE_V | (~PTE_PG & PTE_FLAGS(*faulting_address_entry))); //TODO check if commutative
                                                                                                                         // TODO: check PTE_PG
      kfree((void *)swapped_page_PA);                                                                                    // TODO: check if needed
    }
    // page is no longer in Swapfile, so need to update
    p->paging_info[faulted_page_index].stored = 1;
    p->paging_info[faulted_page_index].offset = -1;

    uint age = 0;

#if SELECTION == LAPA
    age = 0xFFFFFFFF;
#endif

#if SELECTION == SCFIFO
    enqueue(&p->queue, faulted_page_index);
#endif

    p->paging_info[faulted_page_index].age = age;

    sfence_vma(); // flush TLB
  }
  else // TODO: check if works then remove
  {
    printf("faulting address offset is not valid");
  }
}