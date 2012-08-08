#include <stdlib.h>
#include <stdio.h>


extern uid_t getuid (void);

int
main(void)
{
  /*Not the world's greatest random number generator, but it will do
    for now. Use identd if you want real security.*/
  
  srandom( (unsigned int) (unique() + getuid()));
  printf("%d%d%d\n", rand(), rand(), rand());
  exit(0);
}
