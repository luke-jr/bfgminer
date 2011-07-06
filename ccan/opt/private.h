#ifndef CCAN_OPT_PRIVATE_H
#define CCAN_OPT_PRIVATE_H

extern struct opt_table *opt_table;
extern unsigned int opt_count, opt_num_short, opt_num_short_arg, opt_num_long;

extern const char *opt_argv0;

#define subtable_of(entry) ((struct opt_table *)((entry)->names))

const char *first_sopt(unsigned *i);
const char *next_sopt(const char *names, unsigned *i);
const char *first_lopt(unsigned *i, unsigned *len);
const char *next_lopt(const char *p, unsigned *i, unsigned *len);

int parse_one(int *argc, char *argv[], unsigned *offset,
	      void (*errlog)(const char *fmt, ...));

#endif /* CCAN_OPT_PRIVATE_H */
