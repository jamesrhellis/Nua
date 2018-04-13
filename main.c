#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "gen/rh_tp.h"
#include "gen/rh_hash.h"
#include "gen/rh_al.h"

typedef enum val_type { VAL_NIL, VAL_NUM, VAL_STR, VAL_FUNC, VAL_TAB, VAL_TYPE_NO } val_type;
const char *val_type_str[VAL_TYPE_NO] = { "NIL", "NUM", "STR", "FUNC", "TAB" };

struct tab;

typedef struct {
	val_type type;
	union {
		double num;
		char *str;
		void *func;
		struct tab *tab;

	};
} val;

static inline uint64_t val_hash(const val v) {
	uint64_t hash = 0;

	switch (v.type) {
	case VAL_NUM:
		hash = *((uint64_t *)&(v.num));
	default:
		break;
	}

	// Hash must not return 0
	return hash?hash:1;
}

static inline int val_eq(const val a, const val b) {
	switch (a.type) {
	case VAL_NUM:
		return (b.type == VAL_NUM) &&  a.num == b.num;
	default:
		return 0;
	}
}

RH_HASH_MAKE(val_ht, val, val, val_hash, val_eq, 0.9)
RH_AL_MAKE(val_al, val)

typedef enum optype { OPT_N, OPT_RL, OPT_RRR, OPT_O } optype;
typedef enum opcode  { OP_NOP, OP_SETL, OP_END, OP_COVER, OP_JMP, OP_NIL, OP_ADD, OP_SUB, OP_GT, OP_GE, OP_MOV, OPCODE_NO} opcode;
char *opcode_str[OPCODE_NO] =
                     {"NOP","SETL","END","COVER","JMP","NIL","ADD","SUB","GT","GE","MOV"};
optype opcode_type[OPCODE_NO] = { OPT_N, OPT_RL, OPT_N, OPT_RL, OPT_O, OPT_RL , OPT_RRR, OPT_RRR, OPT_RRR, OPT_RRR, OPT_RRR};

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
			int off : 24;
		};
	};
} inst;

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

RH_AL_MAKE(inst_list, inst)
RH_AL_MAKE(inst_lines, int)
RH_HASH_MAKE(loc_map, char *, size_t, rh_string_hash, rh_string_eq, 0.9)

typedef struct func_def {
	mem_grey_link link;
	char *file;

	inst_list ins;
	inst_lines lines;
	val_al literals;
} func_def;

typedef struct frame {
	size_t reg_base;
	func_def func;
} frame;

typedef struct tab {
	mem_grey_link link;
	val_al al;
	val_ht ht;
} tab;

RH_AL_MAKE(frame_stack, frame)

#include "parse.c"

int print_val(val v) {
	switch (v.type) {
	case VAL_NUM:
		printf("%f\n", v.num);
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
	defualt:
		puts("");
		break;
	}

	return 0;
}

int print_literals(func_def f) {
	for (size_t i = 0;i < f.literals.top;++i) {
		printf("%zu| %s; ", i, val_type_str[f.literals.items[i].type]);
		print_val(f.literals.items[i]);
	}
	return 0;
}

int print_func_def(func_def f) {
	for (size_t i = 0;i < f.ins.top;++i) {
		printf("%d| %s; ", f.lines.items[i], opcode_str[f.ins.items[i].op]);
		switch (opcode_type[f.ins.items[i].op]) {
		case OPT_N:
			puts("");
			break;
		case OPT_RL:
			printf("%d, %d\n", f.ins.items[i].reg, f.ins.items[i].lit);
			break;
		case OPT_RRR:
			printf("%d, %d, %d\n", f.ins.items[i].rout, f.ins.items[i].rina, f.ins.items[i].rinb);
			break;
		case OPT_O:
			printf("%d\n", f.ins.items[i].off);
			break;
		}
	}
	return 0;
}

int main(int argn, char **args) {
	if (argn < 2) {
		return 0;
	}

	const char *file = load_file(args[1]);
	if (!file) {
		fprintf(stderr, "Unable to load file!");
		return 1;
	}

	parse_init();

	func_def init;
	if (parse((lexer){file}, &init)) {
		fprintf(stderr, "Unable to parse file!");
		return 1;
	}
	print_func_def(init);
	print_literals(init);
	size_t i = 0;
	val_al stack = val_al_new(512);
	frame_stack func = frame_stack_new(8);
	frame_stack_push(&func, (frame) { 0, init });

	val *reg = stack.items;

	while (true) {
		inst ins = init.ins.items[i];
		switch (ins.op) {
		case OP_JMP:
			i += ins.off;
			continue;	// Avoid addition at end of loop
		case OP_COVER:
			if (reg[ins.reg].type != VAL_NIL) {
				i++;
			}
			break;
		case OP_NIL:
			reg[ins.reg] = (val) {VAL_NIL};
			break;
		case OP_SETL:
			switch (init.literals.items[ins.lit].type) {
			case VAL_TAB:
				reg[ins.reg] = (val) {VAL_TAB, .tab = gc_alloc(sizeof(tab))};
				reg[ins.reg].tab->al = val_al_clone(&init.literals.items[ins.lit].tab->al);
				reg[ins.reg].tab->ht = val_ht_clone(&init.literals.items[ins.lit].tab->ht);
				break;
			default:
				reg[ins.reg] = init.literals.items[ins.lit];
				break;
			}
			break;
		case OP_ADD:
			if (reg[ins.rina].type == VAL_NUM
			&&  reg[ins.rinb].type == VAL_NUM) {
				reg[ins.rout] = (val) {VAL_NUM, reg[ins.rina].num + reg[ins.rinb].num};
				break;
			}
			break;
		case OP_SUB:
			if (reg[ins.rina].type == VAL_NUM
			&&  reg[ins.rinb].type == VAL_NUM) {
				reg[ins.rout] = (val) {VAL_NUM, reg[ins.rina].num - reg[ins.rinb].num};
				break;
			}
			break;
		case OP_GT:
			if (reg[ins.rina].type == VAL_NUM
			&&  reg[ins.rinb].type == VAL_NUM) {
				if (reg[ins.rina].num > reg[ins.rinb].num) {
					reg[ins.rout] = reg[ins.rinb];
				} else {
					reg[ins.rout] = (val) {VAL_NIL};
				}
				break;
			}
			break;
		case OP_GE:
			if (reg[ins.rina].type == VAL_NUM
			&&  reg[ins.rinb].type == VAL_NUM) {
				if (reg[ins.rina].num >= reg[ins.rinb].num) {
					reg[ins.rout] = reg[ins.rinb];
				} else {
					reg[ins.rout] = (val) {VAL_NIL};
				}
			}
			break;
		case OP_MOV:
			reg[ins.rout] = reg[ins.rina];
			break;
		case OP_END:
			printf("Register 0; ");
			print_val(reg[0]);

			printf("Register 1; ");
			print_val(reg[1]);
			return 0;
		default:
			break;
		}
		print_val(reg[0]);
		i++;
	}

	free(func.items);
	free(stack.items);
	return 0;
}
