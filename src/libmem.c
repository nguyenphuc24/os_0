/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* Caitoa release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

// #ifdef MM_PAGING
/*
 * System Library
 * Memory Module Library libmem.c 
 */

#include "string.h"
#include "mm.h"
#include "mm64.h"
#include "syscall.h"
#include "libmem.h"
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>

static pthread_mutex_t mmvm_lock = PTHREAD_MUTEX_INITIALIZER;


/*enlist_vm_freerg_list - add new rg to freerg_list
 *@mm: memory region
 *@rg_elmt: new region
 *
 */
int enlist_vm_freerg_list(struct mm_struct *mm, struct vm_rg_struct *rg_elmt)
{
  if (rg_elmt->rg_start >= rg_elmt->rg_end)
    return -1;

  struct vm_rg_struct *cur = mm->mmap->vm_freerg_list;
  struct vm_rg_struct *prev = NULL;

  /* Tìm vị trí chèn theo thứ tự tăng dần của địa chỉ bắt đầu */
  while (cur != NULL && cur->rg_start < rg_elmt->rg_start)
  {
    prev = cur;
    cur = cur->rg_next;
  }

  /* Chèn node mới vào giữa prev và cur */
  if (prev == NULL)
  {
    rg_elmt->rg_next = mm->mmap->vm_freerg_list;
    mm->mmap->vm_freerg_list = rg_elmt;
  }
  else
  {
    prev->rg_next = rg_elmt;
    rg_elmt->rg_next = cur;
  }

  /* Gộp với vùng kế tiếp nếu liền kề và cùng mode */
  if (rg_elmt->rg_next != NULL && rg_elmt->rg_end == rg_elmt->rg_next->rg_start && rg_elmt->mode_bit == rg_elmt->rg_next->mode_bit)
  {
    struct vm_rg_struct *next = rg_elmt->rg_next;
    rg_elmt->rg_end = next->rg_end;
    rg_elmt->rg_next = next->rg_next;
    free(next);
  }

  /* Gộp với vùng phía trước nếu liền kề và cùng mode */
  if (prev != NULL && prev->rg_end == rg_elmt->rg_start && prev->mode_bit == rg_elmt->mode_bit)
  {
    prev->rg_end = rg_elmt->rg_end;
    prev->rg_next = rg_elmt->rg_next;
    free(rg_elmt);
  }

  return 0;
}

/*get_symrg_byid - get mem region by region ID
 *@mm: memory region
 *@rgid: region ID act as symbol index of variable
 *
 */
struct vm_rg_struct *get_symrg_byid(struct mm_struct *mm, int rgid)
{
  if (rgid < 0 || rgid > PAGING_MAX_SYMTBL_SZ)
    return NULL;

  return &mm->symrgtbl[rgid];
}

/*__alloc - allocate a region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size
 *@alloc_addr: address of allocated memory region
 *
 */
int __alloc(struct pcb_t *caller, int vmaid, int rgid, addr_t size, addr_t *alloc_addr)
{
  /*Allocate at the toproof */
  pthread_mutex_lock(&mmvm_lock);
  struct vm_rg_struct rgnode;
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->krnl->mm, vmaid);
  int inc_sz=0;

  if (get_free_vmrg_area(caller, vmaid, size, &rgnode) == 0)
  {
    caller->krnl->mm->symrgtbl[rgid].rg_start = rgnode.rg_start;
    caller->krnl->mm->symrgtbl[rgid].rg_end = rgnode.rg_end;
    caller->krnl->mm->symrgtbl[rgid].mode_bit = 1;
 
    *alloc_addr = rgnode.rg_start;

    pthread_mutex_unlock(&mmvm_lock);
    return 0;
  }

  /* TODO get_free_vmrg_area FAILED handle the region management (Fig.6)*/

  /*Attempt to increate limit to get space */
#ifdef MM64
  inc_sz = (uint32_t)(size/(int)PAGING64_PAGESZ);
  inc_sz = inc_sz + 1;
#else
  inc_sz = PAGING_PAGE_ALIGNSZ(size);
