#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>

#include "gen/rh_al.h"

#include "gc.h"
#include "val.h"
#include "parse.h"

#include "core_api.h"

int nua_print_val(int no_args, val *stack) {
	if (!no_args) {
		return 0;
	}
	
	print_val(stack[1]);
	fflush(stdout);
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
	
	nua_state *n = nua_init();

	func *base = gc_alloc(&n->gc_list, sizeof(*base), GC_FUNC); {
		tab *env = gc_alloc(&n->gc_list, sizeof(tab), GC_TAB);
		*env = (tab) {0};
		*base = (func) {.type = FUNC_NUA, .def = gc_alloc(&n->gc_list, sizeof(*base->def), GC_FUNCDEF), .env = env};
	}

	if (parse((parser){args[1], file, .lstart = file, .gc_heap = &n->gc_list, .intern_map = &n->intern_map}, base->def)) {
		fprintf(stderr, "Unable to parse file!\n");

		return 1;
	}
	free((void *) file);
	
	print_func_def(*base->def);
	
	func *print =  gc_alloc(&n->gc_list, sizeof(*print), GC_FUNC);
	print->type = FUNC_C;
	print->c_func = &nua_print_val;
	
	tab_set(base->env, (val){VAL_STR, .str = intern(&n->gc_list, &n->intern_map, (slice) {
		.len = 5,
		.str = "print"})}, 
		(val){VAL_FUNC, .func = print});
		
	val_al_push(&n->stack, (val) {VAL_FUNC, .func = base});
		
	nua_call(n, 0, 0, 0);

	return 0;
}
