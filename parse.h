#ifndef NUA_PARSE_H
#define NUA_PARSE_H

#include <stdio.h>
#include <errno.h>
#include <ctype.h>
#include <assert.h>

#include "gen/rh_hash.h"
#include "gen/rh_al.h"

#include "intern.h"

static char *load_file(char *file_name) {
	if (!file_name) {
		return NULL;
	}

	//File is opened in binary mode to guarentee size found in bytes
	FILE *f = fopen(file_name, "rb");

	if (!f) {
		return NULL;
	}

	char *data = NULL;

	if (fseek(f, 0, SEEK_END)) {
		goto EXIT;
	}

	// Errno may be set from previous syscall
	errno = 0;
	long length = ftell(f);
	if (length == 1L && errno) {
		goto EXIT;
	}

	if (fseek(f, 0, SEEK_SET)) {
		goto EXIT;
	}

	data = malloc(length + 1);
	if (!data) {
		goto EXIT;
	}

	if (!fread(data, 1, length, f)) {
		free(data);
		data = NULL;
		goto EXIT;
	}

	data[length] = '\0';
	
EXIT:
	fclose(f);
	return data;
}

typedef enum tokt { 
	// General Token types
	TOK_ERR, TOK_IDENT, TOK_NUM, TOK_STR, TOK_EOI,
	// Special identifiers
	TOK_LOCAL, TOK_IF, TOK_THEN, TOK_ELSE, TOK_END, TOK_WHILE, TOK_DO, TOK_FUN, TOK_RET, TOK_NIL,
	// Special symbols
	TOK_ASSIGN, TOK_EQ, TOK_ADD, TOK_SUB, TOK_GE, TOK_GT, TOK_TABL, TOK_TABR,
	TOK_INDL, TOK_INDR, TOK_BRL, TOK_BRR, TOK_COM, TOK_DOT
} tokt;

typedef struct {
	tokt type;
	union {
		struct {
			char *lexme;
		};
		struct {
			double num;
		};
		struct {
			char *str;
		};
	};
} token;

typedef struct {
	const char *file;
	const char *pos;

	size_t line;
	const char *lstart;

	token current;
} lexer;

char unexpected_char[] = "Unexpected char!";
char unexpected_newl[] = "Unexpected newline in string literal!";

static inline token parse_symb(lexer *l) {
	switch (*l->pos++) {
	case '=':
		if (*l->pos == '=') {
			return (token) {
				TOK_EQ,
			};
		}
		return (token) {
			TOK_ASSIGN,
		};
	case '+':
		return (token) {
			TOK_ADD,
		};
	case '-':
		return (token) {
			TOK_SUB,
		};
	case '>':
		if (*l->pos == '=') {
			return (token) {
				TOK_GE,
			};
		}
		return (token) {
			TOK_GT,
		};
	case '{':
		return (token) {
			TOK_TABL,
		};
	case '}':
		return (token) {
			TOK_TABR,
		};
	case '[':
		return (token) {
			TOK_INDL,
		};
	case ']':
		return (token) {
			TOK_INDR,
		};
	case '(':
		return (token) {
			TOK_BRL,
		};
	case ')':
		return (token) {
			TOK_BRR,
		};
	case ',':
		return (token) {
			TOK_COM,
		};
	case '.':
		return (token) {
			TOK_DOT,
		};
	default:
		return (token) {
			TOK_ERR,
			unexpected_char
		};
	}
}

RH_HASH_MAKE(sident_map, char *, tokt, rh_string_hash, rh_string_eq, 0.9)
sident_map sidents = {0};

int parse_init(void) {
	sident_map_set(&sidents, "local", TOK_LOCAL);
	sident_map_set(&sidents, "if", TOK_IF);
	sident_map_set(&sidents, "then", TOK_THEN);
	sident_map_set(&sidents, "else", TOK_ELSE);
	sident_map_set(&sidents, "end", TOK_END);
	sident_map_set(&sidents, "while", TOK_WHILE);
	sident_map_set(&sidents, "do", TOK_DO);
	sident_map_set(&sidents, "function", TOK_FUN);
	sident_map_set(&sidents, "return", TOK_RET);
	sident_map_set(&sidents, "nil", TOK_NIL);
	return 0;
}