#endif
  int old_sbrk;
  inc_sz = inc_sz + 1;

  old_sbrk = cur_vma->sbrk;

  /* TODO INCREASE THE LIMIT
   * SYSCALL 1 sys_memmap
   */
  struct sc_regs regs;
  regs.a1 = SYSMEM_INC_OP;
  regs.a2 = vmaid;
#ifdef MM64
  regs.a3 = size;
#else
  regs.a3 = PAGING_PAGE_ALIGNSZ(size);
#endif  
  _syscall(caller->krnl, caller->pid, 17, &regs); /* SYSCALL 17 sys_memmap */

  /*Successful increase limit */
  caller->krnl->mm->symrgtbl[rgid].rg_start = old_sbrk;
  caller->krnl->mm->symrgtbl[rgid].rg_end = old_sbrk + size;
  caller->krnl->mm->symrgtbl[rgid].mode_bit = 1;

  *alloc_addr = old_sbrk;

  pthread_mutex_unlock(&mmvm_lock);
  return 0;

}

/*__free - remove a region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size
 *
 */
int __free(struct pcb_t *caller, int vmaid, int rgid)
{
  pthread_mutex_lock(&mmvm_lock);

  if (rgid < 0 || rgid > PAGING_MAX_SYMTBL_SZ)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  /* TODO: Manage the collect freed region to freerg_list */
  struct vm_rg_struct *rgnode = get_symrg_byid(caller->krnl->mm, rgid);

  if (rgnode->rg_start == 0 && rgnode->rg_end == 0)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }
  struct vm_rg_struct *freerg_node = malloc(sizeof(struct vm_rg_struct));
  freerg_node->rg_start = rgnode->rg_start;
  freerg_node->rg_end = rgnode->rg_end;
  freerg_node->mode_bit = rgnode->mode_bit;
  freerg_node->rg_next = NULL;

  rgnode->rg_start = rgnode->rg_end = 0;
  rgnode->mode_bit = 0;
  rgnode->rg_next = NULL;

  /*enlist the obsoleted memory region */
  enlist_vm_freerg_list(caller->krnl->mm, freerg_node);

  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}

/*liballoc - PAGING-based allocate a region memory
 *@proc:  Process executing the instruction
 *@size: allocated size
 *@reg_index: memory region ID (used to identify variable in symbole table)
 */
int liballoc(struct pcb_t *proc, addr_t size, uint32_t reg_index)
{
  addr_t  addr;
  int val = __alloc(proc, 0, reg_index, size, &addr);
  if (val == -1)
  {
    return -1;
  }
#ifdef IODUMP
  printf("liballoc:%d\n", __LINE__);
#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1); // print max TBL
#endif
#endif

  /* By default using vmaid = 0 */
  return val;
}

/*libfree - PAGING-based free a region memory
 *@proc: Process executing the instruction
 *@size: allocated size
 *@reg_index: memory region ID (used to identify variable in symbole table)
 */

int libfree(struct pcb_t *proc, uint32_t reg_index)
{
  int val = __free(proc, 0, reg_index);
  if (val == -1)
  {
    return -1;
  }
printf("libfree:%d\n",__LINE__);
#ifdef IODUMP
#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1); // print max TBL
#endif
#endif
  return 0;//val;
}

/*pg_getpage - get the page in ram
 *@mm: memory region
 *@pagenum: PGN
 *@framenum: return FPN
 *@caller: caller
 *
 */
