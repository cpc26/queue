/* Only needed if OS doesn't define.*/

char *
strdup(char *str)
{
  char *p = (char*) mymalloc(strlen(str)+1);
  strcpy(p, str);
  return(p);
}
