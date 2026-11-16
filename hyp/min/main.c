#include <linux/kvm.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <err.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <string.h>
#include <stdbool.h>

static const uint8_t guest_code[] = {
 0xba, 0xf8, 0x03,  /* mov $0x3f8, %dx */
 0x00, 0xd8,        /* add %bl, %al */
 0x04, '0',         /* add $'0', %al */
 0xee,              /* out %al, (%dx) */
 0xb0, '\n',        /* mov $'\n', %al */
 0xee,              /* out %al, (%dx) */
 0xf4,              /* hlt */
};

int main(void) {
  int kvm_fd, vm_fd, vcpu_fd;
  int ret;

  void *guest_mem;

  size_t vcpu_mmap_size;

  struct kvm_run *vcpu_interface;
  struct kvm_sregs vcpu_sregs;
  struct kvm_regs vcpu_regs = { 0 };

  kvm_fd = open("/dev/kvm", O_RDWR | O_CLOEXEC);
  if (kvm_fd == -1) {
    err(
      EXIT_FAILURE,
      "could not open /dev/kvm"
    );
  }

  ret = ioctl(kvm_fd, KVM_GET_API_VERSION, NULL);
  if (ret == -1) {
    err(
      EXIT_FAILURE,
      "KVM_GET_API_VERSION failed"
    );
  }
  if (ret != 12) {
    errx(
      EXIT_FAILURE,
      "expected KVM_GET_API_VERSION == 12, got %d",
      ret
    );
  }

  vm_fd = ioctl(kvm_fd, KVM_CREATE_VM, 0);
  if (vm_fd == -1) {
    err(
      EXIT_FAILURE,
      "KVM_CREATE_VM failed"
    );
  }

  guest_mem = mmap(
      NULL, 0x1000,
      PROT_READ | PROT_WRITE,
      MAP_SHARED | MAP_ANONYMOUS,
      -1, 0
  );
  if (guest_mem == (void *)-1) {
    err(
      EXIT_FAILURE,
      "mmap guest_mem failed"
    );
  }
  memcpy(guest_mem, guest_code, sizeof(guest_code));

  struct kvm_userspace_memory_region region = {
    .slot = 0,
    .guest_phys_addr = 0x1000,
    .memory_size = 0x1000,
    .userspace_addr = (uint64_t)guest_mem,
  };
  ret = ioctl(vm_fd, KVM_SET_USER_MEMORY_REGION, &region);
  if (ret == -1) {
    err(
      EXIT_FAILURE,
      "KVM_SET_USER_MEMORY_REGION failed"
    );
  }

  vcpu_fd = ioctl(vm_fd, KVM_CREATE_VCPU, 0);
  if (vcpu_fd == -1) {
    err(
      EXIT_FAILURE,
      "KVM_CREATE_VCPU failed"
    );
  }

  ret = ioctl(kvm_fd, KVM_GET_VCPU_MMAP_SIZE, NULL);
  if (ret == -1) {
    err(
      EXIT_FAILURE,
      "KVM_GET_VCPU_MMAP_SIZE failed"
    );
  }
  vcpu_mmap_size = ret;
  if (vcpu_mmap_size < sizeof(struct kvm_run)) {
    errx(
      EXIT_FAILURE,
      "VCPU_MMAP_SIZE too small"
    );
  }

  vcpu_interface = mmap(
    NULL, vcpu_mmap_size,
    PROT_READ | PROT_WRITE,
    MAP_SHARED,
    vcpu_fd, 0
  );
  if (vcpu_interface == (void *)-1) {
    err(
      EXIT_FAILURE,
      "mmap vcpu_interface failed"
    );
  }

  ret = ioctl(vcpu_fd, KVM_GET_SREGS, &vcpu_sregs);
  if (ret == -1) {
    err(
      EXIT_FAILURE,
      "KVM_GET_SREGS failed"
    );
  }
  vcpu_sregs.cs.base = 0;
  vcpu_sregs.cs.selector = 0;
  ret = ioctl(vcpu_fd, KVM_SET_SREGS, &vcpu_sregs);
  if (ret == -1) {
    err(
      EXIT_FAILURE,
      "KVM_SET_SREGS failed"
    );
  }

  vcpu_regs.rip = 0x1000;
  vcpu_regs.rax = 3;
  vcpu_regs.rbx = 4;
  vcpu_regs.rflags = 0x2;
  ret = ioctl(vcpu_fd, KVM_SET_REGS, &vcpu_regs);
  if (ret == -1) {
    err(
      EXIT_FAILURE,
      "KVM_SET_REGS failed"
    );
  }

  printf("Startup Complete\n");

  while(true) {
    ret = ioctl(vcpu_fd, KVM_RUN, NULL);
    if (ret == -1) {
      err(
        EXIT_FAILURE,
        "KVM_RUN failed"
      );
    }

    switch (vcpu_interface->exit_reason) {
      case KVM_EXIT_HLT:
        printf("Halting!\n");
        return EXIT_SUCCESS;
      case KVM_EXIT_IO:
        if (
          (vcpu_interface->io.direction == KVM_EXIT_IO_OUT)
          && (vcpu_interface->io.port == 0x3f8)
        ) {
          fwrite(
            (char *)vcpu_interface + vcpu_interface->io.data_offset,
            vcpu_interface->io.size,
            vcpu_interface->io.count,
            stdout
          );
          break;
        } else {
          errx(
            EXIT_FAILURE,
            "unhandled KVM_EXIT_IO"
          );
        }
      case KVM_EXIT_FAIL_ENTRY:
        errx(
          EXIT_FAILURE,
          "KVM_EXIT_FAIL_ENTRY"
        );
      case KVM_EXIT_INTERNAL_ERROR:
        errx(
          EXIT_FAILURE,
          "KVM_EXIT_INTERNAL_ERROR"
        );
      default:
        errx(
          EXIT_FAILURE,
          "KVM_RUN unknown error"
        );
    }
  }

}

