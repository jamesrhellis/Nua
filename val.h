#ifndef VAL_H
#define VAL_H

typedef enum val_type { VAL_NIL, VAL_NUM, VAL_STR, VAL_FUNC, VAL_TAB, VAL_TYPE_NO } val_type;
const char *val_type_str[VAL_TYPE_NO] = { "NIL", "NUM", "STR", "FUNC", "TAB" };

struct tab;
struct func;

typedef struct {
	val_type type;
	union {
		double num;
		char *str;
		struct func *func;
		struct tab *tab;

	};
} val;

static inline uint64_t val_hash(const val v) {
	uint64_t hash = 0;

	switch (v.type) {
	case VAL_NUM:
		hash = *((uint64_t *)&(v.num));
	case VAL_TAB:
		hash = *((uintptr_t *)(v.tab));
	case VAL_STR:
		hash = *((uintptr_t *)(v.str));
	case VAL_FUNC:
		hash = *((uintptr_t *)(v.func));
	default:
		break;
	}

	// Hash must not return 0
	return hash?hash:1;
}

static inline int val_eq(const val a, const val b) {
	switch (a.type) {
	case VAL_NUM:
		return (b.type == VAL_NUM) && a.num == b.num;
	case VAL_TAB:
		return (b.type == VAL_TAB) && a.tab == b.tab;
	case VAL_STR:
		return (b.type == VAL_STR) && a.str == b.str;
	case VAL_FUNC:
		return (b.type == VAL_FUNC) && a.func == b.func;
	default:
		return 0;
	}
}

RH_HASH_MAKE(val_ht, val, val, val_hash, val_eq, 0.9)
RH_AL_MAKE(val_al, val)
typedef struct tab {
	mem_grey_link link;
	val_al al;
	val_ht ht;
} tab;

val tab_get(tab *t, val v) {
	// Try to find in al first
	if (v.type == VAL_NUM && v.num == floor(v.num) && v.num > 0) {
		size_t ind = (size_t)v.num;
		if (ind < t->al.top) {
			val ret = t->al.items[ind];
			if (ret.type != VAL_NIL) {
				return ret;
			}
		}
	}

	val_ht_bucket *b = val_ht_find(&t->ht, v);
	if (!b) {
		return (val) {VAL_NIL};
	}

	return b->value;
}

int tab_set(tab *t, val k, val v) {
	// Try to set in al first
	if (k.type == VAL_NUM && k.num == floor(k.num) && k.num > 0) {
		size_t ind = (size_t)k.num;
		if (ind < t->al.top) {
			t->al.items[ind] = v;
			return 0;
		} else if (ind == t->al.top) {
			return val_al_push(&t->al, v);
		}
	}

	val_ht_set(&t->ht, v, k);
	return 0;
}

static inline int tab_push(tab *t, val v) {
	return val_al_push(&t->al, v);
}
typedef enum optype { OPT_N, OPT_RU, OPT_RI, OPT_RRR, OPT_O } optype;
typedef enum opcode  { OP_NOP, OP_SETL, OP_SETI, OP_END, OP_COVER, OP_JMP, OP_NIL, OP_ADD, OP_SUB,
	OP_GT, OP_GE, OP_MOV, OP_TAB, OP_GTAB, OP_STAB, OP_PTAB, OP_CALL, OP_RET, OPCODE_NO} opcode;
char *opcode_str[OPCODE_NO] =
                     {"NOP","SETL","SETI","END","COVER","JMP","NIL","ADD","SUB","GT","GE","MOV", "TAB", "GTAB", "STAB", "PTAB", "CALL", "RET"};
optype opcode_type[OPCODE_NO] = { OPT_N, OPT_RU, OPT_RI, OPT_N, OPT_RU, OPT_O, OPT_RU , OPT_RRR, OPT_RRR, OPT_RRR
	, OPT_RRR, OPT_RRR, OPT_RRR, OPT_RRR, OPT_RRR, OPT_RRR, OPT_RRR, OPT_RRR};

typedef struct inst {
	uint8_t op;
	union {
		struct {
			uint8_t reg;
			uint16_t lit;
		};
		struct {
			uint8_t rout;
			uint8_t rina;
			uint8_t rinb;
		};
		struct {
			uint8_t _reg;
			int16_t ilit;
		};
		struct {
			int off : 24;
		};
	};
} inst;

RH_AL_MAKE(inst_list, inst)
RH_AL_MAKE(inst_lines, int)
RH_HASH_MAKE(loc_map, char *, size_t, rh_string_hash, rh_string_eq, 0.9)

typedef struct func_def {
	mem_grey_link link;

	// Properties
	uint8_t max_reg;
	uint8_t no_args;

	// Code
	inst_list ins;
	val_al literals;

	// Debug data
	char *file;
	inst_lines lines;
} func_def;

typedef enum funct { FUNC_ERR, FUNC_NUA, FUNC_C } funct;

typedef struct func {
	funct type;
	union {
		struct {
			val_al upvals;
			func_def *def;
		};
		struct {
			// TODO Real c function type
			void *c_func;
		};
	};
} func;

int print_val(val v);

int print_literals(func_def f) {
	for (size_t i = 0;i < f.literals.top;++i) {
		printf("%zu| %s; ", i, val_type_str[f.literals.items[i].type]);
		print_val(f.literals.items[i]);
	}
	return 0;
}

int print_inst(inst i) {
	printf("%s; ", opcode_str[i.op]);
	switch (opcode_type[i.op]) {
	case OPT_N:
		puts("");
		break;
	case OPT_RU:
		printf("%d, %d\n", i.reg, i.lit);
		break;
	case OPT_RI:
		printf("%d, %d\n", i.reg, i.ilit);
		break;
	case OPT_RRR:
		printf("%d, %d, %d\n", i.rout, i.rina, i.rinb);
		break;
	case OPT_O:
		printf("%d\n", i.off);
		break;
	}
}

int print_func_def(func_def f) {
	for (size_t i = 0;i < f.ins.top;++i) {
		printf("%d| %s; ", f.lines.items[i], opcode_str[f.ins.items[i].op]);
		print_inst(f.ins.items[i]);
	}
	return 0;
}

int print_val(val v) {
	switch (v.type) {
	case VAL_NUM:
		printf("%f\n", v.num);
		break;
	case VAL_FUNC:
		switch (v.func->type) {
		case FUNC_NUA:
			puts("FUNC_NUA:");
			puts("{");
			print_func_def(*v.func->def);
			print_literals(*v.func->def);
			puts("}");
			break;
		case FUNC_C:
			puts("FUNC_C:");
			break;
		}
		break;
	case VAL_TAB:
		puts("{");
		puts("AL:");
		for (size_t i = 0;i < v.tab->al.top;++i) {
			printf("%zu;", i);
			print_val(v.tab->al.items[i]);
		}
		puts("Hash:");
		if (v.tab->ht.items) {
			for (size_t i = 0;i < RH_HASH_SIZE(v.tab->ht.size);++i) {
				if (!v.tab->ht.hash[i]) {
					continue;
				}
				print_val(v.tab->ht.items[i].key);
				printf("= ");
				print_val(v.tab->ht.items[i].value);
			}
		}
		puts("}");
		break;
	default:
		puts(val_type_str[v.type]);
		break;
	}

	return 0;
}

#endif