static inline token parse_ident(lexer *l) {
	const char *start = l->pos;

	while (isalnum(*++l->pos)) {
	}

	size_t len = l->pos - start;
	char *str = malloc(len + 1);
	memcpy(str, start, len);
	str[len] = '\0';
	sident_map_bucket *sp = sident_map_find(&sidents, str);
	if (sp) {
		free(str);
		return (token) {
			sp->value
		};
	}

	return (token) {
		TOK_IDENT,
		str
	};
}

static inline token parse_str(lexer *l) {
	const char *start = l->pos++;
	int escs = 0;
	int in_str = 1;
	while (in_str) {
		if (*l->pos == '\\') {
			escs++;
		} else if (*l->pos == '\n') {
			l->pos++;
			return (token) {
				TOK_ERR,
				unexpected_newl
			};
		} else if (*l->pos == '\"' && *(l->pos - 1) != '\\') {
			break;
		}
		l->pos++;
	}

	char *str = malloc(l->pos - start - escs + 1);
	int i = 0;
	while (start < l->pos) {
		str[i] = *start++;
		if (str[i] == '\\') {
			if (*start == 'n') {
				str[i] = '\n';
			} else {
				str[i] = *start;
			}
			start++;
		}
		i++;
	}
	str[i] = '\0';

	//Go past ending "
	l->pos++;
	
	return (token) {
		TOK_STR,
		str
	};
}

static inline token parse_no(lexer *l) {
	return (token) {
		TOK_NUM,
		.num = strtod(l->pos, (char **)&l->pos)
	};
}

token __lex_next(lexer *l) {
	while (isspace(*l->pos)) {
		if (*l->pos == '\n') {
			l->line++;
			l->lstart = l->pos;
			const char *line = l->pos + 1;
			/*
			while (*line && *line != '\n') {
				putchar(*line++);
			}
			putchar('\n');
			*/
		}
		l->pos++;
	}

	if (*l->pos == '\"') {
		return parse_str(l);
	} else if(isdigit(*l->pos)) {
		return parse_no(l);
	} else if (ispunct(*l->pos)) {
		return parse_symb(l);
	} else if (isalpha(*l->pos)) {
		return parse_ident(l);
	} else if (*l->pos == '\0') {
		return (token) {TOK_EOI};
	}

	return (token) {
		TOK_ERR,
		unexpected_char
	};
}

int lex_next(lexer *l) {
	switch (l->current.type) {
	case TOK_IDENT:
	case TOK_STR:
		free(l->current.lexme);
		break;
	default:
		break;
	}

	l->current = __lex_next(l);
	return l->current.type != TOK_EOI;
}

char *lex_claim_lexme(lexer *l) {
	char *lex = l->current.lexme;
	l->current.lexme = NULL;
	return lex;
}

RH_HASH_MAKE(ident_map, char *, size_t, rh_string_hash, rh_string_eq, 0.9)
RH_AL_MAKE(scope_al, ident_map)

RH_HASH_MAKE(val_map, val, size_t, val_hash, val_eq, 0.9)

typedef struct {
	//Variable register allocation
	scope_al scopes;
	size_t reg;
	size_t temp;

	size_t max_reg;

	//Instructions and debug
	inst_list ins;
	inst_lines lines;
	inst_lines gc_height;

	//Literals
	val_al literals;
	val_map lit_map;
} f_data;

#include "log.h"

int add_scope(f_data *f) {
	return scope_al_push(&f->scopes, (ident_map) {0});
}

int rem_scope(f_data *f) {
	ident_map m = scope_al_pop(&f->scopes);
	if (m.items) {
		for (size_t i = 0;i < (1 << m.size);++i) {
			if (!m.hash[i]) {
				continue;
			}
			free(m.items[i].key);
			--f->reg;
		}
	}

	ident_map_free(&m);

	return 0;
}

