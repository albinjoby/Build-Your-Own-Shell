#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[]) {
  // Flush after every printf
  setbuf(stdout, NULL);
  char command[100];
  char *commands[] = {
      "exit",
      "echo",
      "type"
  };
  
  while (1) {
      printf("$ ");
      fgets(command,sizeof(command),stdin);
      
      command[strcspn(command, "\n")] = '\0';
      
      if (strcmp(command, "exit") == 0) {
          break;
      }else if (strncmp(command, "echo ", 5) == 0) {
          printf("%s\n", command+5);
      }else if (strncmp(command, "type ", 5) == 0) {
          int flag = 0;
          for (int i=0; i<sizeof(commands)/sizeof(commands[0]); i++) {
              if (strcmp(command+5, commands[i]) == 0) {
                  printf("%s is a shell builtin\n",command+5);
                  flag = 1;
                  break;
              }
          }
          if (!flag) {
              printf("%s: not found\n",command+5);
          }
      }
      else{
          printf("%s: command not found\n",command);
      }
  }
  return 0;
}
