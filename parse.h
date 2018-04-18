#ifndef PARSE_H
#define PARSE_H

#include <stdio.h>
#include <errno.h>
#include <ctype.h>
#include <assert.h>
#include "gen/rh_hash.h"
#include "gen/rh_al.h"

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
	TOK_INDL, TOK_INDR, TOK_BRL, TOK_BRR, TOK_COM, TOK_DOT} tokt;

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
	const char *pos;
	size_t line;
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
	sident_map_set(&sidents, "fun", TOK_FUN);
	sident_map_set(&sidents, "ret", TOK_RET);
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

typedef struct {
	//Variable register allocation
	scope_al scopes;
	size_t reg;
	size_t temp;

	size_t max_reg;

	//Instructions and debug
	inst_list ins;
	inst_lines lines;

	//Literals
	val_al literals;
} f_data;

int add_scope(f_data *f) {
	scope_al_push(&f->scopes, (ident_map) {0});
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
		if (local = ident_map_find(&f->scopes.items[i], ident)) {
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
	val_al_push(&f->literals, value);
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

void push_inst(lexer *l, f_data *f, inst i) {
	inst_list_push(&f->ins, i);
	inst_lines_push(&f->lines, l->line+1);
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

	push_inst(&l, &fd, (inst) {OP_END});
	f->ins = fd.ins;
	f->max_reg = fd.max_reg;
	f->lines = fd.lines;
	f->literals = fd.literals;

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

	push_inst(l, f, (inst) {OP_RET, .rout = 1, .rina = t});

	free_temp(f);

	return 0;
}

RH_AL_MAKE(reg_al, uint8_t)
RH_AL_MAKE(ident_al, char *)

int parse_local(lexer *l, f_data *f) {
	if (l->current.type != TOK_LOCAL) {
		return 1;
	}
	lex_next(l);

	reg_al r = {0};
	if (l->current.type != TOK_IDENT) {
		return -1;
	}
	reg_al_push(&r, alloc_local(f,lex_claim_lexme(l)));
	lex_next(l);

	while (l->current.type == TOK_COM) {
		lex_next(l);
		if (l->current.type != TOK_IDENT) {
			return -1;
		}

		reg_al_push(&r, alloc_local(f,lex_claim_lexme(l)));
		lex_next(l);
	}

	if (l->current.type != TOK_ASSIGN) {
		printf("Error no assignment\n");
		return -1;
	}
	lex_next(l);

	size_t target = 0;
	if (parse_expr(l, f, r.items[target++])) {
		return -1;
	}

	while (l->current.type == TOK_COM) {
		lex_next(l);
		if (target >= r.top || parse_expr(l, f, r.items[target++])) {
			return -1;
		}
		lex_next(l);
	}

	if (target < r.top) {
		inst *i = inst_list_rpeek(&f->ins);
		if (i->op != OP_CALL && (--i)->op != OP_CALL) {
			return -1;
		}
		i->rinb += r.top - target;
		// First register output is already used
		size_t reg = i->rout + 1;
		while (target < r.top) {
			push_inst(l, f, (inst) {OP_MOV, .rout = r.items[target++], .rina = reg++});
		}
	}

	reg_al_free(&r);

	return 0;
}

enum ass_type { ASS_ERR, ASS_LOCAL, ASS_GLOB, ASS_TAB };
typedef struct assign {
	uint8_t type;
	union {
		uint8_t rout;
		uint8_t rname;
		struct {
			uint8_t rtab;
			uint8_t rkey;
		};
	};
} assign;
RH_AL_MAKE(ass_al, assign)

int parse_assign(lexer *l, f_data *f) {
	if (l->current.type != TOK_IDENT) {
		return 1;
	}

	size_t *local = find_local(f, l->current.lexme);
	if (!local) {
		// FIXME remove later in dev once globals and uptable added
		printf("Error in local map\n");
		return -1;
	}
	size_t reg = *local;

	lex_next(l);
	if (l->current.type != TOK_ASSIGN) {
		printf("Error no assignment\n");
		return -1;
	}

	lex_next(l);
	if (parse_expr(l, f, reg)) {
		printf("Unable to parse expr\n");
		return -1;
	}

	return 0;
}

int emit_bin_code(lexer *l, f_data *f, tokt op, size_t out, size_t left, size_t right) {
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
	size_t left = alloc_temp(f);
	if (parse_pexpr(l, f, left)) {
		free_temp(f);
		return 1;
	}

	size_t right = alloc_temp(f);
	while (bin_prec(l->current.type) && bin_prec(l->current.type) >= precedence) {
		tokt op = l->current.type;
		lex_next(l);

		parse_bin_expr(l, f, right, bin_prec(op)+bin_assoc(op));
		emit_bin_code(l, f, op, left, left, right);
	}

	// Fix the final instruction to output to out
	inst_list_rpeek(&f->ins)->rout = out;

	free_temp(f);
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
		if (inst_list_peek(&f->ins).op == OP_MOV) {
			inst temp = inst_list_pop(&f->ins);
			inst_list_push(&f->ins, (inst) { OP_PTAB, .rout = reg, .rina = temp.rout });
		} else {
			push_inst(l, f, (inst) {OP_PTAB, .rout = reg, .rina = temp});
		}

		if (l->current.type != TOK_COM) {
			if (l->current.type != TOK_TABR) {
				return -1;
			}
			break;
		}

		lex_next(l);
	}

	if (inst_list_peek(&f->ins).op != OP_TAB) {
		// Useless mov to allow for a retargetable op at end
		push_inst(l, f, (inst) {OP_MOV, .rout = reg, .rina = reg});
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

		push_inst(l, f, (inst) {OP_GTAB, .rout = reg, .rina = reg, .rinb = temp});

		free_temp(f);
		lex_next(l);
		break;
	}
	case TOK_DOT:{
		size_t temp = alloc_temp(f);
		lex_next(l);
		if (l->current.type != TOK_IDENT) {
			return -1;
		}

		push_inst(l, f, (inst) {OP_SETL, temp, f->literals.top});
		val_al_push(&f->literals, (val) {VAL_STR, .str = lex_claim_lexme(l)});

		push_inst(l, f, (inst) {OP_GTAB, .rout = reg, .rina = reg, .rinb = temp});

		free_temp(f);
		lex_next(l);
		break;
	}
	case TOK_BRL:{
		size_t f_reg = reg;
		if (reg < (f->reg + f->temp)) {
			f_reg = alloc_temp(f);
		}

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
		for (int i = 0;i < no_args;++i) {
			free_temp(f);
		}

		if (l->current.type != TOK_BRR) {
			return -1;
		}
		lex_next(l);

		push_inst(l, f, (inst) {OP_CALL, .rout = f_reg, .rina = no_args, .rinb = 1});

		if (reg != f_reg) {
			free_temp(f);
			push_inst(l, f, (inst) {OP_MOV, .rout = reg, .rina = f_reg});
		}
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

	if (l->current.type != TOK_END) {
		return -1;
	}
	lex_next(l);

	func_def *fun_def = calloc(sizeof(*fun_def), 1);
	fun_def->ins = fd.ins;
	fun_def->max_reg = fd.max_reg;
	fun_def->no_args = no_args;
	fun_def->lines = fd.lines;
	fun_def->literals = fd.literals;

	func *fun = calloc(sizeof(*fun), 1);
	*fun = (func) { FUNC_NUA, .def = fun_def};

	push_inst(l, f, (inst) { OP_SETL, reg, f->literals.top });
	alloc_literal(f, (val) { VAL_FUNC, .func = fun });

	return 0;
}

int parse_pexpr(lexer *l, f_data *f, size_t reg) {
	switch (l->current.type) {
	case TOK_NIL:
		push_inst(l, f, (inst) {OP_NIL, reg, 0});
		lex_next(l);
		break;
	case TOK_NUM:{
		double n = l->current.num;
		if (n == floor(n) && n > -32768 && n < 32767) {
			int16_t i = n;
			push_inst(l, f, (inst) {OP_SETI, reg, .ilit = i});
		} else {
			push_inst(l, f, (inst) {OP_SETL, reg, f->literals.top});
			val_al_push(&f->literals, (val) {VAL_NUM, l->current.num});
		}
			lex_next(l);
		break;
	} case TOK_TABL:
		if (parse_tab(l, f, reg)) {
			return 1;
		}
		break;
	case TOK_IDENT:{
		size_t *local = find_local(f, l->current.lexme);
		if (!local) {
			// FIXME remove later in dev once globals and uptable added
			printf("Error unable to find local!\n");
			return -1;
		}
		push_inst(l, f, (inst) {OP_MOV, .rout = reg, .rina = *local, .rinb = *local});
		lex_next(l);
		break;
	} case TOK_FUN:{
		lex_next(l);
		parse_fun(l, f, reg);
		break;
	} default:
		return 1;
	}
	parse_cont(l, f, reg);
}

#endif
