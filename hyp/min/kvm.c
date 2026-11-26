#include <linux/kvm.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <err.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <string.h>
#include <stdint.h>

static const uint8_t guest_code[] = {
    0xba, 0xf8, 0x03, /* mov $0x3f8, %dx */
    0x00, 0xd8,       /* add %bl, %al */
    0x04, '0',        /* add $'0', %al */
    0xee,             /* out %al, (%dx) */
    0xb0, '\n',       /* mov $'\n', %al */
    0xee,             /* out %al, (%dx) */
    0xf4,             /* hlt */
};

int main() {
  int kvmfd = open("/dev/kvm", O_RDWR);
  if (kvmfd < 0) {
    err(EXIT_FAILURE, "failed opening file %s", "open /dev/kvm");
  }

  int api_ver = ioctl(kvmfd, KVM_GET_API_VERSION, 0);
  if (api_ver < 0){
    err(EXIT_FAILURE, "failed calling KVM_GET_API_VERSION");
  }
  if (api_ver != 12) {
    err(EXIT_FAILURE, "expected KVM_GET_API_VERSION == 12, got %d", api_ver);
  }

  int vmfd = ioctl(kvmfd, KVM_CREATE_VM, 0);
  if (vmfd < 0){
    err(EXIT_FAILURE, "failed calling KVM_CREATE_VM");
  }

  void *guest_mem = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (guest_mem == MAP_FAILED) {
    err(EXIT_FAILURE, "failed allocating memory");
  }
  memcpy(guest_mem, guest_code, 4096);

  struct kvm_userspace_memory_region region = {
      .slot = 0,
      .guest_phys_addr = 0x1000,
      .memory_size = 0x1000,
      .userspace_addr = (uint64_t)guest_mem,
  };
  int mem_region = ioctl(vmfd, KVM_SET_USER_MEMORY_REGION, &region);
  if (mem_region < 0){
    err(EXIT_FAILURE, "failed calling KVM_SET_USER_MEMORY_REGION");
  }

  int vcpufd = ioctl(vmfd, KVM_CREATE_VCPU, 0);
  if (vcpufd < 0){
    err(EXIT_FAILURE, "failed calling KVM_CREATE_VCPU");
  }

  int kvm_run_size = ioctl(kvmfd, KVM_GET_VCPU_MMAP_SIZE, 0);
  if (kvm_run_size < 0) {
    err(EXIT_FAILURE, "failed calling KVM_GET_VCPU_MMAP_SIZE");
  }
  if (kvm_run_size < sizeof(struct kvm_run)) {
    err(EXIT_FAILURE, "wrong size of kvm_run");
  }
  printf("size of kvm_run: %d\n", kvm_run_size);

  struct kvm_run *run =
      mmap(NULL, kvm_run_size, PROT_READ | PROT_WRITE, MAP_SHARED, vcpufd, 0);
  if (run == MAP_FAILED) {
    err(EXIT_FAILURE, "mmap vcpu fd failed");
  }

  struct kvm_sregs vcpu_regs;
  int sregs_ret = ioctl(vcpufd, KVM_GET_SREGS, &vcpu_regs);
  if (sregs_ret < 0) {
    err(EXIT_FAILURE, "failed calling KVM_GET_SREGS");
  }
  vcpu_regs.cs.base = 0;
  vcpu_regs.cs.selector = 0;
  sregs_ret = ioctl(vcpufd, KVM_SET_SREGS, &vcpu_regs);
  if (sregs_ret < 0) {
    err(EXIT_FAILURE, "failed calling KVM_SET_SREGS");
  }

  struct kvm_regs vcpu_regs_gen = {0};
  vcpu_regs_gen.rip = 0x1000;
  vcpu_regs_gen.rax = 0x0000;
  vcpu_regs_gen.rbx = 0x0001;
  vcpu_regs_gen.rflags = 0x2;
  int regs_ret = ioctl(vcpufd, KVM_SET_REGS, &vcpu_regs_gen);
  if (regs_ret < 0) {
    err(EXIT_FAILURE, "failed calling KVM_SET_REGS");
  }

  while (1) {
    int run_ret = ioctl(vcpufd, KVM_RUN, 0);
    if (run_ret < 0) {
      err(EXIT_FAILURE, "failed calling KVM_RUN");
    }

    switch (run->exit_reason) {
      case KVM_EXIT_HLT:
        printf("got halted!\n");
        return 0;
      case KVM_EXIT_IO:
        if (run->io.direction == KVM_EXIT_IO_OUT && run->io.port == 0x3f8) {
          printf("got output!\n");
          char *data = (char *)run + run->io.data_offset;
          fwrite(data, run->io.size, run->io.count, stdout);
        } else if (run->io.direction == KVM_EXIT_IO_IN) {
          char *data = (char *)run + run->io.data_offset;
          uint32_t total_bytes = run->io.size * run->io.count;

          if (run->io.port == 0x3f8) {
            for (uint32_t i = 0; i < run->io.count; i++) {
              int ch = getchar();
              if (ch == EOF) {
                ch = 0;
              }
              memset(data + (i * run->io.size), 0, run->io.size);
              memcpy(data + (i * run->io.size), &ch,
                      (run->io.size < sizeof(int)) ? run->io.size : sizeof(int));
            }
          } else {
            memset(data, 0, total_bytes);
          }
        }
        break;
      case KVM_EXIT_FAIL_ENTRY:
        err(EXIT_FAILURE, "KVM_EXIT_FAIL_ENTRY");
        break;
      case KVM_EXIT_INTERNAL_ERROR:
        err(EXIT_FAILURE, "KVM_EXIT_INTERNAL_ERROR");
        break;
      default:
        err(EXIT_FAILURE, "Unhandled KVM exit reason: %d", run->exit_reason);
    }
  }
}
