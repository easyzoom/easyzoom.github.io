#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(void)
{
	int fd;
	char buf[256] = {0};
	const char *msg = "hello from user space";
	ssize_t n;

	fd = open("/dev/ezpipe", O_RDWR);
	if (fd < 0) {
		perror("open /dev/ezpipe");
		return 1;
	}

	n = write(fd, msg, strlen(msg));
	if (n < 0) {
		perror("write");
		close(fd);
		return 1;
	}

	n = read(fd, buf, sizeof(buf) - 1);
	if (n < 0) {
		perror("read");
		close(fd);
		return 1;
	}

	buf[n] = '\0';
	printf("read back: %s\n", buf);

	close(fd);
	return 0;
}