size_t *find_local(f_data *f, char *ident) {
	ident_map_bucket *local = NULL;
	for (int i = f->scopes.top-1;i >= 0;--i) {
		if ((local = ident_map_find(&f->scopes.items[i], ident))) {
			return &local->value;
		}
	}

	return NULL;
}

size_t *find_local_top(f_data *f, char *ident) {
	ident_map_bucket *local = ident_map_find(&f->scopes.items[f->scopes.top-1], ident);
	if (local) {
		return &local->value;
	}

	return NULL;
}

size_t alloc_literal(f_data *f, val value) {
	val_map_bucket *v = val_map_find(&f->lit_map, value);
	if (v) {
		return v->value;
	}

	val_al_push(&f->literals, value);
	val_map_set(&f->lit_map, value, f->literals.top-1);
	return f->literals.top-1;
}

size_t alloc_local(f_data *f, char *name) {
	assert(!f->temp);

	size_t reg = f->reg++;
	if (reg > f->max_reg) {
		f->max_reg = reg;
	}

	ident_map_set(&f->scopes.items[f->scopes.top-1], name, reg);
	return reg;
}

size_t alloc_temp(f_data *f) {
	if (f->reg + f->temp > f->max_reg) {
		f->max_reg = f->reg + f->temp;
	}
	return f->reg + f->temp++;
}

void free_temp(f_data *f) {
	f->temp--;
}

size_t trans_temp(f_data *f, char *name) {
	assert(f->temp == 1);
	free_temp(f);
	return alloc_local(f, name);
}

static inline int is_local(f_data *f, uint8_t reg) {
	return reg < f->reg;
}

void push_inst(lexer *l, f_data *f, inst i) {
	inst_list_push(&f->ins, i);
	inst_lines_push(&f->lines, l->line+1);
	inst_lines_push(&f->gc_height, f->reg + f->temp);
}

inst pop_inst(f_data *f) {
	inst_lines_pop(&f->lines);
	inst_lines_pop(&f->gc_height);
	return inst_list_pop(&f->ins);
}


int parse_code(lexer *l, f_data *f);
int parse_local(lexer *l, f_data *f);
int parse_assign(lexer *l, f_data *f);
int parse_expr(lexer *l, f_data *f, size_t reg);
int parse_bin_expr(lexer *l, f_data *f, size_t left, size_t precedence);
int parse_pexpr(lexer *l, f_data *f, size_t reg);
int parse_if(lexer *l, f_data *f);
int parse_ret(lexer *l, f_data *f);
int parse_while(lexer *l, f_data *f);

int parse(lexer l, func_def *f) {
	lex_next(&l);
	f_data fd = {0};
	add_scope(&fd);

	int err =  parse_code(&l, &fd);
	if (err) {
		return err;
	}
	rem_scope(&fd);

	free(fd.scopes.items);

	push_inst(&l, &fd, (inst) {OP_END});
	f->ins = fd.ins;
	f->max_reg = fd.max_reg;
	f->lines = fd.lines;
	f->gc_height = fd.gc_height;
	f->literals = fd.literals;
	f->file = l.file;

	return 0;
}

int parse_code(lexer *l, f_data *f) {
	int err = 0;
	while (!err) {
		switch (l->current.type) {
		case TOK_IF:
			err = parse_if(l, f);
			break;
		case TOK_WHILE:
			err = parse_while(l, f);
			break;
		case TOK_LOCAL:
			err = parse_local(l, f);
			break;
		case TOK_RET:
			err = parse_ret(l, f);
			break;
		default:
			err = parse_assign(l, f);
			break;
		}
	}

	return 0;
}

