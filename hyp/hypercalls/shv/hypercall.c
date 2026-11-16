#include "vmm.h"
#include <stdint.h>
#define _GNU_SOURCE

#include "hypercall.h"

#include "memory.h"

#include <linux/kvm.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>

#include <assert.h>
#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define __NR_pvmmap 511

/* Handle a system call from the guest app as a hypercall.
 *
 * Returns 1 if the guest should keep executing, 0 otherwise.
 */
int hypercall(struct kvm_regs *regs, uint8_t *memory, int vcpufd) {
  int fd, flags, ret;
  mode_t mode;
  size_t len, count;
  uint64_t addr;
  off_t offset;
  char *path;
  void *buf;

  switch (HC_NUM) {
  case __NR_exit: // exit
  case __NR_exit_group: // exit_group
    // exit(ARG1);
    printf("[HC] exit(%#llx)\n", ARG1);
    return 0;
  case __NR_arch_prctl: // arch_prctl
  case __NR_set_tid_address: // set_tid_address
    /* They are used by musl to set up threading: ignore. */
    fprintf(stderr, "ignored hypercall %lld\n", HC_NUM);
    break;
  case __NR_read:
    fd = (int)ARG1;
    buf = (void *)&memory[ARG2];
    count = (size_t)ARG3;
    printf("[HC] read(%d, %p, %lu)\n", fd, buf, count);

    count = read(fd, buf, count);
    if (count < 0)
      perror("[HC: ERROR] read failed");

    HC_RET = count;
    break;
  case __NR_write:
    fd = (int)ARG1;
    buf = (void *)&memory[ARG2];
    count = (size_t)ARG3;
    printf("[HC] write(%d, %p, %lu)\n", fd, buf, count);

    count = write(fd, buf, count);
    if (count < 0)
      perror("[HC: ERROR] write failed");

    HC_RET = count;
    break;
  case __NR_open:
    path = (char *)&memory[ARG1];
    flags = (int)ARG2;
    mode = (mode_t)ARG3;
    printf("[HC] open(%s, %d, %d)\n", path, flags, mode);

    fd = open(path, flags, mode);
    if (fd < 0)
      perror("[HC: ERROR] open failed");

    HC_RET = fd;
    break;
  case __NR_close:
    fd = (int)ARG1;
    printf("[HC] close(%d)\n", fd);

    ret = close(fd);
    if (ret < 0)
      perror("[HC: ERROR] close failed");

    HC_RET = ret;
    break;
  case __NR_pvmmap:
    addr = (uint64_t)ARG1;
    len = (size_t)ARG2;
    fd = (int)ARG3;
    offset = (off_t)ARG4;
    printf("[HC] pvmmap(%lu, %lu, %d, %lu)\n", addr, len, fd, offset);

    uint64_t addr_align = addr & 0x0FFF;
    uint64_t offset_align = offset & 0x0FFF;

    size_t nb_pages = (offset_align + len + 0x0FFF) / 0x1000;
    uint64_t alloc_len = nb_pages * 0x1000;
    uint64_t base_addr = get_pages(nb_pages);

    uint64_t ret_addr = base_addr + offset_align;

    if ((addr >= base_addr)
        && (addr < base_addr + 0x1000)
        && (addr_align == offset_align)) {
      ret_addr = addr;
      if (ret_addr + len > base_addr + alloc_len) {
        // bump an extra page
        get_pages(1);
      }
    }

    if (fd >= 0) {
      void *host_addr = mmap(&guest_mem[base_addr], alloc_len,
                             PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_FIXED,
                             fd, offset & ~(off_t)0x0FFF);
      if (host_addr == MAP_FAILED)
        err(1, "could not map backing file");
    }

    memory_map_range(guest_mem, base_addr, base_addr + len);

    HC_RET = ret_addr;
    break;
  default:
    fprintf(stderr, "unsupported hypercall %lld\n", HC_NUM);
    break;
  }

  /* Update guest registers after hypercall operation. */
  ioctl(vcpufd, KVM_SET_REGS, regs);

  return 1;
}
