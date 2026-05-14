Cách hoạt động của ALLOC_FREE_WRITE_READ
Bộ nhớ Thật gồm Ram và Swap
Cấu trúc
Ram với 2^14 bytes -> có 4 frame (vì mỗi frame sẽ là  PAGING64_PAGESZ=4096)
Swap với 2^14 bytes -> có 4 frame ( tương tự trên)
       RAM
+-------------------+
| Frame 0           | 0 -> 4095
+-------------------+
| Frame 1           | 4096 -> 8191
+-------------------+
| Frame 2           | 8192 -> 12287
+-------------------+
| Frame 3           | 12288 -> 16383
+-------------------+

free_fp_list: Frame 0 -> Frame 1 -> Frame 2 -> Frame 3 -> NULL
used_fp_list: NULL
rdmflg: 1 (phần biệt ram và swap)
struct framephy_struct
{
    addr_t fpn;
    struct framephy_struct *fp_next;

    /* Resereed for tracking allocated framed */
    struct mm_struct *owner;
};

struct memphy_struct
{
    /* Basic field of data and size */
    BYTE *storage;
    int maxsz;

    /* Sequential device fields */
    int rdmflg;
    int cursor;

    /* Management structure */
    struct framephy_struct *free_fp_list;
    struct framephy_struct *used_fp_list;
};

Các hàm
/* Lấy một frame trống */
int MEMPHY_get_freefp(struct memphy_struct *mp, addr_t *fpn);

/* Trả frame về danh sách frame trống */
int MEMPHY_put_freefp(struct memphy_struct *mp, addr_t fpn);

/* Đọc 1 byte từ bộ nhớ vật lý */
int MEMPHY_read(struct memphy_struct *mp, addr_t addr, BYTE *value);

/* Ghi 1 byte vào bộ nhớ vật lý */
int MEMPHY_write(struct memphy_struct *mp, addr_t addr, BYTE data);

/* In nội dung bộ nhớ vật lý */
int MEMPHY_dump(struct memphy_struct *mp);

/* Khởi tạo bộ nhớ vật lý */
int init_memphy(struct memphy_struct *mp, addr_t max_size, int randomflg);
Bộ nhớ Ảo
Cấu trúc
struct mm_struct
{
    struct vm_area_struct *mmap;

    /* Currently we support a fixed number of symbol */
    struct vm_rg_struct symrgtbl[PAGING_MAX_SYMTBL_SZ];

    /* list of free page */
    struct pgn_t *fifo_pgn;
};

struct vm_area_struct
{
    unsigned long vm_id;
    addr_t vm_start;
    addr_t vm_end;

    addr_t sbrk;
    /*
     * Derived field
     * unsigned long vm_limit = vm_end - vm_start
     */
    struct mm_struct *vm_mm;
    struct vm_rg_struct *vm_freerg_list;
    struct vm_area_struct *vm_next;
};

struct vm_rg_struct
{
    int vmaid;

    addr_t rg_start;
    addr_t rg_end;

    struct vm_rg_struct *rg_next;
};

vm_area_struct hiện tại BTL chỉ hỗ trợ 1 vùng nhớ nên mảng này sẽ luôn bằng 0 (nhưng trên thực tế nó quản lí nhiều loại như heap, stack, text, ...)
symrgtbl này như bảng lưu giá trị
int* a = new int[50];
int* b = new int[100];


-> a sẽ được lưu tại vị trí thanh ghi số 0 và b sẽ được được lưu tại vị trí thanh ghi số 1
| Register          | Biến | Vùng nhớ cấp phát | Kích thước |
|-------------------|------|-------------------|-------------|
| symrgtbl[0]       | a    | 0x1000 → 0x10C7  | int[50]     |
| symrgtbl[1]       | b    | 0x2000 → 0x218F  | int[100]    |
fifo_pgn này cơ chế FIFO nào vô trước ra trước
RAM có 3 frame

Load page 1 -> [1]
Load page 2 -> [1, 2]
Load page 3 -> [1, 2, 3]

Load page 4:
- Page 1 vào trước nên bị thay thế

FIFO = [2, 3, 4]

vm_start: địa chỉ bắt đầu vùng nhớ ảo, vm_end: địa chỉ kết thúc vùng nhớ ảo, sbrk: con trỏ dùng để mở rộng heap/cấp phát thêm bộ nhớ.
Ban đầu:
vm_start = 0 vm_end   = 0 sbrk = 0

