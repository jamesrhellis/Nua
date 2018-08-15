#ifndef GC_H
#define GC_H

#include "val.h"
#include "gc_types.h"

void gc_sweep(mem_block **p, uint8_t white_tag) {
	if (!*p) {
		return;
	}
	
	// TODO clear intern table before the gc
	mem_block **i = p;

	while ((*i)->next) {
		if (white_tag ==  mem_block_tag((*i)->next)) {
			mem_block *tofree = *i;
			set_next_mem_block(*i, (*i)->next);
			
			switch (mem_block_type(tofree)) {
			case GC_TAB: {
				tab *t = (tab *)i;
				val_al_free(&t->al);
				val_ht_free(&t->ht);
				break;
			} case GC_FUNC: {
				func *f = (func *)i;
				val_al_free(&f->upvals);
				// env will free itself
				// func_def will free itself
				break;
			} case GC_FUNCDEF: {
				func_def *d = (func_def *)i;
				inst_list_free(&d->ins);
				val_al_free(&d->literals);
				inst_lines_free(&d->lines);
				break;
			} default:
				break;
			}
				

			free(tofree);
			continue;
		}

		i = &((*i)->next);
	}
}

void gc_val_mark(val *v, int black);
void gc_func_def_mark(func_def *d, int black) {
	if (mem_block_col(&d->link) == black) {
		return;
	}
	
	for (int i = 0;i < d->literals.top;++i) {
		gc_val_mark(&d->literals.items[i], black);
	}
	mem_block_coltag(&d->link, black);
}
void gc_tab_mark(tab *t, int black) {
	if (mem_block_col(&t->link) == black) {
		return;
	}
	
	for (int i = 0;i < t->al.top;++i) {
		gc_val_mark(&t->al.items[i], black);
	}
	for (int i = 0;i < RH_HASH_SIZE(t->ht.size);++i) {
		if (t->ht.hash[i]) {
			gc_val_mark(&t->ht.items[i].key, black);
			gc_val_mark(&t->ht.items[i].value, black);
		}
	}
	mem_block_coltag(&t->link, black);
}
void gc_val_mark(val *v, int black) {
	switch (v->type) {
	case VAL_TAB: {
		gc_tab_mark(v->tab, black);
		break;
	} case VAL_FUNC: {
		if (mem_block_col(&v->func->link) == black) {
			return;
		}
		
		for (int i = 0;i < v->func->upvals.top;++i) {
			gc_val_mark(&v->func->upvals.items[i], black);
		}
		gc_tab_mark(v->func->env, black);
		gc_func_def_mark(v->func->def, black);
		mem_block_coltag(&v->func->link, black);
		break;
	} case VAL_STR: {
		mem_block_coltag(&v->str->link, black);
		break;
	} default:
		break;
	}
}

mem_block *global_heap;

#endif
