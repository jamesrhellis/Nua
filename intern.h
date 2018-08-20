#ifndef NUA_INTERN_H
#define NUA_INTERN_H

#include "gen/rh_hash.h"

#include "gc_types.h"

typedef struct slice {
	int len;
	char *str;
} slice;

typedef struct interned_str {
	mem_block link;
	int len;
	char str[];
} interned_str;

static inline uint64_t slice_hash(slice str) {
	// This is a 64 bit FNV-1a hash
	uint64_t hash = 14695981039346656037LU;

	for (int i = 0;i < str.len;++i) {
		hash ^= str.str[i];
		hash *= 1099511628211;
	}

	// Hash must not return 0
	return hash?hash:1;
}

static inline int slice_eq(slice a, slice b) {
	if (a.len != b.len) {
		return 0;
	}
	
	for (int i = 0;i < a.len;++i) {
		if (a.str[i] != b.str[i]) {
			return 0;
		}
	}
	
	return 1;
}


RH_HASH_MAKE(str_map, slice, interned_str *, slice_hash, slice_eq, 0.9);

slice slice_from_intern(interned_str *i) {
	return (slice) {
		.len = i->len,
		.str = i->str,
	};
}

interned_str *intern_from_slice(mem_block **gc, slice c) {
	interned_str *s = gc_alloc(gc, sizeof(interned_str) + c.len + 1, GC_FLAT);
	s->len = c.len;
	s->link.next = NULL;
	memcpy(s->str, c.str, c.len);
	s->str[c.len] = '\0';
	
	return s;
}

interned_str *intern(mem_block **gc, str_map *m, slice s) {
	str_map_bucket *b = str_map_find(m, s);
	if (!b) {
		interned_str *i = intern_from_slice(gc, s);
		
		str_map_set(m, slice_from_intern(i), i);
		return i;
	} else {
		return b->value;
	}
}

str_map global_intern_map = {0};
	
#endif