Cấp phát 1 page (4096 bytes):
vm_start = 0 vm_end   = 4096 sbrk = 32
-> đã dùng 32 bytes trong page đầu tiên

Thêm biến mới kích thước 64 bytes:
sbrk = 96
-> vẫn còn đủ chỗ trong page hiện tại nên vm_end không đổi

Khi sbrk vượt quá 4096 -> cấp phát thêm 1 page
vm_end = 8192

vm_freerg_list là danh sách liên kết lưu các vùng nhớ trống sau khi free để có thể tái sử dụng lại.

Ví dụ:

int* a = new int[50];
a dùng vùng:
0x1000 -> 0x10C7

free(a)

vm_freerg_list:
+----------------------+
| 0x1000 -> 0x10C7     |
+----------------------+

Sau đó:

int* b = new int[20];

=> hệ thống có thể lấy lại vùng trống này
thay vì cấp phát page mới.
Page Table
Bảng trang (Page Table) là khái niệm trong quản lý bộ nhớ phân trang của hệ điều hành. Nó dùng để ánh xạ từ địa chỉ ảo (virtual address) mà chương trình sử dụng sang địa chỉ vật lý (physical address) trong RAM.
| Page Number | Physical Frame | Swap Offset | Location |
| ----------- | -------------- | ----------- | -------- |
| 0           | 5              | -           | RAM      |
| 1           | 9              | -           | RAM      |
| 2           | -              | 14          | SWAP     |
| 3           | 12             | -           | RAM      |

Mỗi virtual page được quản lý bởi một Page Table Entry (PTE) 32-bit. PTE dùng để ánh xạ địa chỉ ảo sang frame vật lý trong RAM hoặc vị trí trong swap.
| Bit(s) | Ý nghĩa                                         |
| ------ | ----------------------------------------------- |
| 0–12   | **Frame Page Number (FPN)** nếu page đang ở RAM |
| 13–14  | Reserved (0)                                    |
| 15–27  | User-defined numbering                          |
| 0–4    | Swap type nếu page đang ở SWAP                  |
| 5–25   | Swap offset nếu page đang ở SWAP                |
| 28     | Dirty bit                                       |
| 29     | Reserved                                        |
| 30     | Swapped bit                                     |
| 31     | Present bit                                     |

mô phỏng paging thì thường cần 3 thao tác trên page table entry:
int get_pd_from_address(addr_t addr, addr_t *pgd, addr_t *p4d, addr_t *pud, addr_t *pmd, addr_t *pt);
int get_pd_from_pagenum(addr_t pgn, addr_t *pgd, addr_t *p4d, addr_t *pud, addr_t *pmd, addr_t *pt);
int pte_set_fpn(struct pcb_t *caller, addr_t pgn, addr_t fpn);
int pte_set_swap(struct pcb_t *caller, addr_t pgn, int swptyp, addr_t swpoff);
uint32_t pte_get_entry(struct pcb_t *caller, addr_t pgn);
int pte_set_entry(struct pcb_t *caller, addr_t pgn, uint32_t pte_val);
FILE CÁC HÀM CÁC BẠN SẼ CODE Ở TASK 1 MEMORY
File sys_mem.c
int __sys_memmap(struct krnl_t *krnl, uint32_t pid, struct sc_regs *regs)
{
    // TOOO: vì chỉ truyền vào pid nên từ krnl->running_list ta duyệt vòng for để tìm PCB mong muốn

    switch (memop)
    {
       // CODE
    }

    return 0;
}

File mm-vm.c
int inc_vma_limit(struct pcb_t *caller, int vmaid, addr_t inc_sz)
{
  // TODO Align kích thước cần tăng PAGING64_PAGE_ALIGNSZ  và Tính vùng mới

  // TODO Kiểm tra overlap validate_overlap_vm_area(caller, vmaid, old_end, new_end)

  // Tạo region mô tả vùng mở rộng
  struct vm_rg_struct *newrg = malloc(sizeof(struct vm_rg_struct));

  //  Map virtual → physical: vm_map_range(caller, newrg->rg_start, newrg->rg_end, old_end, incnumpage, newrg)

  // Cập nhật giới hạn VMA: trước: [0, 8192) -> sau:[0, 12288) và Update free region list
  cur_vma->vm_end = new_end;
  struct vm_rg_struct *rg = cur_vma->vm_freerg_list;
  return 0;
}

