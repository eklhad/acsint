/* pipetest.c: test opening a child process over pipes */

#include <stdlib.h>
#include <stdio.h>

#include "acsbridge.h"

int main(int argc, char **argv)
{
FILE *f0, *f1;
char line[80];
char *alist[8];

alist[0] = "cat";
alist[1] = "pipetest.c";
alist[2] = "Makefile";
alist[3] = "-";
alist[4] = 0;

/* This is the argv interface, but I've commented it out,
 * because I can test everything from the args in line wrapper.
 * acs_pipe_openv("/bin/cat", alist);
*/

acs_pipe_open("cat", "pipetest.c", "Makefile", "-", 0);

f0 = fdopen(acs_sy_fd0, "r");
f1 = fdopen(acs_sy_fd1, "w");

fprintf(f1, "hello world\n");
fclose(f1);

while(fgets(line, sizeof(line), f0))
printf("%s", line);

exit(0);
}
