#ifndef NUA_GC_TYPES_H
#define NUA_GC_TYPES_H

typedef struct mem_block {
	struct mem_block *next;
	char tag, colour;
} mem_block;

enum gc_mem_type { GC_FLAT, GC_TAB, GC_FUNC, GC_FUNCDEF };

void *gc_alloc(mem_block **p, size_t size, int type) {
	mem_block *mem = calloc(size, 1);
	mem->next = *p;
	mem->tag = type;
	// FIXME use real white
	mem->colour = 0;
	
	*p = mem;

	return (void *) mem;
}

#endif