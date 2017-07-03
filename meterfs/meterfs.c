/*
*/

#include "stdio.h"
#include "stdlib.h"
#include "unistd.h"


int main(void)
{
	char *m = "-\\|/";
	unsigned int i = 0;

	printf("metetrfs: Starting, main is at %p\n", main);

	if (!vfork()) {
		printf("I'm child:  ");
		for (;;) {
			printf("\b%c", m[i++ % 4]);
			usleep(100);
		}
	}

	for (;;) {
		printf("\r%c", m[i++ % 4]);
		usleep(100);
	}

	malloc(100);

	return 0;
}