int parse_while(lexer *l, f_data *f) {
	if (l->current.type != TOK_WHILE) {
		return 1;
	}
	add_scope(f);

	lex_next(l);
	size_t reg = alloc_temp(f);
	size_t start = f->ins.top;
	int err = parse_expr(l, f, reg);
	if (err) {
		return -1;
	}
	if (l->current.type != TOK_DO) {
		return -1;
	}
	lex_next(l);

	push_inst(l, f, (inst) {OP_COVER, reg});
	size_t jmp_from = f->ins.top;
	push_inst(l, f, (inst) {OP_JMP});
	free_temp(f);

	parse_code(l, f);
	push_inst(l, f, (inst) {OP_JMP, .off = start - f->ins.top });
	f->ins.items[jmp_from].off = f->ins.top - jmp_from;

	if (l->current.type != TOK_END) {
		return -1;
	}
	lex_next(l);

	rem_scope(f);

	return 0;
}

int parse_if(lexer *l, f_data *f) {
	if (l->current.type != TOK_IF) {
		return 1;
	}

	add_scope(f);

	lex_next(l);
	size_t reg = alloc_temp(f);
	int err = parse_expr(l, f, reg);
	if (err) {
		return -1;
	}
	if (l->current.type != TOK_THEN) {
		return -1;
	}
	lex_next(l);

	push_inst(l, f, (inst) {OP_COVER, reg});
	size_t start = f->ins.top;
	push_inst(l, f, (inst) {OP_JMP});
	free_temp(f);

	parse_code(l, f);
	f->ins.items[start].off = f->ins.top - start;

	if (l->current.type == TOK_ELSE) {
		// Jump past exit jump added here
		f->ins.items[start].off += 1;

		size_t start = f->ins.top;
		push_inst(l, f, (inst) {OP_JMP});
		lex_next(l);

		parse_code(l, f);
		f->ins.items[start].off = f->ins.top - start;
	}

	if (l->current.type != TOK_END) {
		return -1;
	}
	lex_next(l);

	rem_scope(f);

	return 0;
}

int parse_ret(lexer *l, f_data *f) {
	if (l->current.type != TOK_RET) {
		return 1;
	}
	lex_next(l);

	size_t t = alloc_temp(f);
	int err = parse_expr(l, f, t);
	if (err) {
		return err;
	}

	size_t no_ret = 1;
	while (l->current.type == TOK_COM) {
		lex_next(l);
		if (parse_expr(l, f, alloc_temp(f))) {
			return -1;
		}

		++no_ret;
	}

	push_inst(l, f, (inst) {OP_RET, .rout = no_ret, .rina = t});

	for (int i = 0;i < no_ret;++i) {
		free_temp(f);
	}

	return 0;
}

RH_AL_MAKE(reg_al, uint8_t)
RH_AL_MAKE(ident_al, char *)

int parse_local(lexer *l, f_data *f) {
	if (l->current.type != TOK_LOCAL) {
		return 1;
	}
	lex_next(l);

	ident_al id = {0};
	if (l->current.type != TOK_IDENT) {
		log_error(l, f, "No identifier after local\n");
		return -1;
	}
	ident_al_push(&id, lex_claim_lexme(l));
	lex_next(l);

	while (l->current.type == TOK_COM) {
		lex_next(l);
		if (l->current.type != TOK_IDENT) {
			log_error(l, f, "No identifier after comma \n");
			return -1;
		}

		ident_al_push(&id, lex_claim_lexme(l));
		lex_next(l);
	}

	if (l->current.type != TOK_ASSIGN) {
		log_error(l, f, "Error no assignment\n");
		return -1;
	}
	lex_next(l);
	
	assert(f->temp == 0);
	
	size_t t = 0;
	// Temps are used to avoid data dependency workarounds, to reduce register use
	size_t reg = alloc_temp(f);

	if (parse_expr(l, f, reg)) {
		log_error(l, f, "No expression after local\n");
		return -1;
	}
	trans_temp(f, id.items[t++]);

	while (l->current.type == TOK_COM) {
		reg = alloc_temp(f);
		lex_next(l);

		if (t >= id.top) {
			log_error(l, f, "Excess of expressions after local\n");
			return -1;
		}
		if (parse_expr(l, f, reg)) {
			log_error(l, f, "Unable to parse expression after local\n");
			return -1;
		}

		trans_temp(f, id.items[t++]);
	}

	if (t < id.top) {
		inst *i = inst_list_rpeek(&f->ins);
		if (i->op != OP_CALL && (--i)->op != OP_CALL) {
			log_error(l, f, "Lack of expressions after local\n");
			return -1;
		}
		i->rinb += id.top - t;

		//size_t f_reg = i->rout;
		while (t < id.top) {
			reg = alloc_local(f, id.items[t++]);
			//assert(reg == ++f_reg);
		}
	}

	ident_al_free(&id);
	
	assert(f->temp == 0);
	return 0;
}

