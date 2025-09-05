#include "postbox.h"
#include <arpa/inet.h>
#include <string.h>
letter *create_letter(int fd, uint32_t bufsize, const uint8_t *payload) {
	letter *n = (letter *)malloc(sizeof(*n) + bufsize);
	if (!n) {
		perror("malloc");
		exit(EXIT_FAILURE);
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
