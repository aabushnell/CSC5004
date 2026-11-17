#include "loader.h"
#include "memory.h"
#include "memory_map.h"
#include "vmm.h"

#include <err.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[]) {
  if (argc < 3)
    errx(2, "expected 2 arguments: bootloader_filepath guest_app_filepath");

  if (create_vm())
    errx(EXIT_FAILURE, "creating the VM");

  FILE *f = get_file(argv[1]);
  if (!f)
    err(EXIT_FAILURE, "loading bootloader");
  load_sections(f, guest_mem);

  f = get_file(argv[2]);
  if (!f)
    err(EXIT_FAILURE, "loading guest application");
  load_sections(f, guest_mem);

  // Map the user space stack.
  memory_map_range(guest_mem, USER_STACK_TOP_GPA, BOOT_GPA);
  // Map the boot section as loaded above.
  memory_map_range(guest_mem, BOOT_GPA, APP_GPA);
  // Map the guest app as loaded above.
  memory_map_range(guest_mem, APP_GPA, ALLOC_AREA_GPA);

  // Construct Initial Process Stack for guest app
  //  > based on System-V ABI spec
  uint64_t stack_ptr = APP_STACK_GPA;
  size_t guest_argc = argc - 2;
  uint64_t info_ptr = stack_ptr + 0x28 + 0x08 * guest_argc;

  uint64_t *stack = (uint64_t *)get_hva(stack_ptr);
  size_t stack_idx = 0;

  // Argument count
  stack[stack_idx++] = guest_argc;

  // Argument vector
  for (size_t i = 0; i < guest_argc; i++) {
    const char *arg_str; 
    if (i == 0) {
      // argv[0]
      arg_str = "app";
    } else {
      arg_str = argv[i + 2];
    }
    size_t len = strlen(arg_str) + 1;
    if (info_ptr + len > BOOT_GPA)
      errx(1, "argv array too long; overflows allocated page");

    // Information block (argument string)
    memcpy(get_hva(info_ptr), arg_str, len);

    stack[stack_idx++] = info_ptr;
    info_ptr += len;
  }

  // Argument vector NULL terminator
  stack[stack_idx++] = 0x00;

  // Environment vector NULL terminator
  stack[stack_idx++] = 0x00;

  // Auxiliary vector NULL terminator (double-word)
  stack[stack_idx++] = 0x00;
  stack[stack_idx++] = 0x00;

  launch_vm(BOOT_GPA, APP_GPA, APP_STACK_GPA);

  return 0;
}