int pg_getpage(struct mm_struct *mm, int pgn, int *fpn, struct pcb_t *caller)
{

  uint32_t pte = pte_get_entry(caller, pgn);

  if (!PAGING_PAGE_PRESENT(pte))
  { /* Page is not online, make it actively living */
    addr_t vicpgn, swpfpn;
    addr_t vicfpn;
    uint32_t vicpte;

    /* TODO Initialize the target frame storing our variable */
    addr_t tgtfpn = PAGING_SWP(pte);

    /* TODO: Play with your paging theory here */
    /* Find victim page */
    if (find_victim_page(caller->krnl->mm, &vicpgn) == -1)
    {
      return -1;
    }

    vicpte = pte_get_entry(caller, vicpgn);
    vicfpn = PAGING_FPN(vicpte);

    /* Get free frame in MEMSWP */
    if (MEMPHY_get_freefp(caller->krnl->active_mswp, &swpfpn) == -1)
    {
      return -1;
    }

    /* TODO: Implement swap frame from MEMRAM to MEMSWP and vice versa*/

    /* TODO copy victim frame to swap 
     * SWP(vicfpn <--> swpfpn)
     * SYSCALL 1 sys_memmap
     */
    struct sc_regs regs;
    regs.a1 = SYSMEM_SWP_OP;
    regs.a2 = vicfpn;
    regs.a3 = swpfpn;
    _syscall(caller->krnl, caller->pid, 17, &regs);

    __swap_cp_page(caller->krnl->active_mswp, tgtfpn, caller->krnl->mram, vicfpn);

    /* Update page table */
    pte_set_swap(caller, vicpgn, 0, swpfpn);

    /* Update its online status of the target page */
    pte_set_fpn(caller, pgn, vicfpn);

    enlist_pgn_node(&caller->krnl->mm->fifo_pgn, pgn);
  }

  *fpn = PAGING_FPN(pte_get_entry(caller,pgn));

  return 0;
}

/*pg_getval - read value at given offset
 *@mm: memory region
 *@addr: virtual address to acess
 *@value: value
 *
 */
int pg_getval(struct mm_struct *mm, int addr, BYTE *data, struct pcb_t *caller)
{
  int pgn = PAGING_PGN(addr);
  int off = PAGING_OFFST(addr);
  int fpn;

  if (pg_getpage(mm, pgn, &fpn, caller) != 0)
    return -1; /* invalid page access */

  int phyaddr;
#ifdef MM64
  phyaddr = fpn * PAGING64_PAGESZ + off;
#else
  phyaddr = fpn * PAGING_PAGESZ + off;
#endif

  MEMPHY_read(caller->krnl->mram, phyaddr, data);

  return 0;
}

/*pg_setval - write value to given offset
 *@mm: memory region
 *@addr: virtual address to acess
 *@value: value
 *
 */
int pg_setval(struct mm_struct *mm, int addr, BYTE value, struct pcb_t *caller)
{
  int pgn = PAGING_PGN(addr);
  int off = PAGING_OFFST(addr);
  int fpn;

  /* Get the page to MEMRAM, swap from MEMSWAP if needed */
  if (pg_getpage(mm, pgn, &fpn, caller) != 0)
    return -1; /* invalid page access */

  int phyaddr;
#ifdef MM64
  phyaddr = fpn * PAGING64_PAGESZ + off;
#else
  phyaddr = fpn * PAGING_PAGESZ + off;
#endif

  MEMPHY_write(caller->krnl->mram, phyaddr, value);

  return 0;
}

/*__read - read value in region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@offset: offset to acess in memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size
 *
 */
int __read(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE *data)
{
  pthread_mutex_lock(&mmvm_lock);
  struct vm_rg_struct *currg = get_symrg_byid(caller->krnl->mm, rgid);

//struct vm_area_struct *cur_vma = get_vma_by_num(caller->krnl->mm, vmaid);

  /* TODO Invalid memory identify */
  if (currg == NULL || currg->mode_bit != 1)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  pg_getval(caller->krnl->mm, currg->rg_start + offset, data, caller);

  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}

/*libread - PAGING-based read a region memory */
int libread(
    struct pcb_t *proc, // Process executing the instruction
    uint32_t source,    // Index of source register
    addr_t offset,    // Source address = [source] + [offset]
    uint32_t* destination)
{
  BYTE data;
printf("libread:%d\n",__LINE__);
  int val = __read(proc, 0, source, offset, &data);

  *destination = data;
#ifdef IODUMP
#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1); // print max TBL
#endif
#endif

  return val;
}

/*__write - write a region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@offset: offset to acess in memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size
 *
 */
