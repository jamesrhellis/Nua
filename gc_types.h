#ifndef GC_TYPES_H
#define GC_TYPES_H

typedef struct mem_block {
	struct mem_block *next;
} mem_block;

typedef struct mem_grey_link {
	struct mem_block *next;
	struct mem_grey_link *grey_next;
} mem_grey_link;

enum gc_mem_type { GC_FLAT, GC_TAB, GC_FUNC, GC_FUNCDEF };

static inline uint8_t mem_block_tag(mem_block *m) {
	return (uintptr_t)(m->next) & ((uintptr_t)0x111);
}
static inline mem_block *next_mem_block(mem_block *m) {
	return (mem_block *)((uintptr_t)(m->next) & ~((uintptr_t)0x111));
}
static inline void set_next_mem_block(mem_block *t, mem_block *m) {
	t->next = (mem_block *)((uintptr_t)next_mem_block(m) & (uintptr_t) mem_block_tag(t->next));
}
	
static inline void mem_block_typetag(mem_block *m, uint8_t type) {
	m->next = (mem_block *)(((uintptr_t)m->next & ~((uintptr_t)0x110)) | (type << 1));
}
static inline int mem_block_type(mem_block *m) {
	return ((uintptr_t)m->next & (uintptr_t)0x110) >> 1;
}

static inline void mem_block_coltag(mem_block *m, uint8_t colour) {
	m->next = (mem_block *)(((uintptr_t)m->next & ~((uintptr_t)0x1)) | colour);
}

static inline int mem_block_col(mem_block *m) {
	return ((uintptr_t)m->next & (uintptr_t)0x1);
}

void *gc_alloc(mem_block **p, size_t size, int type) {
	mem_block *mem = calloc(size, 1);
	mem->next = *p;
	mem_block_typetag(mem, type);
	*p = mem;

	return (void *) mem;
}

#endif