#define _GNU_SOURCE
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MIN(a, b) ((a) < (b) ? (a) : (b))

#define WORK_PREFIX "../files/work-"
#define MULTIPART_PREFIX "multipart/form-data; boundary="
#define CONTENT_DISPOSITION_KEY "Content-Disposition: "
#define CONTENT_TYPE_KEY "Content-Type: "

static char *
str_to_sep(char *buf, size_t *idx, size_t len, const char *sep)
{
	size_t seplen = strlen(sep);
	char *str = &buf[*idx];
	char *cr = memmem(str, len - *idx, sep, seplen);
	if(cr != NULL) {
		*idx = cr - buf + seplen;
		*cr = '\0';
		return str;
	}
	return NULL;
}

static bool
match(char *buf, size_t *idx, size_t len, char *m)
{
	size_t mlen = strlen(m);
	if(mlen > len - *idx) {
		return false;
	}
	if(memcmp(&buf[*idx], m, mlen) == 0) {
		*idx += mlen;
		return true;
	}
	return false;
}

int
main(int argc, char *argv[])
{
	const char *content_type = getenv("CONTENT_TYPE");
	fprintf(stderr, "ct %s\n", content_type);
	ssize_t len = strlen(content_type);
	if(
		len < sizeof(MULTIPART_PREFIX)-1 ||
		memcmp(content_type, MULTIPART_PREFIX, sizeof(MULTIPART_PREFIX)-1) != 0
	) {
		fprintf(stderr, "Bad Content-Type: %s\n", content_type);
		goto err_500;
	}
	const char *boundary = &content_type[sizeof(MULTIPART_PREFIX)-1];
	size_t boundary_len = strlen(boundary);	

	char buf[8192];
	len = read(STDIN_FILENO, buf, sizeof(buf));
	if (len < 0) {
		perror("read");
		goto err_500;
	}

	size_t idx = 0;
	if(
		!match(buf, &idx, len, "--") ||
		!match(buf, &idx, len, boundary)
	) {
		fprintf(stderr, "Bad multipart boundary: %.*s\n", (int)MIN(len, boundary_len), content_type);
		goto err_500;
	}
	
	if(!match(buf, &idx, len, "\r\n")) {
		fputs("Bad multipart structure, expected new line after boundary\n", stderr);
		goto err_500;
	}

	char *image_type = NULL;

	do {
		if(match(buf, &idx, len, CONTENT_DISPOSITION_KEY)) {
			if(!match(buf, &idx, len, "form-data")) {
				fprintf(stderr, "Bad " CONTENT_DISPOSITION_KEY " (expected 'form-data'): %.*s", (int)MIN(len-idx, boundary_len), &buf[idx]);
				goto err_500;
			}
			if(!match(buf, &idx, len, "; ")) {
				fprintf(stderr, "Bad " CONTENT_DISPOSITION_KEY " (expected ';'): %.*s", (int)MIN(len-idx, boundary_len), &buf[idx]);
				goto err_500;
			}
			if(str_to_sep(buf, &idx, len, "\r\n") == NULL) {
				fprintf(stderr, "Bad " CONTENT_DISPOSITION_KEY " (expected new line): %.*s", (int)MIN(len-idx, boundary_len), &buf[idx]);
				goto err_500;
			}
		} else if(match(buf, &idx, len, CONTENT_TYPE_KEY)) {
			if(!match(buf, &idx, len, "image/")) {
				goto err_415;
			}
			image_type = str_to_sep(buf, &idx, len, "\r\n");
		} else {
			fputs("Multipart error\n", stderr);
			goto err_500;
		}
	} while(!match(buf, &idx, len, "\r\n"));

	if(image_type == NULL) {
		fputs("No MIME type given\n", stderr);
		goto err_500;
	}

	if(strcmp(image_type, "svg+xml") == 0) {
		image_type = "svg";
	} else if(
		strcmp(image_type, "png") != 0 &&
		strcmp(image_type, "jpeg") != 0 &&
		strcmp(image_type, "webp") != 0 &&
		strcmp(image_type, "gif") != 0
	) {
		goto err_415;
	}

	uint64_t random;
	char filename[sizeof(WORK_PREFIX) + sizeof(random) * 2] = WORK_PREFIX;

	if(getentropy(&random, sizeof(random)) == -1) {
		perror("getentropy");
		goto err_500;
	}
	for(size_t i = sizeof(WORK_PREFIX)-1; i < sizeof(filename)-1; i++) {
		filename[i] = 'a' + ((random >> i) & 0xf);
	}

	fprintf(stderr, "%s\n", filename);
	int file = open(filename, O_CREAT | O_EXCL | O_WRONLY, 0600);
	if(file < 0) {
		perror("open");
		goto err_500;
	}

	size_t flen = 0;
	len = write(file, &buf[idx], len - idx);
	if(len == -1) {
		perror("first write");
		goto err_500;
	}
	flen += len;

	do {
		len = splice(STDIN_FILENO, NULL, file, NULL, 1024 * 1024, 0);
		if (len < 0) {
			perror("splice");
			goto err_500;
		}
		flen += len;
	} while(len > 0);
	
	if(flen < boundary_len) {
		fputs("No boundary?\n", stderr);
		goto err_500;
	}
	if(ftruncate(file, flen - boundary_len) == -1) {
		perror("ftruncate");
		goto err_500;
	}

	int ret;
	ret = close(file);
	if(ret < 0) {
		perror("close");
		goto err_500;
	}

	execl("../../upload-checksum.sh", "../../upload-checksum.sh", filename, image_type, NULL);
	unlink(filename);
err_500:
	puts("Status: 500 Internal Server Error\r");
	return 0;
err_415:
	puts("Status: 415 Unsupported Media Type\r\n"
		"\r\n"
		"\"error\": \"typeNotAllowed\"}\r");
	return 0;
}
