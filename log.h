#ifndef LOG_H
#define LOG_H

#include "gen/rh_al.h"

struct error {
	char *err;
};

RH_AL_MAKE(error_al, struct error);

error_al parse_errors;

static inline void log_error(lexer *l, f_data *f, char *c) {
	error_al_push(&parse_errors, (struct error){.err=c});
}

static inline void print_error(struct error error) {
	fprintf(stderr, "%s\n", error.err);
}

#endif
