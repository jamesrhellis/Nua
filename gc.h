#ifndef GC_H
#define GC_H

typedef enum mem_type { MEM_FLAT, MEM_TAB, MEM_FUNC, MEM_SPARE} mem_type;

typedef struct mem_block {
	struct mem_block *next;
} mem_block;

typedef struct mem_grey_link {
	struct mem_block *next;
	struct mem_grey_link *grey_next;
} mem_grey_link;

static inline mem_block *next_mem_block(mem_block *m) {
	return (mem_block *)((uintptr_t)(m->next) & ~((uintptr_t)0x111));
}

static inline void mem_block_typetag(mem_block *m, uint8_t mem_type) {
	uint8_t tag = mem_type << 1;
	m->next = (mem_block *)((uintptr_t)m->next & ~((uintptr_t)0x110) | tag);
}

static inline void mem_block_coltag(mem_block *m, uint8_t colour) {
	m->next = (mem_block *)((uintptr_t)m->next & ~((uintptr_t)0x1) | colour);
}

static inline uint8_t mem_block_tag(mem_block *m) {
	return (uintptr_t)(m->next) & ((uintptr_t)0x111);
}

void *gc_alloc(mem_block **p, size_t size) {
	mem_block *mem = calloc(size, 1);
	mem->next = *p;
	*p = mem;

	return (void *) mem;
}

void gc_sweep(mem_block **p, uint8_t white_tag) {
	if (!*p) {
		return;
	}

	while (*p) {
		if (white_tag ==  mem_block_tag(*p)) {
			mem_block *tofree = *p;
			*p = (*p)->next;

			free(tofree);
			continue;
		}

		p = &((*p)->next);
	}
}

#endif