enum ass_type { ASS_ERR, ASS_LOCAL, ASS_ENV, ASS_TAB };
typedef struct assign {
	uint8_t type;
	union {
		uint8_t rout;
		uint16_t renv;
		struct {
			uint8_t rtab;
			uint8_t rkey;
		};
	};
} assign;
RH_AL_MAKE(ass_al, assign)

int emit_bin_code(lexer *l, f_data *f, tokt op, size_t out, size_t left, size_t right);
int parse_assign(lexer *l, f_data *f) {
	if (l->current.type != TOK_IDENT) {
		return 1;
	}
	
	size_t reg = alloc_temp(f);
	if (parse_pexpr(l, f, reg)) {
		log_error(l, f ,"Error invalid primary expression\n");
		return -1;
	}
	
	// Early return to parse primary expressions - e.g function calls
	if (l->current.type != TOK_COM && l->current.type != TOK_ASSIGN) {
		free_temp(f);
		return 0;
	}
	
	ass_al a = {0};
	goto SKIP_PARSE_EXPRESSION;
	
	while (l->current.type == TOK_COM) {
		lex_next(l);		
		if (parse_pexpr(l, f, reg)) {
			log_error(l, f ,"Error invalid target to assign\n");
			return -1;
		}
		
		SKIP_PARSE_EXPRESSION:;
		
		inst i = pop_inst(f);
		switch (i.op) {
		case OP_MOV:
			ass_al_push(&a, (assign) {ASS_LOCAL, i.rina});
			break;
		case OP_GENV:
			ass_al_push(&a, (assign) {ASS_ENV, .renv = i.lit});
			break;
		case OP_GTAB:
			// Key, Value and tab
			if (!is_local(f, i.rinb)) {
				// Register reserved for the table
				alloc_temp(f);
			}
			if (!is_local(f, i.rina)) {
				// Register reserved for the index
				reg = alloc_temp(f);
			}
			ass_al_push(&a, (assign) {ASS_TAB, .rtab = i.rina, .rkey = i.rinb});
			break;
		default:
			log_error(l, f, "Error non-assignable primary expression in assignment\n");
			return -1;
		}
	}
	
	int assign_op = 0;

	if (l->current.type != TOK_ASSIGN) {
		log_error(l, f, "Expected assignment operator\n");
		return -1;
	}
	lex_next(l);

	inst_list locals = {0};
	inst_list tabs_envs = {0};
	
	if (parse_expr(l, f, reg)) {
		log_error(l, f, "Error no rexpression\n");
		return -1;
	}
	
	int inline_mov(f_data *f, int reg);
	int claim_temps(f_data *f, inst ins, int reg);

	int t = 0;
	while (1) {
		switch (a.items[t].type) {
		case ASS_LOCAL: {
			inst ins = pop_inst(f);
			if (op_retarget[ins.op] && !assign_op) {
				ins.rout = a.items[t].rout;
				reg = claim_temps(f, ins, reg);
				inst_list_push(&locals, ins);
			} else {
				push_inst(l, f, ins);
				inst_list_push(&locals, (inst) {OP_MOV, .rout = a.items[t].rout, .rina = reg});
				reg = alloc_temp(f);
			}
			break;
		} case ASS_ENV:
			inst_list_push(&tabs_envs, (inst) { OP_SENV, .reg = inline_mov(f, reg), .lit = a.items[t].renv});
			reg = alloc_temp(f);
			break;
		case ASS_TAB:
			inst_list_push(&tabs_envs, (inst) { OP_STAB, .rout = a.items[t].rtab,
				.rina = a.items[t].rkey, .rinb = inline_mov(f, reg)});
			reg = alloc_temp(f);
			break;
		}
		
		++t;
		
		if (l->current.type == TOK_COM) {
			lex_next(l);
			if (parse_expr(l, f, reg)) {
				log_error(l, f, "Expected an rexpression following comma\n");
				return -1;
			}
		} else if (t < a.top) {
			inst *i = inst_list_rpeek(&f->ins);
			if (i->op != OP_CALL) {
				log_error(l, f, "Insufficent rexpressions to assign \n");
				return -1;
			}
			// Func must return one more
			++i->rina;
			push_inst(l, f, (inst) {OP_MOV, .rout = reg, .rina = reg});
		} else {
			break;
		}
	}

	if (assign_op) {
		// TODO - emmit assignment operator code here
		for (int i = 0;i < locals.top;++i) {
			// Guarenteed to be a move instruction
			inst ins = locals.items[i];
			emit_bin_code(l, f, assign_op, ins.rout, ins.rout, ins.rina);
			ins = pop_inst(f);
			locals.items[i] = ins;
		}
		
		for (int i = 0;i < tabs_envs.top;++i) {
			inst ins = tabs_envs.items[i];
			switch (ins.op) {
			case OP_STAB:
				// Reg is still availible for computation
				push_inst(l, f, (inst) {OP_GTAB, .rout = reg, .rina = ins.rout, .rinb = ins.rina});
				emit_bin_code(l, f, assign_op, ins.rinb, ins.rinb, reg);
			case OP_SENV:
				push_inst(l, f, (inst) {OP_GENV, .reg = reg, .lit = ins.lit});
				emit_bin_code(l, f, assign_op, ins.reg, ins.reg, reg);
			default:
				log_error(l, f, "Internal Error: Unexpected instruction in tab/env assignment\n");
				return -1;
			}
		}
	}
	

	int redirect(inst op, int redir_reg, inst_list ops);
	
	// Free all temps used for tab indexing
	inst ins;
	while ((ins = inst_list_pop(&locals)).op) {
		if (redirect(ins, reg,  locals) || redirect(ins, reg, tabs_envs)) {
			push_inst(l, f, (inst) {OP_MOV, .rout = reg, .rina = ins.rout});
			reg = alloc_temp(f);
		}
		push_inst(l, f, ins);
	}
	
	while ((ins = inst_list_pop(&tabs_envs)).op) {
		push_inst(l, f, ins);
	}


	// FIXME - may be hiding temp allocation bugs
	f->temp = 0;

	return 0;
}
int inline_mov(f_data *f, int reg) {
	inst *i = inst_list_rpeek(&f->ins);
	if (i->op == OP_MOV) {
		pop_inst(f);
		return i->rina;
	}
	return reg;
}
int claim_temps(f_data *f, inst ins, int reg) {
	switch (opcode_type[ins.op]) {
	// Safe for all possible ops
	case OPT_RRR:
		if (!is_local(f, ins.rinb)) {
			reg = alloc_temp(f);
		}
	case OPT_RR: // Fallthrough
		if (!is_local(f, ins.rina)) {
			reg = alloc_temp(f);
		}
		break;
	default:
		break;
	}
	return reg;
}
int redirect(inst op, int redir_reg, inst_list ops) {
	int reg = op.rout;
	int redir = 0;
	inst ins;
	// Local copy safe to iterate with pops
	while ((ins = inst_list_pop(&ops)).op) {
		// Not safe for call/ret, but safe for 
		// all redirectable ops
		switch (opcode_type[ins.op]) {
		case OPT_RRR:
			if (ins.rinb == reg) {
				ins.rinb = redir_reg;
				redir = 1;
			}
		case OPT_RR: // Fallthrough
			if (ins.rina == reg) {
				ins.rina = redir_reg;
				redir = 1;
			}
			ops.items[ops.top] = ins;
			break;
		default:
			break;
		}
	}
	
	return redir;
}

