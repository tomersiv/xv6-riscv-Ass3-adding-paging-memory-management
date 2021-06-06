#include "param.h"
#include "types.h"
#include "memlayout.h"
#include "elf.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "spinlock.h"
#include "proc.h"

/*
 * the kernel's page table.
 */
pagetable_t kernel_pagetable;

extern char etext[];  // kernel.ld sets this to end of kernel code.

extern char trampoline[]; // trampoline.S

void swap(uint64 va, pte_t *pte);
int exchange_pages(uint64 va_on_swap, int in_swap);

// Make a direct-map page table for the kernel.
pagetable_t kvmmake(void)
{
  pagetable_t kpgtbl;

  kpgtbl = (pagetable_t) kalloc();
  memset(kpgtbl, 0, PGSIZE);

  // uart registers
  kvmmap(kpgtbl, UART0, UART0, PGSIZE, PTE_R | PTE_W);

  // virtio mmio disk interface
  kvmmap(kpgtbl, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);

  // PLIC
  kvmmap(kpgtbl, PLIC, PLIC, 0x400000, PTE_R | PTE_W);

  // map kernel text executable and read-only.
  kvmmap(kpgtbl, KERNBASE, KERNBASE, (uint64)etext-KERNBASE, PTE_R | PTE_X);

  // map kernel data and the physical RAM we'll make use of.
  kvmmap(kpgtbl, (uint64)etext, (uint64)etext, PHYSTOP-(uint64)etext, PTE_R | PTE_W);

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
pte_t *walk(pagetable_t pagetable, uint64 va, int alloc)
{
  if(va >= MAXVA)
    panic("walk");

  for(int level = 2; level > 0; level--) {
    pte_t *pte = &pagetable[PX(level, va)];
    if(*pte & PTE_V) {
      pagetable = (pagetable_t)PTE2PA(*pte);
    }
    else {
      if(!alloc || (pagetable = (pde_t*)kalloc()) == 0)
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
uint64 walkaddr(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  uint64 pa;

  if(va >= MAXVA)
    return 0;

  pte = walk(pagetable, va, 0);
  if(pte == 0)
    return 0;
  if((*pte & PTE_V) == 0)
    return 0;
  if((*pte & PTE_U) == 0)
    return 0;
  pa = PTE2PA(*pte);
  return pa;
}

// add a mapping to the kernel page table.
// only used when booting.
// does not flush TLB or enable paging.
void kvmmap(pagetable_t kpgtbl, uint64 va, uint64 pa, uint64 sz, int perm)
{
  if(mappages(kpgtbl, va, sz, pa, perm) != 0)
    panic("kvmmap");
}

int mappages_NONE(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm){
  uint64 a, last;
  pte_t *pte;

  a = PGROUNDDOWN(va);
  last = PGROUNDDOWN(va + size - 1);
  for(;;){
    if((pte = walk(pagetable, a, 1)) == 0)
      return -1;

    if (*pte & PTE_V)
      if (walkaddr(pagetable,va) != 0)
        panic("remap");
    
    *pte = PA2PTE(pa) | perm | PTE_V;
    if(a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

int mappages_not_NONE(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm){
  uint64 a, last;
  pte_t *pte;

  a = PGROUNDDOWN(va);
  last = PGROUNDDOWN(va + size - 1);
  for(;;){
    if((pte = walk(pagetable, a, 1)) == 0)
      return -1;
    
    if(*pte & PTE_V)
      panic("remap");

    *pte = PA2PTE(pa) | perm | PTE_V;
    if(a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa. va and size might not
// be page-aligned. Returns 0 on success, -1 if walk() couldn't
// allocate a needed page-table page.
int mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm)
{
    #ifdef NONE
      return mappages_NONE(pagetable, va, size, pa, perm);
    #endif
  
    #ifndef NONE
      return mappages_not_NONE(pagetable, va, size, pa, perm);
    #endif
}

int mappage(pagetable_t pagetable, uint64 va, uint64 pa, int perm)
{
  uint64 a;
  pte_t *pte;

  a = PGROUNDDOWN(va);
  
  if((pte = walk(pagetable, a, 1)) == 0)
    return -1;
  if(*pte & PTE_V)
    panic("remap");
  *pte = PA2PTE(pa) | perm | PTE_V;

  return 0;
}

void uvmunmap_NONE(pagetable_t pagetable, uint64 va, uint64 npages, int do_free){
  uint64 a;
  pte_t *pte;

  if((va % PGSIZE) != 0)
    panic("uvmunmap: not aligned");

  for(a = va; a < va + npages*PGSIZE; a += PGSIZE){
    if((pte = walk(pagetable, a, 0)) == 0)
      panic("uvmunmap: walk");

    if((*pte & PTE_V) == 0)
      panic("uvmunmap: not mapped");
    
    if (PTE_FLAGS(*pte) == PTE_V)
      if (walkaddr(pagetable,va) != 0)
        panic("uvmunmap: not a leaf");

    if(do_free){
      if (walkaddr(pagetable, va) != 0){
        uint64 pa = PTE2PA(*pte);
        kfree((void*)pa);
      }
    }
  
    *pte = 0;
  }
}

void uvmunmap_not_NONE(pagetable_t pagetable, uint64 va, uint64 npages, int do_free){
  struct proc *p;
  uint64 a;
  pte_t *pte;

  if((va % PGSIZE) != 0)
    panic("uvmunmap: not aligned");

  for(a = va; a < va + npages*PGSIZE; a += PGSIZE){
    if((pte = walk(pagetable, a, 0)) == 0)
      panic("uvmunmap: walk");

    if((*pte & PTE_V) == 0)
      if ((*pte & PTE_PG) == 0)
        panic("uvmunmap: not mapped");

    if(PTE_FLAGS(*pte) == PTE_V)
      panic("uvmunmap: not a leaf");

    if(do_free) {
      if ((*pte & PTE_PG) == 0){
        uint64 pa = PTE2PA(*pte);
        kfree((void*)pa);
      }
    }

    *pte = 0;
    p = myproc();

    if(p->pid > 2){
      for (int i=0; i < MAX_PSYC_PAGES; i++){
        if (p->ram[i].va == a)
          remove_page(&p->ram[i]);

        if (p->swap[i].va == a)
          remove_page(&p->swap[i]);
      } 
    }
  }
}

// Remove npages of mappings starting from va. va must be
// page-aligned. The mappings must exist.
// Optionally free the physical memory.
void uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free)
{
  #ifdef NONE
    uvmunmap_NONE(pagetable, va, npages, do_free);
  #endif

  #ifndef NONE
    uvmunmap_not_NONE(pagetable, va, npages, do_free);
  #endif
}

// create an empty user page table.
// returns 0 if out of memory.
pagetable_t uvmcreate()
{
  pagetable_t pagetable;
  pagetable = (pagetable_t) kalloc();
  if(pagetable == 0)
    return 0;
  memset(pagetable, 0, PGSIZE);
  return pagetable;
}

// Load the user initcode into address 0 of pagetable,
// for the very first process.
// sz must be less than a page.
void
uvminit(pagetable_t pagetable, uchar *src, uint sz)
{
  char *mem;

  if(sz >= PGSIZE)
    panic("inituvm: more than a page");
  mem = kalloc();
  memset(mem, 0, PGSIZE);
  mappages(pagetable, 0, PGSIZE, (uint64)mem, PTE_W | PTE_R | PTE_X | PTE_U);
  memmove(mem, src, sz);
}

// Allocate PTEs and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
uint64 uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz) {
  char *mem;
  uint64 a;
  
  if(newsz < oldsz)
    return oldsz;

  oldsz = PGROUNDUP(oldsz);

  for(a = oldsz; a < newsz; a += PGSIZE){
    #ifndef NONE
      struct proc *p = myproc();
      int idx = 0;

      if (p->pid > 2){
        if (!(a / PGSIZE < MAX_TOTAL_PAGES)) 
          panic("process cannot be larger than 32 pages");

        // when not called from exec
        if (p->pagetable == pagetable){
          if (count_pages(p->ram, 1) < MAX_PSYC_PAGES){
            struct page_data *page;
            for (page = p->ram; page < &p->ram[MAX_PSYC_PAGES]; page++){
              if (page->used == 0){
                idx = (int)(page - p->ram);
                break;
              }
            }
          }
          else 
          {
            idx = exchange_pages(a, 0);
          }
        }
      }
    #endif

    mem = kalloc();
    if(mem == 0){
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
    memset(mem, 0, PGSIZE);
    if(mappages(pagetable, a, PGSIZE, (uint64)mem, PTE_W | PTE_X | PTE_R | PTE_U) != 0){
      kfree(mem);
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }

    #ifndef NONE
      if (p->pid > 2){
        if (p->pagetable == pagetable){
          // add page to p->ram
          struct page_data *page_data = &p->ram[idx];

          #ifdef SCFIFO
            page_data->fifo_time = p->fifo_counter++;
          #endif

          #ifdef NFUA
            page_data->age = 0;
          #endif

          #ifdef LAPA
            page_data->age = 0xFFFFFFFF;
          #endif

          page_data->offset = -1;
          page_data->va = a;
          page_data->used = 1;

          // turn on valid bit
          pte_t *pte = walk(p->pagetable, a, 0);
          *pte = *pte | PTE_V;
        }
      }
      #ifdef SCFIFO
        if (p->pid > 2 && p->pagetable == pagetable){
          struct page_data *page;
          int page_index = -1;
          for (page = p->ram; page < &p->ram[MAX_PSYC_PAGES]; page++) {
            if (page->va == a) {
              page_index = (int)(page - p->ram);
              break;
            }
          }
          if(page_index == -1){
            panic("couldn't find page in RAM");
          }
        }
      #endif
    #endif
  }
  return newsz;
}

int
lazy_alloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz){
  uint64 a;
  pte_t *pte;
  
  if(newsz < oldsz)
    return oldsz; 
  
  oldsz = PGROUNDUP(oldsz);

  for(a = oldsz; a < newsz; a += PGSIZE){ 
    pte = walk(pagetable, a, 1);
    if (pte == 0){ 
      return 0;
    }
    *pte = *pte | PTE_V;
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
  if(newsz >= oldsz)
    return oldsz;

  if(PGROUNDUP(newsz) < PGROUNDUP(oldsz)){
    int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
    uvmunmap(pagetable, PGROUNDUP(newsz), npages, 1);
  }

  return newsz;
}

// Recursively free page-table pages.
// All leaf mappings must already have been removed.
void
freewalk(pagetable_t pagetable)
{
  // there are 2^9 = 512 PTEs in a page table.
  for(int i = 0; i < 512; i++){
    pte_t pte = pagetable[i];
    if((pte & PTE_V) && (pte & (PTE_R|PTE_W|PTE_X)) == 0){
      // this PTE points to a lower-level page table.
      uint64 child = PTE2PA(pte);
      freewalk((pagetable_t)child);
      pagetable[i] = 0;
    }
    else if(pte & PTE_V){
      panic("freewalk: leaf");
    }
  }
  kfree((void*)pagetable);
}

// Free user memory pages,
// then free page-table pages.
void
uvmfree(pagetable_t pagetable, uint64 sz)
{
  if(sz > 0)
    uvmunmap(pagetable, 0, PGROUNDUP(sz)/PGSIZE, 1);
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
  pte_t *new_pte;
  struct proc *p = myproc();

  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walk(old, i, 0)) == 0)
      panic("uvmcopy: pte should exist");

    #ifndef NONE
      if (p->pid > 2 && (*pte & PTE_PG)){
        if((new_pte = walk(new, i, 0)) == 0) {
          return -1;   
        }
        else {
          *new_pte = PTE_FLAGS(*pte);
          continue;
        }
      }
    #endif

    if((*pte & PTE_V) == 0)
      panic("uvmcopy: page not present");
    
    #ifdef NONE
      if (walkaddr(p->pagetable,i) == 0){
        if((new_pte = walk(new, i, 1)) == 0){
          return -1;
        }
        else {
          *new_pte = PTE_FLAGS(*pte);
          continue;
        }
      }
    #endif

    pa = PTE2PA(*pte);
    flags = PTE_FLAGS(*pte);
    if((mem = kalloc()) == 0)
      goto err;
    memmove(mem, (char*)pa, PGSIZE);
    if(mappages(new, i, PGSIZE, (uint64)mem, flags) != 0){
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
  if(pte == 0)
    panic("uvmclear");
  *pte &= ~PTE_U;
}

// Copy from kernel to user.
// Copy len bytes from src to virtual address dstva in a given page table.
// Return 0 on success, -1 on error.
int copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
  uint64 n, va0, pa0;

  while(len > 0){
    va0 = PGROUNDDOWN(dstva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (dstva - va0);
    if(n > len)
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

  while(len > 0){
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (srcva - va0);
    if(n > len)
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

  while(got_null == 0 && max > 0){
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (srcva - va0);
    if(n > max)
      n = max;

    char *p = (char *) (pa0 + (srcva - va0));
    while(n > 0){
      if(*p == '\0'){
        *dst = '\0';
        got_null = 1;
        break;
      }
      else {
        *dst = *p;
      }
      --n;
      --max;
      p++;
      dst++;
    }

    srcva = va0 + PGSIZE;
  }
  if(got_null){
    return 0;
  }
  else {
    return -1;
  }
}

int swap_pages(int va_on_swap, int va_on_ram, int in_swap){
  struct proc *p = myproc();  

  // get the pte of the page with virtual address "va_on_ram"
  pte_t *pte = walk(p->pagetable, va_on_ram, 0);
  if ((*pte & PTE_U) == 0)
    panic("trying to swap a page with a PGE_U bit turned-off");
  
  // find the page with virtual address "va_on_ram"
  struct page_data *page_data;
  int ram_arr_index = -1;
  for (page_data = p->ram; page_data < &p->ram[MAX_PSYC_PAGES]; page_data++){
    if (page_data->va == va_on_ram) {
      ram_arr_index = (int)(page_data - p->ram);
      break;
    }
  }
  if(ram_arr_index == -1)
    panic("error when trying to locate a page in RAM");

  // remove the page with virtual address "va_on_ram" from ram
  *pte &= ~(PTE_V);
  remove_page(&p->ram[ram_arr_index]);

  int swap_arr_index = 0;
  if (in_swap == 1){
    // move the page with virtual address "va_on_swap" into ram
    // and store in "swap_arr_index" the available space in p->swap
    pte = walk(p->pagetable, va_on_swap, 0);
    swap_arr_index = swapfile_to_ram(va_on_swap, pte, ram_arr_index);
  }
  else {
    // find the first available space in p->swap
    for (page_data = p->swap; page_data < &p->swap[MAX_PSYC_PAGES]; page_data++){
      if (page_data->used == 0) {
        swap_arr_index = (int)(page_data - p->swap);
        break;
      }
    }
  }

  // write the page with virtual address "va_on_ram" to swapfile
  int off = swap_arr_index * PGSIZE;
  char *page = kalloc();
  pte = walk(p->pagetable, va_on_ram, 0);
  memmove(page, (void *)PTE2PA(*pte), PGSIZE);

  if ((writeToSwapFile(p, page, off, PGSIZE)) == -1)
    panic("error when trying to write to swapfile");

  kfree(page);

  // write page data for the page with virtual address "va_on_ram" to p->swap
  page_data = &p->swap[swap_arr_index];
  page_data->va = va_on_ram;
  page_data->offset = off;
  page_data->used = 1;

  // remove the mapping of the page that was removed from the ram
  // and turn on it's PTE_PG bit
  *pte |= PTE_PG;   //turn on PTE_PG
  kfree((void*)PTE2PA(*pte));
  
  return ram_arr_index;
}

int get_NFUA_index() {
  struct proc *p = myproc();
  struct page_data *page;
  pte_t *pte;
  uint age = 0;
  int page_num = 0;
  int first = 1;

  for(int i=0; i < MAX_PSYC_PAGES; i++){
    page = &p->ram[i];
    pte = walk(p->pagetable, page->va, 0);

    if ((*pte & PTE_U) == 0){
      continue;
    }

    if (first || page->age < age){
      first = 0;
      age = page->age;
      page_num = i;
    }
  }
  return page_num;
}

int get_LAPA_index(){
  struct proc *p = myproc();
  struct page_data *page;
  pte_t *pte;
  uint age = 0;
  int page_num = 0;
  int first = 1;
  int ones = 0;

  for (int i = 0; i < MAX_PSYC_PAGES; i++){
    page = &p->ram[i];
    pte = walk(p->pagetable, page->va, 0);

    if ((*pte & PTE_U) == 0){
      continue;
    }

    int counter = 0;
    uint idx_mask = 1;

    while (idx_mask != 0) {
      if ((page->age & idx_mask) != 0) {
        counter++;
      }
      idx_mask <<= 1;
    }
    if (first || counter < ones ||(ones == counter && page->age < age)){
      first = 0;
      page_num = i;
      ones = counter;
      age = page->age;
    }
  }

  return page_num;
}

int get_SCFIFO_index(){
  struct proc *p = myproc();
  struct page_data *page;
  pte_t *pte;;
  int page_num = 0;
  int first = 1;
  int found = 0;
  uint time = 0;

  while(!found){
    // find the page that was least recently accessed
    for(page = p->ram; page < &p->ram[MAX_PSYC_PAGES]; page++){
      pte = walk(p->pagetable, page->va, 0);
      
      if (!(*pte & PTE_U))
        continue;
      
      if (first || page->fifo_time < time){
        first = 0;
        time = page->fifo_time;
        page_num = (int)(page - p->ram);
      }
    }

    page = &p->ram[page_num];
    pte = walk(p->pagetable, page->va, 0);
    // if that page was just accessed, try again
    if(*pte & PTE_A){
      first = 1;
      page_num = 0;
      *pte &= ~(PTE_A);
      page->fifo_time = p->fifo_counter++;
    }
    else {
      found = 1;
    }
  }

  return page_num;
}

int exchange_pages(uint64 va_on_swap, int in_swap){
    struct proc *p = myproc();
    int ram_arr_index = 0;

    #ifdef NFUA
      ram_arr_index = get_NFUA_index();
    #endif

    #ifdef LAPA
      ram_arr_index = get_LAPA_index();
    #endif

    #ifdef SCFIFO
      ram_arr_index = get_SCFIFO_index();
    #endif

    int va_on_ram = p->ram[ram_arr_index].va;
    return swap_pages(va_on_swap, va_on_ram, in_swap);
}

void handle_NONE(){
    pte_t *pte; 
    uint64 va = PGROUNDDOWN(r_stval());
    struct proc *p = myproc();

    if(va > MAXVA){
      exit(-1);
    }
    
    if ((pte = walk(p->pagetable, va, 0)) != 0){
      if ((*pte & PTE_V) && (walkaddr(p->pagetable,va) == 0)){
        if (uvmalloc(p->pagetable, va, va + PGSIZE) == 0)
          panic ("uvmalloc failure");
      }
      else {
        p->killed = 1;
      }
    }
    else {
      p->killed = 1;
    }
}

void handle_not_NONE(){
  pte_t *pte;
  uint64 va = PGROUNDDOWN(r_stval());
  struct proc *p = myproc();

  if ((pte = walk(p->pagetable, va, 0)) != 0){
    if (*pte & PTE_PG){
      swap(va, pte);
    }
    else {
      p->killed = 1;
    }       
  }
  else {
    p->killed = 1;
  }
}

void handle_page_fault(){
  #ifdef NONE
    handle_NONE();
  #endif

  #ifndef NONE
    handle_not_NONE();
  #endif
}

void swap(uint64 va, pte_t *pte){
  struct proc *p = myproc();

  if (count_pages(p->ram, 1) < MAX_PSYC_PAGES) { 

    // find first available space in p->ram
    int ram_arr_index = -1;
    struct page_data *page_data;
    for (page_data = p->ram; page_data < &p->ram[MAX_PSYC_PAGES]; page_data++) {
      if (page_data->used == 0) {
        ram_arr_index = (int)(page_data - p->ram);
        break;
      }
    }
    if (ram_arr_index < 0)
      panic ("error: too many pages in RAM");

    // swap page from swapfile into the available space in p->ram
    swapfile_to_ram(va, pte, ram_arr_index);
  }
  else {
    // swap between a page in RAM to a page in swapfile
    exchange_pages(va, 1);
  }
}