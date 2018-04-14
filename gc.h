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

RH_AL_MAKE(gc_process_al, mem_block *)
mem_block *gc_list = NULL;

void *gc_alloc(size_t size) {
	mem_block *mem = calloc(size, 1);
	mem->next = gc_list;
	gc_list = mem;

	return (void *) mem;
}

void gc_sweep(uint8_t white_tag) {
	if (!gc_list) {
		return;
	}

	mem_block **ptr = &gc_list;
	while (*ptr) {
		if (white_tag ==  mem_block_tag(*ptr)) {
			mem_block *tofree = *ptr;
			*ptr = (*ptr)->next;

			free(tofree);
			continue;
		}

		ptr = &((*ptr)->next);
	}
}

#endif