int emit_bin_code(lexer *l, f_data *f, tokt op, size_t out, size_t left, size_t right) {
	inst *i = inst_list_rpeek(&f->ins);

	switch (op) {
	case TOK_ADD:
		push_inst(l, f, (inst) {OP_ADD, .rout = out, .rina = left, .rinb =  right});
		break;
	case TOK_SUB:
		push_inst(l, f, (inst) {OP_SUB, .rout = out, .rina = left, .rinb =  right});
		break;
	case TOK_GT:
		push_inst(l, f, (inst) {OP_GT, .rout = out, .rina = left, .rinb =  right});
		break;
	case TOK_GE:
		push_inst(l, f, (inst) {OP_GE, .rout = out, .rina = left, .rinb =  right});
		break;
	default:
		break;
	}
	return 0;
}


static inline int bin_prec(tokt op) {
	switch (op) {
	case TOK_ADD:
		return 4;
	case TOK_SUB:
		return 4;
	case TOK_GT:
		return 1;
	case TOK_GE:
		return 1;
	default:
		return 0;
	}
}

static inline int bin_assoc(tokt op) {
	switch (op) {
	default:
		return 1;
	}
}

int parse_expr(lexer *l, f_data *f, size_t reg) {
	return parse_bin_expr(l, f, reg, 0);
}

