#ifndef MACRO_H
#define MACRO_H

#define ALLOC_CHECK(ptr, msg) \
	if (!ptr) {               \
		fprintf(stderr, msg); \
		exit(0);              \
	}

#endif
