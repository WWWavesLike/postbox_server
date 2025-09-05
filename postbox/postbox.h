#ifndef POSTBOX_H
#define POSTBOX_H
#include "../util/list.h"
#include <stdint.h>

typedef struct {
	uint32_t key;
	int cfd;
	struct list_head ptr;
	uint32_t bufsz;
	const uint8_t buf[0];
} letter;

typedef struct {
	struct list_head *pb;
	struct list_head *sq;
} listargs;

letter *create_letter(int fd, uint32_t bufsize, const uint8_t *payload);

#endif
