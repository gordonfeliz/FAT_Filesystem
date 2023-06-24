#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fs.h>

#define ASSERT(cond, func)                               \
do {                                                     \
	if (!(cond)) {                                       \
		fprintf(stderr, "Function '%s' failed\n", func); \
		exit(EXIT_FAILURE);                              \
	}                                                    \
} while (0)

int main(int argc, char *argv[])
{
	int ret;
	char *diskname;
	int fd;
	char data[26];

	if (argc < 1) {
		printf("Usage: %s <diskimage>\n", argv[0]);
		exit(1);
	}

	/* Mount disk */
	diskname = argv[1];
	ret = fs_mount(diskname);
	ASSERT(!ret, "fs_mount");

	/* Open file */
	fd = fs_open("myfile");
	ASSERT(fd >= 0, "fs_open");

	/* Read some data */
	fs_lseek(fd, 12);
	ret = fs_read(fd, data, 10);
	ASSERT(ret == 10, "fs_read");
	ASSERT(!strncmp(data, "mnopqrstuv", 10), "fs_read");

	/* Close file and unmount */
	fs_close(fd);
	fs_umount();

	return 0;
}
