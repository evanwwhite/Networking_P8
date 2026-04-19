typedef unsigned long size_t;

extern long open(const char *path, int flags, int mode);
extern long read(int fd, void *buf, size_t len);
extern long write(int fd, const void *buf, size_t len);
extern void exit(int code) __attribute__((noreturn));

size_t strlen(const char *s) {
  size_t len = 0;
  while (s[len] != '\0') {
    len++;
  }
  return len;
}

static void put_str(const char *s) {
  (void)write(1, s, strlen(s));
}

static void fail(const char *msg) {
  put_str("*** FAIL: ");
  put_str(msg);
  put_str("\n");
  exit(1);
}

int main(void) {
  char chunk[6];
  long fd;
  long n;

  put_str("*** ahrdina: opening /sequence\n");

  // This test focuses on one behavior: read must advance the descriptor's file
  // offset. A buggy implementation that always reads from offset 0 will print
  // the first chunk twice instead of progressing through the file.
  fd = open("/sequence", 0, 0);
  if (fd < 0) {
    fail("open(/sequence) failed");
  }

  put_str("*** ahrdina: first read\n");
  n = read((int)fd, chunk, 5);
  if (n != 5) {
    fail("first read returned the wrong length");
  }
  chunk[5] = '\0';
  put_str("*** ahrdina: chunk1=");
  put_str(chunk);
  put_str("\n");

  put_str("*** ahrdina: second read\n");
  n = read((int)fd, chunk, 5);
  if (n != 5) {
    fail("second read returned the wrong length");
  }
  chunk[5] = '\0';
  put_str("*** ahrdina: chunk2=");
  put_str(chunk);
  put_str("\n");

  exit(0);
}
