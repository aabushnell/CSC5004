#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/uio.h>

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* reverse: reverse string s in place.
 *
 * Adapted from K&R2 to accept len as argument.
 */
void reverse(char s[], size_t len) {
  int i, j;
  char c;

  for (i = 0, j = len - 1; i < j; i++, j--) {
    c = s[i];
    s[i] = s[j];
    s[j] = c;
  }
}

/* itoa: convert n to characters in s.
 *
 * Returns strlen of s.
 *
 * Adapted from K&R2 to return strlen of s.
 */
size_t itoa(int n, char s[]) {
  int i, sign;

  if ((sign = n) < 0)
    n = -n;

  i = 0;
  do { /* generate digits in reverse order */
    s[i++] = n % 10 + '0';
  } while ((n /= 10) > 0);

  if (sign < 0)
    s[i++] = '-';

  s[i] = '\0';
  reverse(s, i);

  return i;
}

/* Write a line, i.e., s followed by a linefeed, using the plain syscall write.
 *
 * Expects a proper null-terminated string.
 *
 * Returns the number of bytes written, or -1 and sets errno as write does.
 */
int writeln(int fd, char *s) {
  int ret;
  size_t len = strlen(s);

  s[len] = '\n';
  ret = write(fd, s, len + 1);
  s[len] = '\0';

  return ret;
}

#define SYS_PVMMAP 511
void *pvmmap(void *addr, size_t len, int fd, off_t offset) {
  return (void *)syscall(SYS_PVMMAP, addr, len, fd, offset);
}

#define BUFF_SIZE 512
int main1(int argc, char *argv[]) {
  int fd_rd, fd_wr;
  size_t count_rd, count_wr;
  char buff[BUFF_SIZE];

  fd_rd = open("secrets.txt", O_RDONLY);
  count_rd = read(fd_rd, buff, BUFF_SIZE);
  if (count_rd < 0) {
    writeln(STDERR_FILENO, "failed to read secrets!");
    return 0x01;
  }

  fd_wr = open("nsa_database.txt", O_RDWR | O_CREAT | O_TRUNC,
               S_IRUSR | S_IWUSR);
  count_wr = write(fd_wr, buff, count_rd);
  if (count_wr < 0 || count_wr != count_rd) {
    writeln(STDERR_FILENO, "failed to properly write to database!");
    return 0x01;
  }

  close(fd_rd);
  close(fd_wr);

  return 0x42;
}

int main2(int argc, char *argv[]) {
  char n_args_str[32] = "argc = ";
  itoa(argc, &n_args_str[7]);
  writeln(STDOUT_FILENO, n_args_str);

  writeln(STDOUT_FILENO, "argv:");
  for (int i = 0; i < argc; i++) {
    writeln(STDOUT_FILENO, argv[i]);
  }

  return 0x42;
}

int main3(int argc, char *argv[]) {
  void *mapped_mem = pvmmap(NULL, 0x1000, -1, 0);
  int *my_data = (int *)mapped_mem;

  // private key \in (0, 2^384)
  int my_private_key = 4;

  // store in memory
  my_data[0] = my_private_key;

  // retrieve from memory
  char private_key_str[32] = "private_key = ";
  itoa(my_data[0], &private_key_str[14]);
  writeln(STDOUT_FILENO, private_key_str);

  return 0x42;
}

int main(int argc, char *argv[]) {
  int fd;
  fd = open("private_key.txt", O_RDONLY);

  void *mapped_mem = pvmmap(NULL, 0x1000, fd, 0);
  char *key_data = (char *)mapped_mem;

  char private_key_str[32] = "private_key = ";
  private_key_str[14] = key_data[0];
  private_key_str[15] = '\0';
  writeln(STDOUT_FILENO, private_key_str);

  return 0x42;
}