int parse_bin_expr(lexer *l, f_data *f, size_t out, size_t precedence) {
	size_t left = out;

	if (parse_pexpr(l, f, left)) {
		return 1;
	}
	
	inst i = inst_list_peek(&f->ins);
	if (i.op == OP_MOV && (bin_prec(l->current.type) && bin_prec(l->current.type) >= precedence)) {
		pop_inst(f);
		left = i.rina;
	}

	size_t right = alloc_temp(f);
	size_t temp = right;
	while (bin_prec(l->current.type) && bin_prec(l->current.type) >= precedence) {
		right = temp;
		tokt op = l->current.type;
		lex_next(l);

		parse_bin_expr(l, f, right, bin_prec(op)+bin_assoc(op));

		right = inline_mov(f, right);
		
		emit_bin_code(l, f, op, out, left, right);
	}

	free_temp(f);

	return 0;
}

int parse_tab(lexer *l, f_data *f, size_t reg) {
	if (l->current.type !=  TOK_TABL) {
		return 1;
	}
	lex_next(l);

	push_inst(l, f, (inst) {OP_TAB, reg});

	size_t temp = alloc_temp(f);
	while (!parse_expr(l, f, temp)) {
		// Avoid unnessesary move instructions
		push_inst(l, f, (inst) {OP_PTAB, .rout = reg, .rina = inline_mov(f, temp)});

		if (l->current.type != TOK_COM) {
			if (l->current.type != TOK_TABR) {
				return -1;
			}
			break;
		}

		lex_next(l);
	}

	free_temp(f);

	if (l->current.type !=  TOK_TABR) {
		return -1;
	}
	lex_next(l);

	return 0;
}

int parse_cont(lexer *l, f_data *f, size_t reg) {
	switch (l->current.type) {
	case TOK_INDL:{
		size_t temp = alloc_temp(f);
		lex_next(l);

		parse_expr(l, f, temp);
		if (l->current.type != TOK_INDR) {
			return -1;
		}
		lex_next(l);

		push_inst(l, f, (inst) {OP_GTAB, .rout = reg, .rina = reg, .rinb = temp});

		free_temp(f);
		break;
	}
	case TOK_DOT:{
		size_t temp = alloc_temp(f);
		lex_next(l);
		if (l->current.type != TOK_IDENT) {
			return -1;
		}

		char *ident = lex_claim_lexme(l);
		push_inst(l, f, (inst) {OP_SETL, temp, alloc_literal(f, (val) {VAL_STR,
					.str = intern(&global_heap, &global_intern_map, (slice) {
						.len = strlen(ident),
						.str = ident })
					})
				});
		free(ident);

		push_inst(l, f, (inst) {OP_GTAB, .rout = reg, .rina = reg, .rinb = temp});

		free_temp(f);
		lex_next(l);
		break;
	}
	case TOK_BRL:{
		size_t f_reg = reg;
		// func cannot be called from any reg other than the top - args needed
		assert(reg == (f->reg + f->temp - 1));

		if (inst_list_peek(&f->ins).op == OP_MOV) {
			inst temp = inst_list_pop(&f->ins);
			inst_list_push(&f->ins, (inst) { OP_MOV, .rout = f_reg, .rina = temp.rina });
		}
		lex_next(l);

		size_t no_args = 0;
		size_t temp = alloc_temp(f);
		while (!parse_expr(l, f, temp)) {
			++no_args;

			if (l->current.type == TOK_COM) {
				lex_next(l);
			}

			temp = alloc_temp(f);
		}

		free_temp(f);
		
		push_inst(l, f, (inst) {OP_CALL, .rout = f_reg, .rina = no_args, .rinb = 1});
		
		for (int i = 0;i < no_args;++i) {
			free_temp(f);
		}

		if (l->current.type != TOK_BRR) {
			return -1;
		}
		lex_next(l);



		break;
	}default:
		break;
	}
	return 0;
}