int __write(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE value)
{
  pthread_mutex_lock(&mmvm_lock);
  struct vm_rg_struct *currg = get_symrg_byid(caller->krnl->mm, rgid);

  struct vm_area_struct *cur_vma = get_vma_by_num(caller->krnl->mm, vmaid);

  if (currg == NULL || cur_vma == NULL || currg->mode_bit != 1) /* Invalid memory identify */
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  pg_setval(caller->krnl->mm, currg->rg_start + offset, value, caller);

  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}

/*libwrite - PAGING-based write a region memory */
int libwrite(
    struct pcb_t *proc,   // Process executing the instruction
    BYTE data,            // Data to be wrttien into memory
    uint32_t destination, // Index of destination register
    addr_t offset)
{
  int val = __write(proc, 0, destination, offset, data);
  if (val == -1)
  {
    return -1;
  }
#ifdef IODUMP
  printf("libwrite:%d\n", __LINE__);
#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1); // print max TBL
#endif
#endif

  return val;
}


/*libkmem_malloc- alloc region memory in kmem
 *@caller: caller
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: memory size
 */

int libkmem_malloc(struct pcb_t * caller, uint32_t size, uint32_t reg_index)
{
  /* TODO: provide OS level management
   *       and forward the request to helper
   */
addr_t  addr;
int val = __kmalloc(caller, -1, reg_index, size, &addr);

  /* TODO: provide OS kmem allocation validation
   */
#ifdef IODUMP
  printf("libkmem_malloc:%d\n", __LINE__);
#ifdef PAGETBL_DUMP
  print_pgtbl(caller, 0, -1); // print max TBL
#endif
#endif

  return val;
}


/*kmalloc - alloc region memory in kmem
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: memory size
 *@alloc_addr: allocated address
 */
addr_t __kmalloc(struct pcb_t *caller, int vmaid, int rgid, addr_t size, addr_t *alloc_addr)
{
  /* TODO: provide OS kernel memory allocation
   *       update krnl_pgd for OS kernel level management */

  struct krnl_t *krnl = caller->krnl;
  struct vm_rg_struct ret_rg;
#ifdef MM64
  int pgnum = (size + PAGING64_PAGESZ - 1) / PAGING64_PAGESZ;
#else
  int pgnum = (size + PAGING_PAGESZ - 1) / PAGING_PAGESZ;
#endif
  
  if (vm_map_kernel(caller, KERNEL_BASE, KERNEL_BASE + (pgnum * PAGING64_PAGESZ), KERNEL_BASE, pgnum, &ret_rg) < 0)
    return -1;
  
  if (rgid >= 0 && rgid < PAGING_MAX_SYMTBL_SZ)
  {
    krnl->mm->symrgtbl[rgid].rg_start = ret_rg.rg_start;
    krnl->mm->symrgtbl[rgid].rg_end = ret_rg.rg_end;
    krnl->mm->symrgtbl[rgid].mode_bit = 0;
  }
  
  *alloc_addr = ret_rg.rg_start;
  return ret_rg.rg_start;

}

/*libkmem_cache_pool_create - create cache pool in kmem
 *@caller: caller
 *@size: memory size
 *@align: alignment size of each cache slot (identical cache slot size)
 *@cache_pool_id: cache pool ID
 */
int libkmem_cache_pool_create(struct pcb_t *caller, uint32_t size, uint32_t align, uint32_t cache_pool_id)
{
  /* TODO: provide OS level management */

  struct krnl_t *krnl = caller->krnl;
  struct vm_rg_struct ret_rg;
#ifdef MM64
  int pgnum = (size + PAGING64_PAGESZ - 1) / PAGING64_PAGESZ;
#else
  int pgnum = (size + PAGING_PAGESZ - 1) / PAGING_PAGESZ;
#endif
  
  if (vm_map_kernel(caller, KERNEL_BASE, KERNEL_BASE + (pgnum * PAGING64_PAGESZ), KERNEL_BASE, pgnum, &ret_rg) < 0)
    return -1;
  
  if (cache_pool_id < PAGING_MAX_SYMTBL_SZ)
  {
    krnl->mm->symrgtbl[cache_pool_id].rg_start = ret_rg.rg_start;
    krnl->mm->symrgtbl[cache_pool_id].rg_end = ret_rg.rg_end;
    krnl->mm->symrgtbl[cache_pool_id].mode_bit = 0;
  }
#ifdef IODUMP
  printf("libkmem_cache_pool_create:%d\n", __LINE__);
#ifdef PAGETBL_DUMP
  print_pgtbl(caller, 0, -1); // print max TBL
#endif
#endif

  return 0;
}

