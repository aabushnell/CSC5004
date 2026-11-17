#include "memory.h"
#include "memory_map.h"

#include "vmm.h"

#include <err.h>
#include <linux/kvm.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

uint64_t new_page = ALLOC_AREA_GPA;
uint64_t max_page = MAX_PAGE;
int slot_id = 1;

uint64_t alloc_new_slot() {
  if (slot_id >= max_slots)
    errx(1, "OOM, exceeded max supported memory slots: %d", max_slots);

  printf("allocating a new slot!\n");

  uint8_t *host_mem = mmap(NULL, GUEST_INIT_MEM_SZ, PROT_READ | PROT_WRITE,
                   MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  if (host_mem == MAP_FAILED)
    err(1, "allocating guest memory, slot #%d", slot_id);

  uint64_t guest_addr = max_page;

  struct kvm_userspace_memory_region region = {
    .slot = slot_id,
    .guest_phys_addr = guest_addr,
    .memory_size = GUEST_INIT_MEM_SZ,
    .userspace_addr = (uint64_t)host_mem,
  };

  int ret = ioctl(vmfd, KVM_SET_USER_MEMORY_REGION, &region);
  if (ret == -1)
    err(1, "KVM_SET_USER_MEMORY_REGION");

  memory_slots[slot_id].guest_phys_base_addr = guest_addr;
  memory_slots[slot_id].host_virt_base_addr = (uint64_t)host_mem;
  memory_slots[slot_id].memory_size = GUEST_INIT_MEM_SZ;

  slot_id++;
  max_page += GUEST_INIT_MEM_SZ;

  return guest_addr;
}

uint8_t *get_hva(uint64_t gpa) {
  if (gpa >= max_page)
    errx(1, "unallocated address %lu", gpa);

  int idx = gpa / GUEST_INIT_MEM_SZ;
  uint64_t offset = gpa % GUEST_INIT_MEM_SZ;

  return (uint8_t *)memory_slots[idx].host_virt_base_addr + offset;
}

/* Get new guest-accessible pages.
 *
 * They are already mapped in the EPT via the KVM userspace memory ragion, but
 * not mapped in the guest's userspace.
 */
uint64_t get_pages(int nb_page) {
  if (PAGE_SIZE * nb_page > GUEST_INIT_MEM_SZ)
    errx(1, "cannot allocate more than a single slot (%d) of memory at a time", GUEST_INIT_MEM_SZ);

  if (new_page + PAGE_SIZE * nb_page > max_page)
    new_page = alloc_new_slot();

  uint64_t ret = new_page;
  new_page += PAGE_SIZE * nb_page;

  return ret;
}

/* Map pages in the guest's userspace.
 *
 * Also see memory_map_range.
 */
void memory_map(uint8_t *mem, uint64_t virt_addr, uint64_t phys_addr) {
  uint64_t *cr3, *pgd, *pud, *pmd, *pt, page;
  uint64_t *pgde_p, *pude_p, *pmde_p, *pte_p;

  cr3 = (uint64_t *)get_hva(CR3);
  pgde_p = &cr3[pgd_index(virt_addr)];
  if (!present(*pgde_p)) {
    page = get_pages(1);
    memset(get_hva(page), 0, 0x1000);
    *pgde_p = page | PRESENT | USER | RW;
  }

  pud = (uint64_t *)get_hva(ptep2pfn(pgde_p));
  pude_p = &pud[pud_index(virt_addr)];
  if (!present(*pude_p)) {
    page = get_pages(1);
    memset(get_hva(page), 0, 0x1000);
    *pude_p = page | PRESENT | USER | RW;
  }

  pmd = (uint64_t *)get_hva(ptep2pfn(pude_p));
  pmde_p = &pmd[pmd_index(virt_addr)];
  if (!present(*pmde_p)) {
    page = get_pages(1);
    memset(get_hva(page), 0, 0x1000);
    *pmde_p = page | PRESENT | USER | RW;
  }

  pt = (uint64_t *)get_hva(ptep2pfn(pmde_p));
  pte_p = &pt[pte_index(virt_addr)];
  if (!present(*pte_p)) {
    page = phys_addr;
    *pte_p = page | PRESENT | USER | RW;
  }
}
