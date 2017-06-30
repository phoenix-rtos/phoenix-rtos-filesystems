/*
*/

#include "stdio.h"
#include "stdlib.h"
#include "unistd.h"


int main(void)
{
	printf("metetrfs: %d %p\n", 1, main);

	if (!vfork()) {
		printf("I'm child!\n");
		for (;;);
	}

for (;;);

	malloc(100);

	return 0;
}
