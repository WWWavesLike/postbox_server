#include "postbox.h"
#include <arpa/inet.h>
#include <string.h>
letter *create_letter(int fd, uint32_t bufsize, const uint8_t *payload) {
	/*
		버퍼 오버플로우 방지. io.c에서 검사하므로 주석처리함.
	if (bufsize < 4) {
		return NULL;
	}
*/
	letter *n = (letter *)malloc(sizeof(*n) + bufsize);
	if (!n) {
		return NULL;
	}
	uint32_t net_id;
	memcpy(&net_id, payload, 4);
	n->cfd = fd;
	n->key = ntohl(net_id);
	n->bufsz = bufsize - 4u;
	init_list_head(&n->ptr);

	memcpy((void *)n->buf, payload + 4u, n->bufsz);

	return n;
}