int parse_fun(lexer *l, f_data *f, size_t reg) {
	if (l->current.type != TOK_BRL) {
		return 1;
	}
	lex_next(l);

	f_data fd = {0};
	add_scope(&fd);

	size_t no_args = 0;
	while (l->current.type == TOK_IDENT) {
		++no_args;
		alloc_local(&fd, lex_claim_lexme(l));
		lex_next(l);

		if (l->current.type == TOK_COM) {
			lex_next(l);
		} else {
			break;
		}
	}

	if (l->current.type != TOK_BRR) {
		return -1;
	}
	lex_next(l);

	int err = parse_code(l, &fd);
	if (err) {
		return err;
	}
	rem_scope(&fd);

	free(fd.scopes.items);

	if (l->current.type != TOK_END) {
		return -1;
	}
	lex_next(l);

	func_def *fun_def = gc_alloc(&global_heap, sizeof(*fun_def), GC_FUNCDEF);
	fun_def->ins = fd.ins;
	fun_def->max_reg = fd.max_reg;
	fun_def->no_args = no_args;
	fun_def->lines = fd.lines;
	fun_def->gc_height = fd.gc_height;
	fun_def->literals = fd.literals;

	fun_def->file = l->file;

	func *fun = gc_alloc(&global_heap, sizeof(*fun), GC_FUNC);
	fun->type = FUNC_NUA;
	fun->def = fun_def;

	push_inst(l, f, (inst) { OP_SETL, reg
		, alloc_literal(f, (val) { VAL_FUNC, .func = fun })});

	return 0;
}

int parse_pexpr(lexer *l, f_data *f, size_t reg) {
	switch (l->current.type) {
	case TOK_NIL:
		push_inst(l, f, (inst) {OP_NIL, reg, 0});
		lex_next(l);
		break;
	case TOK_NUM:
		push_inst(l, f, (inst) {OP_SETL, reg
			, alloc_literal(f, (val) {VAL_NUM, l->current.num})});
		lex_next(l);
		break;
	case TOK_TABL:
		if (parse_tab(l, f, reg)) {
			log_error(l, f, "Error unable parse tab\n");
			return 1;
		}
		break;
	case TOK_IDENT:{
		size_t *local = find_local(f, l->current.lexme);
		if (local) {
			push_inst(l, f, (inst) {OP_MOV, .rout = reg, .rina = *local});
		} else {
			char *ident = lex_claim_lexme(l);
			int lit = alloc_literal(f, (val) {VAL_STR,
				.str = intern(&global_heap, &global_intern_map, (slice) {
					.len = strlen(ident),
					.str = ident,
				})
			});
			free(ident);
			push_inst(l, f, (inst) {OP_GENV, .reg = reg, .lit = lit});
		}
		lex_next(l);
		break;
	} case TOK_FUN:{
		lex_next(l);
		parse_fun(l, f, reg);
		break;
	} default:
		return 1;
	}
	return parse_cont(l, f, reg);
}

#endif