/*libkmem_cache_alloc - allocate cache slot in cache pool, cache slot has identical size
 * the allocated size is embedded in pool management mechanism
 *@caller: caller
 *@cache_pool_id: cache pool ID
 *@reg_index: memory region index
 */
int libkmem_cache_alloc(struct pcb_t *proc, uint32_t cache_pool_id, uint32_t reg_index)
{
  /* TODO: provide OS level management
   *       and forward the request to helper
   */
  addr_t addr = __kmem_cache_alloc(proc, -1, reg_index, cache_pool_id, &addr);

  //krnl->kcpooltbl...
  //krnl->krnl_pgd ...
#ifdef IODUMP
  printf("libkmem_cache_alloc:%d\n", __LINE__);
#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1); // print max TBL
#endif
#endif

  return 0;
}

/*kmem_cache_alloc - alloc region memory in kmem cache
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@cache_pool_id: cached pool ID
 *@alloc_addr: allocated address
 */

addr_t __kmem_cache_alloc(struct pcb_t *caller, int vmaid, int rgid, int cache_pool_id, addr_t *alloc_addr)
{
  /* TODO: provide OS level management */
  struct krnl_t *krnl = caller->krnl;
  
  if (cache_pool_id < 0 || cache_pool_id >= PAGING_MAX_SYMTBL_SZ)
    return -1;
  if (rgid < 0 || rgid >= PAGING_MAX_SYMTBL_SZ)
    return -1;
  
  struct vm_rg_struct *cache_pool = &krnl->mm->symrgtbl[cache_pool_id];
  if (cache_pool->rg_start == 0 || cache_pool->rg_end == 0)
    return -1;
  
  krnl->mm->symrgtbl[rgid].rg_start = cache_pool->rg_start;
  krnl->mm->symrgtbl[rgid].rg_end = cache_pool->rg_end;
  krnl->mm->symrgtbl[rgid].mode_bit = 0;
  
  *alloc_addr = cache_pool->rg_start;
  return cache_pool->rg_start;

}


int libkmem_copy_from_user(struct pcb_t *caller, uint32_t source, uint32_t destination, uint32_t offset, uint32_t size)
{
  /* TODO: provide OS level management kmem
   */
  BYTE data;
  int i;
  for (i = 0; i < size; i++)
  {
    if (__read_user_mem(caller, 0, source, offset + i, &data) < 0)
      return -1;
    if (__write_kernel_mem(caller, 0, destination, i, data) < 0)
      return -1;
  }
#ifdef IODUMP
  printf("libkmem_copy_from_user:%d\n", __LINE__);
#ifdef PAGETBL_DUMP
  print_pgtbl(caller, 0, -1); // print max TBL
#endif
#endif

  return 0;
}

int libkmem_copy_to_user(struct pcb_t *caller, uint32_t source, uint32_t destination, uint32_t offset, uint32_t size)
{
  /* TODO: provide OS level management kmem
   */
  BYTE data;
  int i;
  for (i = 0; i < size; i++)
  {
    if (__read_kernel_mem(caller, 0, source, i, &data) < 0)
      return -1;
    if (__write_user_mem(caller, 0, destination, offset + i, data) < 0)
      return -1;
  }
#ifdef IODUMP
  printf("libkmem_copy_to_user:%d\n", __LINE__);
#ifdef PAGETBL_DUMP
  print_pgtbl(caller, 0, -1); // print max TBL
#endif
#endif

  return 0;
}


/*__read_kernel_mem - read value in kernel region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@offset: offset to acess in memory region
 *@value: data value
 */
