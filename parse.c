#include <stdio.h>
#include <errno.h>
#include <ctype.h>
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
	TOK_LOCAL, TOK_IF, TOK_THEN, TOK_ELSE, TOK_END, TOK_WHILE, TOK_DO, TOK_NIL,
	// Special symbols
	TOK_ASSIGN, TOK_EQ, TOK_ADD, TOK_SUB, TOK_GE, TOK_GT} tokt;

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
	sident_map_set(&sidents, "nil", TOK_NIL);
	return 0;
}


static inline token parse_ident(lexer *l) {
	const char *start = l->pos;

	while (isalnum(*l->pos++)) {
	}

	size_t len = l->pos - start - 1;
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
	l->current = __lex_next(l);
	return l->current.type != TOK_EOI;
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

	free(m.hash);
	free(m.items);

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
	size_t reg = f->reg++;
	if (f->reg > f->max_reg) {
		f->max_reg = f->reg;
	}

	ident_map_set(&f->scopes.items[f->scopes.top-1], name, reg);
	return reg;
}

size_t alloc_temp(f_data *f) {
	if (f->reg + ++f->temp > f->max_reg) {
		f->max_reg = f->reg + f->temp;
	}
	return f->reg + f->temp;
}

void free_temp(f_data *f) {
	f->temp--;
}

void push_inst(lexer *l, f_data *f, inst i) {
	inst_list_push(&f->ins, i);
	inst_lines_push(&f->lines, l->line);
}

int parse_code(lexer *l, f_data *f);
int parse_local(lexer *l, f_data *f);
int parse_assign(lexer *l, f_data *f);
int parse_expr(lexer *l, f_data *f, size_t reg);
int parse_bin_expr(lexer *l, f_data *f, size_t left, size_t precedence);
int parse_pexpr(lexer *l, f_data *f, size_t reg);
int parse_if(lexer *l, f_data *f);
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

int parse_local(lexer *l, f_data *f) {
	if (l->current.type != TOK_LOCAL) {
		return 1;
	}
	lex_next(l);
	if (l->current.type != TOK_IDENT) {
		return -1;
	}
	alloc_local(f, l->current.lexme);

	if (parse_assign(l, f)) {
		return -1;
	}

	return 0;
}

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

int emit_bin_code(lexer *l, f_data *f, tokt op, size_t left, size_t right) {
	switch (op) {
	case TOK_ADD:
		push_inst(l, f, (inst) {OP_ADD, .rout = left, .rina = left, .rinb =  right});
		break;
	case TOK_SUB:
		push_inst(l, f, (inst) {OP_SUB, .rout = left, .rina = left, .rinb =  right});
		break;
	case TOK_GT:
		push_inst(l, f, (inst) {OP_GT, .rout = left, .rina = left, .rinb =  right});
		break;
	case TOK_GE:
		push_inst(l, f, (inst) {OP_GE, .rout = left, .rina = left, .rinb =  right});
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

int parse_bin_expr(lexer *l, f_data *f, size_t left, size_t precedence) {
	parse_pexpr(l, f, left);
	size_t right = alloc_temp(f);

	while (bin_prec(l->current.type) && bin_prec(l->current.type) >= precedence) {
		tokt op = l->current.type;
		lex_next(l);

		parse_bin_expr(l, f, right, bin_prec(op)+bin_assoc(op));
		emit_bin_code(l, f, op, left, right);
	}

	free_temp(f);
	return 0;
}

int parse_pexpr(lexer *l, f_data *f, size_t reg) {
	switch (l->current.type) {
	case TOK_NIL:
		push_inst(l, f, (inst) {OP_NIL, reg, 0});
		lex_next(l);
		return 0;
	case TOK_NUM:
		push_inst(l, f, (inst) {OP_SETL, reg, f->literals.top});
		val_al_push(&f->literals, (val) {VAL_NUM, l->current.num});
		lex_next(l);
		return 0;
	case TOK_IDENT:{
		size_t *local = find_local(f, l->current.lexme);
		if (!local) {
			// FIXME remove later in dev once globals and uptable added
			printf("Error unable to find local!\n");
			return -1;
		}
		push_inst(l, f, (inst) {OP_MOV, .rout = reg, .rina = *local, .rinb = *local});
		lex_next(l);
		return 0;
	}
	default:
		return 1;
	}
}