File mm64.c
int pte_set_swap(struct pcb_t *caller, addr_t pgn, int swptyp, addr_t swpoff)
{
  struct mm_struct *mm = caller->krnl->mm;
  
  addr_t pgd_i, p4d_i, pud_i, pmd_i, pt_i;
  get_pd_from_pagenum(pgn, &pgd_i, &p4d_i, &pud_i, &pmd_i, &pt_i);
  
  // TODO Duyệt qua các tầng của bảng trang
  // PGD -> P4D -> PUD -> PMD -> PT
  // để tìm tới Page Table Entry (PTE)
  // ứng với địa chỉ ảo cần tra cứu

  CLRBIT(*pte, PAGING_PTE_PRESENT_MASK); // KHÔNG còn ở RAM -> chỗ này thầy bị nhầm 
  SETBIT(*pte, PAGING_PTE_SWAPPED_MASK); // đang ở swap
  SETVAL(*pte, swptyp, PAGING_PTE_SWPTYP_MASK, PAGING_PTE_SWPTYP_LOBIT);
  SETVAL(*pte, swpoff, PAGING_PTE_SWPOFF_MASK, PAGING_PTE_SWPOFF_LOBIT);
  return 0;
}
int pte_set_fpn(struct pcb_t *caller, addr_t pgn, addr_t fpn)
{
  struct mm_struct *mm = caller->krnl->mm;

  addr_t pgd_i, p4d_i, pud_i, pmd_i, pt_i;
  get_pd_from_pagenum(pgn, &pgd_i, &p4d_i, &pud_i, &pmd_i, &pt_i);
  // TODO giống thằng trên
  SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
  CLRBIT(*pte, PAGING_PTE_SWAPPED_MASK);
  SETVAL(*pte, fpn, PAGING_PTE_FPN_MASK, PAGING_PTE_FPN_LOBIT);
  return 0;
}
uint32_t pte_get_entry(struct pcb_t *caller, addr_t pgn)
{
  struct mm_struct *mm = caller->krnl->mm;

  addr_t pgd_i, p4d_i, pud_i, pmd_i, pt_i;
  get_pd_from_pagenum(pgn, &pgd_i, &p4d_i, &pud_i, &pmd_i, &pt_i);
  uint32_t pte = 0;
  // TODO giống thằng trên
  return pte;
}

addr_t vmap_page_range(struct pcb_t *caller, addr_t addr, int pgnum, struct framephy_struct *frames, struct vm_rg_struct *ret_rg)
{
  struct framephy_struct *fpit = frames;
  addr_t pgn;

  // TODO Cập nhật vùng nhớ đã map
  
  // TODO Map từng page ảo với frame vật lý tương ứng
  for (int pgit = 0; pgit < pgnum; pgit++)
  {
    pgn = /*Tính số thứ tự page từ địa chỉ bắt đầu*/

    // Ghi ánh xạ page -> frame vào page table: pte_set_fpn

    // Đưa vào FIFO (page replacement)

    fpit = fpit->fp_next;
  }

  return 0;
}
addr_t alloc_pages_range(struct pcb_t *caller, int req_pgnum, struct framephy_struct **frm_lst)
{
  addr_t fpn;
  int i;

  struct framephy_struct ||*head = NULL;
  struct framephy_struct *tail = NULL;

  for (i = 0; i < req_pgnum; i++)
  {
    // Swapping
    if (MEMPHY_get_freefp(caller->krnl->mram, &fpn) != 0)
    {
      // TODO chọn page bị đuổi -> find_victim_page
      addr_t victim_fpn = PAGING_PTE_FPN(victim_pte);

      // TODO MEMPHY_get_freefp find swpoff and swptyp 
      addr_t swp_off;
      int swptyp;

      // TODO update used_fp_list in swap

      // TODO _syscall SYSMEM_SWP_OP
      struct sc_regs regs;
      regs.a1 = SYSMEM_SWP_OP;
      regs.a2 = victim_fpn;
      regs.a3 = swp_off;

      // TODO pte_set_swap

      // TODO update MEMPHY_remove_usedfp ->  MEMPHY_put_freefp -> MEMPHY_get_freefp
    }

    // TODO update head and tail

    // TODO update caller->krnl->mram->used_fp_list
  }

  *frm_lst = head;
  return 0;
}
 
