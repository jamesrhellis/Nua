#ifndef NUA_API
#define NUA_API

typedef struct nua_state {
	val_al stack;
	
	// Mem management
	size_t white;		// Current val of white tag (0, 1)
	mem_block *gc_list;	// All objects
	str_map intern_map;
} nua_state;

void gc_mark(nua_state *n, int height) {
	n->white = !n->white;
	int white = n->white;
	
	for (int i = 0;i < height+1;++i) {
		gc_val_mark(&n->stack.items[i], !white);
	}
}

int nua_call(nua_state *n, int arg_base, int no_args, int no_returns);
int nua_pcall(nua_state *n, int arg_base, int no_args, int no_returns);

int nua_c_func(nua_state *n, int arg_base, int no_args, int no_returns);

nua_state *nua_init() {
	nua_state *n = malloc(sizeof(*n));
	*n = (nua_state) {.stack = val_al_new(256)};
	return n;
}

int nua_call(nua_state *n, int base, int no_args, int no_returns) {	
	int pc = 0;
	
	func *f = n->stack.items[base].func;
	val *lit = f->def->literals.items;
	tab *env = f->env;
	
	size_t max = base + f->def->max_reg;

	if (max >= n->stack.size) {
		size_t old = n->stack.size, new = n->stack.size * 2;
		val_al_resize(&n->stack, new);
	}

	// MAY BE INVALIDATED DURING FUNCTION CALL DUE TO RESIZE
	val *reg = &n->stack.items[base + 1];

	for (int i = no_args;i < f->def->no_args;++i) {
		reg[base + i + 1] = (val) { VAL_NIL };
	}

	while (1) {
		inst ins = f->def->ins.items[pc];
		// printf("%d", pc);print_inst(ins);
		switch (ins.op) {
		case OP_COVER:	
			pc++;
			if (reg[ins.reg].type != VAL_NIL) {
				break;
			} else {
				ins = f->def->ins.items[pc];
			} // fall through	
		case OP_JMP:
			pc += ins.off;
			continue;	// Avoid addition at end of loop
		case OP_NIL:
			reg[ins.reg] = (val) {VAL_NIL};
			break;
		case OP_SETL:
			switch (lit[ins.lit].type) {
			case VAL_TAB:
				reg[ins.reg] = (val) {VAL_TAB, .tab = gc_alloc(&global_heap, sizeof(tab), GC_TAB)};
				reg[ins.reg].tab->al = val_al_clone(&lit[ins.lit].tab->al);
				reg[ins.reg].tab->ht = val_ht_clone(&lit[ins.lit].tab->ht);
				break;
			case VAL_FUNC:
				reg[ins.reg] = (val) {VAL_FUNC, .func = gc_alloc(&global_heap, sizeof(func), GC_FUNC)};
				reg[ins.reg].func->type = FUNC_NUA;
				reg[ins.reg].func->def = lit[ins.lit].func->def;
				reg[ins.reg].func->env = f->env;
				break;
			default:
				reg[ins.reg] = lit[ins.lit];
				break;
			}
			break;
		case OP_CALL: {
			if (reg[ins.rout].type != VAL_FUNC) {
				printf("Attempt to call non-function!\n");
				print_val(reg[ins.rout]);
				return -1;
			}
			// OP is interpreted
			// .rout = func register, and base of func args - 1, base of return vals
			// .rina = no args, call has to pad with nils
			// .rinb = no return vals, return has to pad with nils
			int no_ret;
			switch (reg[ins.rout].func->type) {
			case FUNC_NUA:
				no_ret = nua_call(n, base + 1 + ins.rout, ins.rina, ins.rinb);
				break;
			case FUNC_C:	
				no_ret = reg[ins.rout].func->c_func(ins.rina, &n->stack.items[base + 1 + ins.rout]);				
				break;
			default:
				return -1;
			}

			val *reg = &n->stack.items[base + 1];	
									
			for (int i = no_ret;i < ins.rinb;++i) {
				reg[ins.rout + i] = (val) { VAL_NIL };
			}
			break;
		} case OP_RET: {
			// OP is interpreted
			// .rina = base register
			// .rout = no values to return
			for (int i = 0;i < ins.rout;++i) {
				n->stack.items[base + i] = reg[ins.rina + i];
			}
			
			return ins.rout;
		} case OP_ADD:
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
			} else {
				print_val(reg[ins.rina]);
				print_val(reg[ins.rinb]);
				return -1;
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
			reg[ins.rout] = (val) {VAL_TAB, .tab = gc_alloc(&global_heap, sizeof(tab), GC_TAB)};
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
			tab_set(env, lit[ins.lit], reg[ins.reg]);
			break;
		case OP_GENV:
			reg[ins.reg] = tab_get(env, lit[ins.lit]);
			break;
		default:
			break;
		}
		//gc_mark(n, base + f->def->gc_height.items[pc]);
		//gc_sweep(&global_heap, n->white);
		
		pc++;
	}

	return 0;
}
#endif