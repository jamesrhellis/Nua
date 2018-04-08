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
	TOK_LOCAL, TOK_IF, TOK_THEN, TOK_ELSE, TOK_END,
	// Special symbols
	TOK_ASSIGN, TOK_EQ} tokt;

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

typedef struct {
	//Variable register allocation
	ident_map local;
	size_t reg;
	size_t temp;

	//Instructions and debug
	inst_list ins;
	inst_lines lines;

	//Literals
	val_al literals;
} f_data;

size_t alloc_literal(f_data *f, val value) {
	val_al_push(&f->literals, value);
	return f->literals.top-1;
}

size_t alloc_local(f_data *f, char *name) {
	size_t reg = f->reg++;
	ident_map_set(&f->local, name, reg);
	return reg;
}

size_t alloc_temp(f_data *f) {
	return f->reg + f->temp++;
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

int parse(lexer l, func_def *f) {
	lex_next(&l);
	f_data fd = {0};
	int err =  parse_code(&l, &fd);
	if (err) {
		return err;
	}
	f->ins = fd.ins;
	f->lines = fd.lines;
	f->literals = fd.literals;

	return 0;
}

int parse_code(lexer *l, f_data *f) {
	int err = 0;
	while (!err) {
		if (l->current.type == TOK_LOCAL) {
			err = parse_local(l, f);
		} else {
			err = parse_assign(l, f);
		}
	}

	push_inst(l, f, (inst) {OP_END});
	return 0;
}

/*
int parse_if(lexer *l, f_data *f) {
	if (l->current.type != TOK_IF) {
		return 1;
	}

	lex_next(l);
	size_t reg = alloc_temp(f);
	int err = parse_expr(l, f, reg);
	if (err) {
		return -1;
	}
	push_inst(l, f, (inst) {OP_COVER, reg});
	push_inst(l, f, (inst) {OP_JMP, reg});
	free_temp(f);
	return 0;
}
*/

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

	ident_map_bucket *local = ident_map_find(&f->local, l->current.lexme);
	if (!local) {
		// FIXME remove later in dev once globals and uptable added
		printf("Error in local map\n");
		return -1;
	}
	size_t reg = local->value;

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

int parse_expr(lexer *l, f_data *f, size_t reg) {
	if (l->current.type != TOK_NUM) {
		printf("Number is not\n");
		return 1;
	}

	val_al_push(&f->literals, (val) { VAL_NUM, l->current.num});
	push_inst(l, f, (inst) {OP_SETL, reg, f->literals.top-1});

	lex_next(l);
	return 0;
}