int init_mm(struct mm_struct *mm, struct pcb_t *caller)
{
  struct vm_area_struct *vma0 = malloc(sizeof(struct vm_area_struct));

  /* TODO init page table directory */
  // mm->pgd = ...
  // mm->p4d = ...
  // mm->pud = ...
  // mm->pmd = ...
  // mm->pt = ...

  /* By default the owner comes with at least one vma */
  vma0->vm_id = 0;
  vma0->vm_start = 0;
  vma0->vm_end = vma0->vm_start;
  vma0->sbrk = vma0->vm_start;
  struct vm_rg_struct *first_rg = init_vm_rg(vma0->vm_start, vma0->vm_end);
  enlist_vm_rg_node(&vma0->vm_freerg_list, first_rg);

  /* TODO update VMA0 next */
  // vma0->next = ...

  /* Point vma owner backward */
  // vma0->vm_mm = mm;

  /* TODO: update mmap */
  // mm->mmap = ...
  // mm->symrgtbl = ...
  // mm->kcpooltbl

  return 0;
}
File libmem.c
/*
 * Chèn một vùng nhớ trống vào danh sách free region,
 * giữ thứ tự theo địa chỉ và gộp các vùng liền kề nếu có
 */
int enlist_vm_freerg_list(struct mm_struct *mm, struct vm_rg_struct *rg_elmt)
{
  if (rg_elmt == NULL)
    return -1;

  // Kiểm tra vùng nhớ hợp lệ
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

  // TODO Gộp với vùng kế tiếp nếu liền kề

  // TODO Gộp với vùng phía trước nếu liền kề

  return 0;
}
int __alloc(struct pcb_t *caller, int vmaid, int rgid, addr_t size, addr_t *alloc_addr)
{
  pthread_mutex_lock(&mmvm_lock);
  struct vm_rg_struct rgnode;
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->krnl->mm, vmaid);
  int inc_sz = 0;

  if (get_free_vmrg_area(caller, vmaid, size, &rgnode) == 0)
  {
    caller->krnl->mm->symrgtbl[rgid].rg_start = rgnode.rg_start;
    caller->krnl->mm->symrgtbl[rgid].rg_end = rgnode.rg_end;

    *alloc_addr = rgnode.rg_start;
    // TODO UPDATE sbrk

    pthread_mutex_unlock(&mmvm_lock);
    return 0;
  }

  struct sc_regs regs;
  regs.a1 = SYSMEM_INC_OP;
  regs.a2 = vmaid;
  regs.a3 = size;
  // TODO SYSCALL 1 sys_memmap

  // TODO get_free_vmrg_area GIỐNG Ở TRÊN

  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}
int pg_setval(struct mm_struct *mm, int addr, BYTE value, struct pcb_t *caller)
{
  // TODO pgn và off trong PAGING64

  /* Get the page to MEMRAM, swap from MEMSWAP if needed */
  int fpn;
  if (pg_getpage(mm, pgn, &fpn, caller) != 0)
    return -1; /* invalid page access */

  int phyaddr = /*TODO use PAGING64_PAGESZ*/;
  // TODO  SYSCALL 17 sys_memmap

  return 0;
}
int pg_getval(struct mm_struct *mm, int addr, BYTE *data, struct pcb_t *caller)
{
  // TODO pgn và off trong PAGING64
  int fpn;

  if (pg_getpage(mm, pgn, &fpn, caller) != 0)
    return -1; /* invalid page access */

  int phyaddr = /*TODO use PAGING64_PAGESZ*/;

  // SYSCALL 17 sys_memmap giống trên mà này là đọc 

  *data = (BYTE)regs.a3;
  return 0;
}
int pg_getpage(struct mm_struct *mm, int pgn, int *fpn, struct pcb_t *caller)
{
  uint32_t pte = pte_get_entry(caller, pgn);
  // thực hiện swaping
  if (!PAGING_PAGE_PRESENT(pte))
  {
    addr_t vicpgn, swpfpn;
    /* Find victim page */
    if (find_victim_page(caller->krnl->mm, &vicpgn) == -1)
    {
      return -1;
    }
    
    // TODO tính vicpte và vicfpn vừa tìm được 

    // TOODO swpfpn và swptyp trong pte
    
    // TODO SYSCALL 1 sys_memmap SWP(vicfpn <--> swpfpn)

    // pte_set_swap(...);

    // pte_set_fpn(...);
    pte_set_fpn(caller, pgn, vicfpn);

    enlist_pgn_node(&caller->krnl->mm->fifo_pgn, pgn);
  }

  *fpn = PAGING_FPN(pte_get_entry(caller, pgn));

  return 0;
}
Cách Swapping
Các trường hợp dùng
RAM đầy khi cấp phát
Chọn 1 page đang nằm trong RAM để swap ra ngoài, lấy frame đó cấp cho page mới.
Read / Write vào page đang nằm ở swap
Nếu CPU truy cập page không còn trong RAM thì xảy ra page fault, hệ điều hành phải swap page đó trở lại RAM.
Cách hoạt động
Cấp phát
alloc
↓
RAM còn trống?
 ├─ Có  → cấp frame
 └─ Không → swap 1 victim page ra ngoài
          → lấy frame vừa giải phóng

