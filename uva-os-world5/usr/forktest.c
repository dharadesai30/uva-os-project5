// Test that fork fails gracefully.
// Tiny executable so that the limit can be filling the proc table.
// fxl: for some reason,  1st print() prints ok but subseuqent ones print
// corrupted msgs. while printf() work fine. TBD

#include "user.h"

#define N  1000

static void
print(const char *s)
{
  write(1, s, strlen(s));
}

void
forktest(void)
{
  int n, pid;

  print("fork test\n");

  for(n=0; n<N; n++){
    pid = fork();
    if(pid < 0)
      break;
    if(pid == 0)
      exit(0);
  }

  if(n == N){
    printf("fork claimed to work N times!\n");
    exit(1);
  }

  for(; n > 0; n--){
    if(wait(0) < 0){
      print("wait stopped early\n");
      exit(1);
    }
  }

  if(wait(0) != -1){
    print("wait got too many\n");
    exit(1);
  }

  printf("fork test OK\n");
}

int
main(void)
{
  forktest();
  exit(0);
}