int __read_kernel_mem(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE *data)
{
  struct vm_rg_struct *krg = &caller->krnl->mm->symrgtbl[rgid];
  if (krg->rg_start == 0 || krg->rg_end == 0 || krg->mode_bit != 0)
    return -1;
  
  addr_t read_addr = krg->rg_start + offset;
  if (read_addr >= krg->rg_end)
    return -1;
  
  addr_t pgn = PAGING_PGN(read_addr);
  int off = PAGING_OFFST(read_addr);
  addr_t pgd_i, p4d_i, pud_i, pmd_i, pt_i;
  
#ifdef MM64
  get_pd_from_pagenum(pgn, &pgd_i, &p4d_i, &pud_i, &pmd_i, &pt_i);
  addr_t *p4d = (addr_t *)caller->krnl->krnl_pgd[pgd_i];
  if (!p4d) return -1;
  addr_t *pud = (addr_t *)p4d[p4d_i];
  if (!pud) return -1;
  addr_t *pmd = (addr_t *)pud[pud_i];
  if (!pmd) return -1;
  addr_t *pt = (addr_t *)pmd[pmd_i];
  if (!pt) return -1;
  addr_t pte = pt[pt_i];
#else
  addr_t pte = caller->krnl->krnl_pgd[pgn];
#endif

  if (!PAGING_PAGE_PRESENT(pte)) return -1;
  int fpn = PAGING_FPN(pte);
  int phyaddr;
#ifdef MM64
  phyaddr = fpn * PAGING64_PAGESZ + off;
#else
  phyaddr = fpn * PAGING_PAGESZ + off;
#endif

  if (phyaddr < 0 || phyaddr >= caller->krnl->mram->maxsz)
    return -1;
  
  MEMPHY_read(caller->krnl->mram, phyaddr, data);

  return 0;
}

/*__write_kernel_mem - write a kernel region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@offset: offset to acess in memory region
 *@value: data value
 */
int __write_kernel_mem(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE value)
{
  struct vm_rg_struct *krg = &caller->krnl->mm->symrgtbl[rgid];
  if (krg->rg_start == 0 || krg->rg_end == 0 || krg->mode_bit != 0)
    return -1;
  
  addr_t write_addr = krg->rg_start + offset;
  if (write_addr >= krg->rg_end)
    return -1;
  
  addr_t pgn = PAGING_PGN(write_addr);
  int off = PAGING_OFFST(write_addr);
  addr_t pgd_i, p4d_i, pud_i, pmd_i, pt_i;

#ifdef MM64
  get_pd_from_pagenum(pgn, &pgd_i, &p4d_i, &pud_i, &pmd_i, &pt_i);
  addr_t *p4d = (addr_t *)caller->krnl->krnl_pgd[pgd_i];
  if (!p4d) return -1;
  addr_t *pud = (addr_t *)p4d[p4d_i];
  if (!pud) return -1;
  addr_t *pmd = (addr_t *)pud[pud_i];
  if (!pmd) return -1;
  addr_t *pt = (addr_t *)pmd[pmd_i];
  if (!pt) return -1;
  addr_t pte = pt[pt_i];
#else
  addr_t pte = caller->krnl->krnl_pgd[pgn];
#endif

  if (!PAGING_PAGE_PRESENT(pte)) return -1;
  int fpn = PAGING_FPN(pte);
  int phyaddr;
#ifdef MM64
  phyaddr = fpn * PAGING64_PAGESZ + off;
#else
  phyaddr = fpn * PAGING_PAGESZ + off;
#endif

  if (phyaddr < 0 || phyaddr >= caller->krnl->mram->maxsz)
    return -1;
  
  MEMPHY_write(caller->krnl->mram, phyaddr, value);

  return 0;
}

/*__read_user_mem - read value in user region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@offset: offset to acess in memory region
 *@value: data value
 */
int __read_user_mem(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE *data)
{
  /* TODO: provide OS level management user memory access */
  struct vm_rg_struct *urg = &caller->krnl->mm->symrgtbl[rgid];
  if (urg->rg_start == 0 || urg->rg_end == 0 || urg->mode_bit != 1)
    return -1;
  
  addr_t read_addr = urg->rg_start + offset;
  if (read_addr >= urg->rg_end)
    return -1;
  
  return pg_getval(caller->krnl->mm, read_addr, data, caller);
}