Truy cập bộ nhớ
CPU đọc/ghi địa chỉ ảo
              ↓
Tra page table
present = 1 → truy cập trực tiếp trong RAM
present = 0, swapped = 1 → page fault
Swap-in
Nếu page fault:
Lấy frame trống
hoặc
swap 1 page khác ra ngoài
↓
copy page từ swap vào RAM
↓
cập nhật page table
 

Bộ nhớ User Space và Kernel Space
User space: vùng bộ nhớ dành cho các chương trình người dùng (process).
Process chỉ được truy cập vùng nhớ của chính nó, không được truy cập trực tiếp phần cứng hay bộ nhớ kernel để đảm bảo an toàn và cô lập.
Kernel space: vùng bộ nhớ dành cho hệ điều hành (kernel).
Chứa kernel code, page table, driver, kernel heap/cache,… và có quyền truy cập toàn bộ RAM, thiết bị phần cứng.
Khi user program cần thao tác đặc quyền (I/O, cấp phát trang, swap, đọc/ghi RAM…), nó sẽ gọi system call để chuyển từ user space sang kernel space.
Hình ảnh
alloc và kmalloc
alloc / malloc (user memory):
Cấp phát vùng nhớ cho process trong user space.
Hệ điều hành tìm vùng trống (vm_freerg_list), cập nhật symrgtbl, map page vào RAM nếu cần rồi trả về địa chỉ ảo cho chương trình sử dụng.
kmalloc (kernel malloc):
Cấp phát bộ nhớ trong kernel space cho hệ điều hành.
Kernel tự map page vào krnl_pgd, quản lý trực tiếp frame vật lý và dùng cho cache, driver, system data,…
Ví dụ
// USER MEMORY
alloc 100 1

→ Cấp phát 100 bytes cho user process
→ Lưu vùng nhớ vào thanh ghi/vùng số 1
Ví dụ:
write 65 1 0

→ Ghi giá trị 65 ('A') vào offset 0 của vùng nhớ số 1
// KERNEL MEMORY
kmalloc 200 2

→ Kernel cấp phát 200 bytes trong kernel space
→ Lưu vùng nhớ kernel vào vùng số 2
Ví dụ dùng:
copy_from_user 1 2 0 10

→ Copy 10 bytes từ user region 1
sang kernel region 2 bắt đầu từ offset 0
Cách phân chia
User Virtual Memory (User VM)
Dành cho chương trình người dùng.
Mỗi process có page table riêng (pgd) để ánh xạ:
Virtual Address → Physical Address

Ví dụ:
0x0000 ~ 0x7FFFFFFF

Kernel Virtual Memory (Kernel VM)
Dành cho kernel và driver.
Dùng page table kernel (krnl_pgd) và chỉ kernel được truy cập.
Ví dụ:
0x80000000 ~ 0xFFFFFFFF
Hàm vm_map_kernel
addr_t vm_map_kernel(struct pcb_t *caller,
                     int incpgnum,
                     struct vm_rg_struct *ret_rg)
{
  struct framephy_struct *frm_lst = NULL;

  /* 1. cấp phát frame liên tục */
  // TODO GỌI alloc_pages_range để cấp phát giả định luôn bé hơn 1 page, các bạn có thể sung 1 hàm phù hợp 2 page vì alloc_pages_range không hỗ trợ cấp phát liên tục 2 page
  struct framephy_struct *fpit = frm_lst;
  struct krnl_t *krnl = caller->krnl;

  /* 2. kernel base address*/
  const addr_t KERNEL_BASE = 0xfe00000000000000ULL;

  /* 3. tính địa chỉ kernel virtual bắt đầu */
  ret_rg->rg_start = KERNEL_BASE + ((addr_t)fpit->fpn * PAGING64_PAGESZ);
  ret_rg->rg_end = 0; // UPDATE Ở HÀM KHÁC

