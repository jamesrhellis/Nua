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

typedef enum opcode  { OP_NOP,  OP_SETL, OP_END, OPCODE_NO} opcode;
char *opcode_str[OPCODE_NO] =
                     {"NOP","SETL", "END"};

typedef struct inst {
	uint8_t op;
	union {
		struct {
			uint8_t reg;
			uint16_t lit;
		};
		struct {
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
	size_t i = 0;
	val_al reg = val_al_new(256);

	while (true) {
		printf("%zu\n", i);
		inst ins = init.ins.items[i];
		switch (ins.op) {
		case OP_SETL:
			reg.items[ins.reg] = init.literals.items[ins.lit];
			break;
		case OP_END:
			printf("Register 0; %f\n", reg.items[0].num);
			return 0;
		}
		i++;
	}
}
