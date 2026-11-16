#ifndef VMM_H
#define VMM_H

#include <stdint.h>

#define DEV_KVM "/dev/kvm"

/* IO port where hypercalls are expected.
 *
 * The syscall wrapper in guest/boot/boot.asm translates a guest app syscall
 * into an out instruction to this port.
 */
#define HC_IO_PORT 0x118

// Guest memory mapped in the host.
extern uint8_t *guest_mem;

extern int vmfd;

typedef struct slot {
  uint64_t guest_phys_base_addr;
  uint64_t host_virt_base_addr;
  uint64_t memory_size;
} memory_slot;

extern int max_slots;
extern memory_slot *memory_slots;

int create_vm(void);
int launch_vm(uint64_t boot_gpa, uint64_t app_gpa, uint64_t app_stack_gpa);

#endif