  /* 4. map từng frame vào page table kernel */
  // incpgnum=1 hiện đã giả lập chỉ cso 1 frame
  for (int i = 0; i < incpgnum; i++)
  {
    addr_t fpn = fpit->fpn;

    /* kernel virtual address của frame này */
    addr_t kva = ret_rg->rg_start;

    addr_t pgd_i, p4d_i, pud_i, pmd_i, pt_i;
    get_pd_from_pagenum(kva, &pgd_i, &p4d_i, &pud_i, &pmd_i, &pt_i);
    // TODO krnl->krnl_pgd giống set page bên mm->pdg

    *pte = 0;
    SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
    CLRBIT(*pte, PAGING_PTE_SWAPPED_MASK);

    SETVAL(*pte, fpn, PAGING_PTE_FPN_MASK, PAGING_PTE_FPN_LOBIT);

    /* next frame */
    fpit = fpit->fp_next;
  }

  return kva_start;
}
Các hàm file libmem.c còn lại
addr_t __kmalloc(struct pcb_t *caller, int vmaid, int rgid, addr_t size, addr_t *alloc_addr)
{
  // 1. align size to page size PAGING64_PAGE_ALIGNSZ and PAGING64_PAGESZ
  addr_t req_size = PAGING64_PAGE_ALIGNSZ(size);
  int incpgnum = req_size / PAGING64_PAGESZ;

  struct vm_rg_struct newrg;
  // TODO vm_map_kernel

  *alloc_addr = newrg.rg_start;
  // TODO symrgtbl .rg_start .rg_end .mode_bit
  return 0;
}

int libkmem_cache_pool_create(struct pcb_t *caller, uint32_t size, uint32_t align, uint32_t cache_pool_id)
{
  // TODO tương tự __kmalloc nhwung cập nhật krnl->mm->kcpooltbl
}

addr_t __kmem_cache_alloc(struct pcb_t *caller,
                          int vmaid,
                          int rgid,
                          int cache_pool_id,
                          addr_t *alloc_addr)
{
  struct krnl_t *krnl = caller->krnl;
  struct kcache_pool_struct *pool = /* TODO */;
  *alloc_addr = pool->storage;

  // TODO krnl->mm->symrgtbl[rgid]
  return 0;
}

int __read_user_mem(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE *data)
{
  // TODO pg_getval tương tự read thôi
}

int __write_user_mem(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE value)
{
  // TODO pg_setval tương tự write thôi
}
int __read_kernel_mem(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE *data)
{
  struct vm_rg_struct *currg = get_symrg_byid(caller->krnl->mm, rgid);

  addr_t kva = currg->rg_start + offset;

  int pgn = /* TODO */;
  int off = /* TODO */;

  addr_t pgd_i, p4d_i, pud_i, pmd_i, pt_i;
  // TODO get_pd_from_pagenum and krnl_pgd

  int fpn = /* TODO */;

  int phyaddr = /* TODO */;

  MEMPHY_read(caller->krnl->mram,
              phyaddr,
              data);
  return 0;
}

int __write_kernel_mem(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE value)
{
  // TODO giống đọc
  MEMPHY_write(caller->krnl->mram, phyaddr, value);
  return 0;
}

                    VIRTUAL MEMORY SPACE (64-bit)

0xFFFFFFFFFFFFFFFF  ┌──────────────────────────────────────┐
                    │             Kernel Space             │
                    │                                      │
                    │  KMALLOC                             │
                    │  KMEM_CACHE_CREATE                   │
                    │  KMEM_CACHE_ALLOC                    │
                    │  COPY_FROM_USER                      │
                    │  COPY_TO_USER                        │
                    │                                      │
                    │  kernel code / heap / cache          │
                    │  krnl_pgd / driver / kmem            │
                    │                                      │
0xFE00000000000000  ├──────────── KERNEL_BASE ─────────────┤
                    │                                      │
                    │              User Space              │
                    │                                      │
                    │  ALLOC                               │
                    │  FREE                                │
                    │  READ                                │
                    │  WRITE                               │
                    │  CALC                                │
                    │  SYSCALL                             │
                    │                                      │
                    │  stack / heap / mmap / code          │
                    │  mỗi process có pgd riêng            │
                    │                                      │
0x0000000000000000  └──────────────────────────────────────┘