#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
	int file = open("../update.md", O_CREAT | O_EXCL | O_WRONLY, 0600);
	if(file < 0) {
		perror("open");
		goto err;
	}

	ssize_t len;
	do {
		len = splice(STDIN_FILENO, NULL, file, NULL, 1024 * 1024, 0);
		if (len < 0) {
			perror("splice");
			goto err;
		}
	} while(len > 0);

	int ret;
	ret = close(file);
	if(ret < 0) {
		perror("close");
		goto err;
	}

	ret = rename("../update.md", "../index.md");
	if(ret < 0) {
		perror("rename");
		goto err;
	}

	puts("Status: 200 OK\r");
	return 0;
err:
	puts("Status: 500 Internal Server Error\r");
	return 0;

}
