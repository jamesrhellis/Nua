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

typedef struct tab {
	val_al al;
	val_ht ht;
} tab;

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

RH_AL_MAKE(inst_list, inst)
RH_AL_MAKE(inst_lines, int)
RH_HASH_MAKE(loc_map, char *, size_t, rh_string_hash, rh_string_eq, 0.9)

typedef struct func_def {
	inst_list ins;
	inst_lines lines;
	val_al literals;
} func_def;

#include "parse.c"

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
	size_t i = 0;
	val_al reg = val_al_new(256);

	while (true) {
		inst ins = init.ins.items[i];
		switch (ins.op) {
		case OP_COVER:
			if (reg.items[ins.reg].type != VAL_NIL) {
				i++;
			}
			break;
		case OP_JMP:
			i += ins.off - 1;
			break;
		case OP_NIL:
			reg.items[ins.reg] = (val) {VAL_NIL};
			break;
		case OP_SETL:
			reg.items[ins.reg] = init.literals.items[ins.lit];
			break;
		case OP_ADD:
			reg.items[ins.rout].num = reg.items[ins.rina].num + reg.items[ins.rinb].num;
			break;
		case OP_SUB:
			reg.items[ins.rout].num = reg.items[ins.rina].num - reg.items[ins.rinb].num;
			break;
		case OP_GT:
			if (reg.items[ins.rina].num > reg.items[ins.rinb].num) {
				reg.items[ins.rout] = reg.items[ins.rinb];
			} else {
				reg.items[ins.rout] = (val) {VAL_NIL};
			}
			break;
		case OP_GE:
			if (reg.items[ins.rina].num >= reg.items[ins.rinb].num) {
				reg.items[ins.rout] = reg.items[ins.rinb];
			} else {
				reg.items[ins.rout] = (val) {VAL_NIL};
			}
			break;
		case OP_MOV:
			reg.items[ins.rout] = reg.items[ins.rina];
			break;
		case OP_END:
			printf("Register 0; %f\n", reg.items[0].num);
			printf("Register 1; %f\n", reg.items[1].num);
			return 0;
		default:
			break;
		}
		i++;
	}
	return 0;
}
