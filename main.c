#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>

#include "gen/rh_tp.h"
#include "gen/rh_hash.h"
#include "gen/rh_al.h"

#include "gc.h"
#include "val.h"
#include "parse.h"

typedef struct frame {
	size_t reg_base;
	size_t ins;
	tab *env;

	func *func;
} frame;

RH_AL_MAKE(frame_stack, frame)

typedef struct thread {
	frame_stack func;
	val_al stack;

	// Mem management
	size_t min_stack;	// Lowest pos used since last gc
} thread;

RH_AL_MAKE(thread_list, thread)

typedef struct global {
	// Control
	thread_list threads;

	// Mem management
	size_t white;		// Current val of white tag (0, 1)
	mem_block *gc_list;	// All objects
	mem_grey_link *gc_grey;	// Incrementally processed objects
} global;

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

	func *base = calloc(sizeof(*base), 1);
	*base = (func) { FUNC_NUA, .def = calloc(sizeof(*base->def), 1)};

	if (parse((lexer){args[1], file, .lstart = file}, base->def) || parse_errors.items) {
		fprintf(stderr, "Unable to parse file!\n");
		rh_al_for(struct error e, parse_errors, {
			print_error(e);
		})

		return 1;
	}

	print_func_def(*base->def);

	global g = {0}; {
		thread t = {.stack = val_al_new(256), .func = frame_stack_new(8)};
		tab *env = gc_alloc(&g.gc_list, sizeof(tab));
		tab_set(env, (val){VAL_NUM, .num = 0}, (val){VAL_TAB, .tab = env});
		frame_stack_push(&t.func, (frame) {.func = base, .env = env});

		thread_list_push(&g.threads, t);
	}

	thread *t = &g.threads.items[0];
	frame *top = frame_stack_rpeek(&t->func);

	val *reg = &t->stack.items[top->reg_base];
	val *lit = top->func->def->literals.items;
	tab *env = top->env;

	while (true) {
		inst ins = top->func->def->ins.items[top->ins];
		switch (ins.op) {
		case OP_JMP:
			top->ins += ins.off;
			continue;	// Avoid addition at end of loop
		case OP_COVER:
			if (reg[ins.reg].type != VAL_NIL) {
				top->ins++;
			}
			break;
		case OP_NIL:
			reg[ins.reg] = (val) {VAL_NIL};
			break;
		case OP_SETI:
			reg[ins.reg] = (val) {VAL_NUM, .num = ins.ilit};
			break;
		case OP_SETL:
			switch (lit[ins.lit].type) {
			case VAL_TAB:
				reg[ins.reg] = (val) {VAL_TAB, .tab = gc_alloc(&g.gc_list, sizeof(tab))};
				reg[ins.reg].tab->al = val_al_clone(&lit[ins.lit].tab->al);
				reg[ins.reg].tab->ht = val_ht_clone(&lit[ins.lit].tab->ht);
				break;
			case VAL_FUNC:
				reg[ins.reg] = (val) {VAL_FUNC, .tab = gc_alloc(&g.gc_list, sizeof(tab))};
				*reg[ins.reg].func = (func) {FUNC_NUA, .def = lit[ins.lit].func->def};
				break;
			default:
				reg[ins.reg] = lit[ins.lit];
				break;
			}
			break;
		case OP_CALL:
			// OP is interpreted
			// .rout = func register, and base of func args - 1, base of return vals
			// .rina = no args, call has to pad with nils
			// .rinb = no return vals, return has to pad with nils
			frame_stack_push(&t->func, (frame) {.func = reg[ins.rout].func, .reg_base = &reg[ins.rout+1] - t->stack.items});
			size_t max = t->stack.top + reg[ins.rout].func->def->max_reg;

			if (max >= t->stack.size) {
				val_al_resize(&t->stack, t->stack.size * 2);
			}

			for (int i = ins.rina;i < reg[ins.rout].func->def->no_args;++i) {
				reg[ins.rout + i] = (val) { VAL_NIL };
			}

			top = frame_stack_rpeek(&t->func);

			reg = &t->stack.items[top->reg_base];
			lit = top->func->def->literals.items;
			if (top->func->env) {
				env = top->env = top->func->env;
			} else {
				env = (top-1)->env;
			}

			// Avoid skipping the first instruction
			continue;
		case OP_RET:
			// OP is interpreted
			// .rina = base register
			// .rout = no values to return
			frame_stack_pop(&t->func);
			size_t no_ret = ins.rout;

			reg -= 1;

			for (int i = 0;i < no_ret;++i) {
				reg[i] = reg[ins.rina + i + 1];
			}

			top = frame_stack_rpeek(&t->func);

			reg = &t->stack.items[top->reg_base];
			lit = top->func->def->literals.items;

			ins = top->func->def->ins.items[top->ins];
			for (int i = no_ret;i < ins.rinb;++i) {
				reg[ins.rout + i] = (val) { VAL_NIL };
			}

			env = top->env;

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
		case OP_TAB:
			reg[ins.rout] = (val) {VAL_TAB, .tab = gc_alloc(&g.gc_list, sizeof(tab))};
			val_ht_resize(&reg[ins.rout].tab->ht, ins.rina);
			val_al_resize(&reg[ins.rout].tab->al, RH_HASH_SIZE(ins.rinb));
			break;
		case OP_PTAB:
			switch (reg[ins.rout].type) {
			case VAL_TAB:
				tab_push(reg[ins.rout].tab, reg[ins.rina]);
				break;
			default:
				break;
			}
			break;
		case OP_STAB:
			switch (reg[ins.rout].type) {
			case VAL_TAB:
				tab_set(reg[ins.rout].tab, reg[ins.rina], reg[ins.rinb]);
				break;
			default:
				print_val(reg[ins.rout]);
				break;
			}
			break;
		case OP_GTAB:
			switch (reg[ins.rina].type) {
			case VAL_TAB:
				reg[ins.rout] = tab_get(reg[ins.rina].tab, reg[ins.rinb]);
				break;
			default:
				break;
			}
			break;
		case OP_SENV:
			tab_set(env, reg[ins.rina], reg[ins.rinb]);
			break;
		case OP_GENV:
			reg[ins.rout] = tab_get(env, reg[ins.rina]);
			break;
		case OP_END:
			printf("Register 0; ");
			print_val(reg[0]);

			printf("Register 1; ");
			print_val(reg[1]);

			free((void *) file);
			return 0;
		default:
			break;
		}
		top->ins++;
	}

	return 0;
}
