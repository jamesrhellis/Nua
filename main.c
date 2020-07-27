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

func *nua_load_file(nua_state *n, tab *env, char *file_name) {

	const char *file = load_file(file_name);
	if (!file) {
		fprintf(stderr, "Unable to load file!");
		return NULL;
	}

	func *file_func = nua_new_func(n, env);

	parser p = {
		file_name, file,
		.lstart = file,
		.gc_heap = &n->gc_list,
		.intern_map = &n->intern_map
	};

	if (parse(p, file_func->def)) {
		fprintf(stderr, "Unable to parse file!\n");

		free((void *) file);
		return NULL;
	}

	free((void *) file);
	return file_func;
}

int main(int argn, char **args) {
	if (argn < 2) {
		return 0;
	}

	nua_init();
	
	nua_state *n = nua_new_state();
	tab *env = nua_new_tab(n);

	func *base = nua_load_file(n, env, args[1]);
	if (!base) {
		return 1;
	}

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
