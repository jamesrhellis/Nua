#ifndef GC_H
#define GC_H

#include "val.h"
#include "gc_types.h"

void gc_sweep(mem_block **p, uint8_t white_tag) {
	if (!*p) {
		return;
	}
	
	// TODO clear intern table before the gc
	mem_block **prev = p;
	mem_block *current = *p;

	while (current) {
		if (white_tag ==  current->colour) {
			mem_block *tofree = current;
			*prev = current->next;
			current = *prev;
			
			switch (tofree->tag) {
			case GC_TAB: {
				printf("Freeing Table\n");
				tab *t = (tab *)tofree;
				val_al_free(&t->al);
				val_ht_free(&t->ht);
				break;
			} case GC_FUNC: {
				printf("Freeing Func\n");
				func *f = (func *)tofree;
				val_al_free(&f->upvals);
				// env will free itself
				// func_def will free itself
				break;
			} case GC_FUNCDEF: {
				printf("Freeing Func def\n");
				func_def *d = (func_def *)tofree;
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

		prev = &current->next;
		current = *prev;
	}
}

void gc_val_mark(val *v, int black);
void gc_func_def_mark(func_def *d, int black) {
	if (d->link.colour == black) {
		return;
	}
	
	for (int i = 0;i < d->literals.top;++i) {
		gc_val_mark(&d->literals.items[i], black);
	}
	d->link.colour = black;
}
void gc_tab_mark(tab *t, int black) {
	if (!t || t->link.colour == black) {
		return;
	}
	puts("Marking tab");
	
	for (int i = 0;i < t->al.top;++i) {
		gc_val_mark(&t->al.items[i], black);
	}
	if (t->ht.items) {
		for (int i = 0;i < RH_HASH_SIZE(t->ht.size);++i) {
			if (t->ht.hash[i]) {
				gc_val_mark(&t->ht.items[i].key, black);
				gc_val_mark(&t->ht.items[i].value, black);
			}
		}
	}
	t->link.colour = black;
}
void gc_val_mark(val *v, int black) {
	switch (v->type) {
	case VAL_TAB: {
		gc_tab_mark(v->tab, black);
		break;
	} case VAL_FUNC: {
		if (v->func->type == FUNC_C) {
			v->func->link.colour = black;
			return;
		}
		
		if (v->func->link.colour == black) {
			return;
		}
		puts("Marking func");
		for (int i = 0;i < v->func->upvals.top;++i) {
			gc_val_mark(&v->func->upvals.items[i], black);
		}
		gc_tab_mark(v->func->env, black);
		gc_func_def_mark(v->func->def, black);
		v->func->link.colour = black;
		break;
	} case VAL_STR: {
				puts("Marking str");
		v->str->link.colour = black;
		break;
	} default:
		break;
	}
}

mem_block *global_heap;

#endif
