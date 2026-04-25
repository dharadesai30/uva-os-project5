// simple user lib. derived from xv6. slighly augmented. 
#include "user.h"

// wrapper so that it's OK if main() does not call exit().
void
_main()
{
  extern int main();
  main();
  exit(0);
}

char*
strcpy(char *s, const char *t)
{
  char *os;

  os = s;
  while((*s++ = *t++) != 0)
    ;
  return os;
}

int
strcmp(const char *p, const char *q)
{
  while(*p && *p == *q)
    p++, q++;
  return (uchar)*p - (uchar)*q;
}

uint
strlen(const char *s)
{
  int n;

  for(n = 0; s[n]; n++)
    ;
  return n;
}

void*
memset(void *dst, int c, uint n)
{
  char *cdst = (char *) dst;
  int i;
  for(i = 0; i < n; i++){
    cdst[i] = c;
  }
  return dst;
}

char*
strchr(const char *s, char c)
{
  for(; *s; s++)
    if(*s == c)
      return (char*)s;
  return 0;
}

char*
gets(char *buf, int max)
{
  int i, cc;
  char c;

  for(i=0; i+1 < max; ){
    cc = read(0, &c, 1);
    if(cc < 1)
      break;
    buf[i++] = c;
    if(c == '\n' || c == '\r')
      break;
    // fxl: below, support sh cmd history (up/down arrows) 
    // this is a bit hacky and breaks the classic semantics
    // of gets(). but sh seems the only caller of gets()
    // in libc-simple
    // TBD (Nov 2024)
  }
  buf[i] = '\0';
  return buf;
}

int
stat(const char *n, struct stat *st)
{
  int fd;
  int r;

  fd = open(n, O_RDONLY);
  if(fd < 0)
    return -1;
  r = fstat(fd, st);
  close(fd);
  return r;
}

int
atoi(const char *s)
{
  int n, sign=1;
  if (*s=='-') {s++; sign=-1;}
  n = 0;
  while('0' <= *s && *s <= '9')
    n = n*10 + *s++ - '0';
  return sign==1?n:-n;
}

void*
memmove(void *vdst, const void *vsrc, int n)
{
  char *dst;
  const char *src;

  dst = vdst;
  src = vsrc;
  if (src > dst) {
    while(n-- > 0)
      *dst++ = *src++;
  } else {
    dst += n;
    src += n;
    while(n-- > 0)
      *--dst = *--src;
  }
  return vdst;
}

int
memcmp(const void *s1, const void *s2, uint n)
{
  const char *p1 = s1, *p2 = s2;
  while (n-- > 0) {
    if (*p1 != *p2) {
      return *p1 - *p2;
    }
    p1++;
    p2++;
  }
  return 0;
}

void *
memcpy(void *dst, const void *src, uint n)
{
  return memmove(dst, src, n);
}

// (fxl) atoi16() support both dec and hex. 
// https://github.com/littlekernel/lk/blob/master/lib/libc/atoi.c
int isdigit(int c) {
    return ((c >= '0') && (c <= '9'));
}

static int isxdigit(int c) {
    return isdigit(c) || ((c >= 'a') && (c <= 'f')) || ((c >= 'A') && (c <= 'F'));
}

static int hexval(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    else if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    else if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return 0;
}

unsigned atoi16(const char *num) {
    unsigned value = 0;
    int neg = 0;

    if (num[0] == '0' && num[1] == 'x') {
        // hex
        num += 2;
        while (*num && isxdigit(*num))
            value = value * 16 + hexval(*num++);
    } else {
        // decimal
        if (num[0] == '-') {
            neg = 1;
            num++;
        }
        while (*num && isdigit(*num))
            value = value * 10 + *num++  - '0';
    }
    if (neg)
        value = -value;

    return value;
}

void spinlock_init(struct spinlock_u *lk) {
  lk->locked = 0; 
}

void spinlock_lock(struct spinlock_u *lk) {
  while(__sync_lock_test_and_set(&lk->locked, 1) != 0)
    ;
  __sync_synchronize();
}

void spinlock_unlock(struct spinlock_u *lk) {
  __sync_synchronize();
  __sync_lock_release(&lk->locked);
}

/// assert. needed by assert()
void __assert_fail(const char * assertion, const char * file, 
  unsigned int line, const char * function) {  
  printf("assertion failed: %s at %s:%d\n", assertion, file, (int)line); 
  exit(1); 
}
