#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
  // Flush after every printf
  setbuf(stdout, NULL);
  char command[100];
  while (1) {
      printf("$ ");
      fgets(command,sizeof(command),stdin);
  }

  return 0;
}