/*__write_user_mem - write a user region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@offset: offset to acess in memory region
 *@value: data value
 */
int __write_user_mem(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE value)
{
  /* TODO: provide OS level management user memory access */
  struct vm_rg_struct *urg = &caller->krnl->mm->symrgtbl[rgid];
  if (urg->rg_start == 0 || urg->rg_end == 0 || urg->mode_bit != 1)
    return -1;
  
  addr_t write_addr = urg->rg_start + offset;
  if (write_addr >= urg->rg_end)
    return -1;
  
  return pg_setval(caller->krnl->mm, write_addr, value, caller);
}


/*free_pcb_memphy - collect all memphy of pcb
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@incpgnum: number of page
 */
int free_pcb_memph(struct pcb_t *caller)
{
  pthread_mutex_lock(&mmvm_lock);
  int pagenum, fpn;
  uint32_t pte;

  for (pagenum = 0; pagenum < PAGING_MAX_PGN; pagenum++)
  {
    pte = caller->krnl->mm->pgd[pagenum];

    if (PAGING_PAGE_PRESENT(pte))
    {
      fpn = PAGING_FPN(pte);
      MEMPHY_put_freefp(caller->krnl->mram, fpn);
    }
    else
    {
      fpn = PAGING_SWP(pte);
      MEMPHY_put_freefp(caller->krnl->active_mswp, fpn);
    }
  }

  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}


/*find_victim_page - find victim page
 *@caller: caller
 *@pgn: return page number
 *
 */
int find_victim_page(struct mm_struct *mm, addr_t *retpgn)
{
  struct pgn_t *pg = mm->fifo_pgn;

  /* TODO: Implement the theorical mechanism to find the victim page */
  if (!pg)
  {
    return -1;
  }
  if (pg->pg_next == NULL)
  {
    *retpgn = pg->pgn;
    mm->fifo_pgn = NULL;
    free(pg);
    return 0;
  }
  struct pgn_t *prev = NULL;
  while (pg->pg_next)
  {
    prev = pg;
    pg = pg->pg_next;
  }
  *retpgn = pg->pgn;
  if (prev)
    prev->pg_next = NULL;
  else
    mm->fifo_pgn = NULL;

  free(pg);

  return 0;
}

/*get_free_vmrg_area - get a free vm region
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@size: allocated size
 *
 */
int get_free_vmrg_area(struct pcb_t *caller, int vmaid, int size, struct vm_rg_struct *newrg)
{
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->krnl->mm, vmaid);

  struct vm_rg_struct *rgit = cur_vma->vm_freerg_list;

  if (rgit == NULL)
    return -1;

  /* Probe unintialized newrg */
  newrg->rg_start = newrg->rg_end = -1;
  newrg->mode_bit = 0;

  /* Traverse on list of free vm region to find a fit space */
  while (rgit != NULL)
  {
    if (rgit->rg_start + size <= rgit->rg_end)
    { /* Current region has enough space */
      newrg->rg_start = rgit->rg_start;
      newrg->rg_end = rgit->rg_start + size;
      newrg->mode_bit = rgit->mode_bit;

      /* Update left space in chosen region */
      if (rgit->rg_start + size < rgit->rg_end)
      {
        rgit->rg_start = rgit->rg_start + size;
      }
      else
      { /*Use up all space, remove current node */
        /*Clone next rg node */
        struct vm_rg_struct *nextrg = rgit->rg_next;

        /*Cloning */
        if (nextrg != NULL)
        {
          rgit->rg_start = nextrg->rg_start;
          rgit->rg_end = nextrg->rg_end;

          rgit->rg_next = nextrg->rg_next;

          free(nextrg);
        }
        else
        {                                /*End of free list */
          rgit->rg_start = rgit->rg_end; // dummy, size 0 region
          rgit->rg_next = NULL;
        }
      }
      break;
    }
    else
    {
      rgit = rgit->rg_next; // Traverse next rg
    }
  }

  if (newrg->rg_start == -1) // new region not found
    return -1;

  return 0;
}

// #endif
