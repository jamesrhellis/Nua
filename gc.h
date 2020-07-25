#ifndef NUA_GC_H
#define NUA_GC_H

#include "val.h"
#include "gc_types.h"

void gc_sweep(mem_block *p) {
	if (!p) {
		return;
	}
	
	// TODO clear intern table before the gc
	mem_block **prev = &p->next;
	mem_block *current = p->next;
	int white = p->colour;

	while (current) {
		//puts("Here");
		if (white ==  current->colour) {
			mem_block *tofree = current;
			*prev = current->next;
			current = *prev;
			
			switch (tofree->tag) {
			case GC_TAB: {
				//printf("Freeing Table\n");
				tab *t = (tab *)tofree;
				val_al_free(&t->al);
				val_ht_free(&t->ht);
				break;
			} case GC_FUNC: {
				//printf("Freeing Func\n");
				func *f = (func *)tofree;
				// env will free itself
				// func_def will free itself
				break;
			} case GC_FUNCDEF: {
				//printf("Freeing Func def\n");
				func_def *d = (func_def *)tofree;
				inst_list_free(&d->ins);
				val_al_free(&d->literals);
				inst_lines_free(&d->lines);
				break;
			} default:
				// FIXME we are leaking strings currently
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
	d->link.colour = black;
	
	for (int i = 0;i < d->literals.top;++i) {
		gc_val_mark(&d->literals.items[i], black);
	}
}
void gc_tab_mark(tab *t, int black) {
	if (!t || t->link.colour == black) {
		return;
	}
	t->link.colour = black;
	//puts("Marking tab");
	
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
		v->func->link.colour = black;
		//puts("Marking func");
		gc_tab_mark(v->func->env, black);
		gc_func_def_mark(v->func->def, black);

		break;
	} case VAL_STR: {
		//puts("Marking str");
		v->str->link.colour = black;
		break;
	} default:
		break;
	}
}

#endif
