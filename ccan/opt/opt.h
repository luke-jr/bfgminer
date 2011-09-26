#ifndef CCAN_OPT_H
#define CCAN_OPT_H
#include <ccan/compiler/compiler.h>
#include <ccan/typesafe_cb/typesafe_cb.h>
#include <stdbool.h>
#include <stdlib.h>

struct opt_table;

/**
 * OPT_WITHOUT_ARG() - macro for initializing an opt_table entry (without arg)
 * @names: the names of the option eg. "--foo", "-f" or "--foo|-f|--foobar".
 * @cb: the callback when the option is found.
 * @arg: the argument to hand to @cb.
 * @desc: the description for opt_usage(), or opt_hidden.
 *
 * This is a typesafe wrapper for initializing a struct opt_table.  The callback
 * of type "char *cb(type *)", "char *cb(const type *)" or "char *cb(void *)",
 * where "type" is the type of the @arg argument.
 *
 * If the @cb returns non-NULL, opt_parse() will stop parsing, use the
 * returned string to form an error message for errlog(), free() the
 * string and return false.
 *
 * Any number of equivalent short or long options can be listed in @names,
 * separated by '|'.  Short options are a single hyphen followed by a single
 * character, long options are two hyphens followed by one or more characters.
 *
 * See Also:
 *	OPT_WITH_ARG()
 */
#define OPT_WITHOUT_ARG(names, cb, arg, desc)	\
	{ (names), OPT_CB_NOARG((cb), (arg)), { (arg) }, (desc) }

/**
 * OPT_WITH_ARG() - macro for initializing long and short option (with arg)
 * @names: the option names eg. "--foo=<arg>", "-f" or "-f|--foo <arg>".
 * @cb: the callback when the option is found (along with <arg>).
 * @show: the callback to print the value in get_usage (or NULL)
 * @arg: the argument to hand to @cb and @show
 * @desc: the description for opt_usage(), or opt_hidden.
 *
 * This is a typesafe wrapper for initializing a struct opt_table.  The callback
 * is of type "char *cb(const char *, type *)",
 * "char *cb(const char *, const type *)" or "char *cb(const char *, void *)",
 * where "type" is the type of the @arg argument.  The first argument to the
 * @cb is the argument found on the commandline.
 *
 * Similarly, if @show is not NULL, it should be of type "void *show(char *,
 * const type *)".  It should write up to OPT_SHOW_LEN bytes into the first
 * argument; unless it uses the entire OPT_SHOW_LEN bytes it should
 * nul-terminate that buffer.
 *
 * Any number of equivalent short or long options can be listed in @names,
 * separated by '|'.  Short options are a single hyphen followed by a single
 * character, long options are two hyphens followed by one or more characters.
 * A space or equals in @names is ignored for parsing, and only used
 * for printing the usage.
 *
 * If the @cb returns non-NULL, opt_parse() will stop parsing, use the
 * returned string to form an error message for errlog(), free() the
 * string and return false.
 *
 * See Also:
 *	OPT_WITHOUT_ARG()
 */
#define OPT_WITH_ARG(name, cb, show, arg, desc)	\
	{ (name), OPT_CB_ARG((cb), (show), (arg)), { (arg) }, (desc) }

/**
 * OPT_SUBTABLE() - macro for including another table inside a table.
 * @table: the table to include in this table.
 * @desc: description of this subtable (for opt_usage()) or NULL.
 */
#define OPT_SUBTABLE(table, desc)					\
	{ (const char *)(table), OPT_SUBTABLE,				\
	  sizeof(_check_is_entry(table)) ? NULL : NULL, NULL, NULL,	\
	  { NULL }, (desc) }

/**
 * OPT_ENDTABLE - macro to create final entry in table.
 *
 * This must be the final element in the opt_table array.
 */
#define OPT_ENDTABLE { NULL, OPT_END, NULL, NULL, NULL, { NULL }, NULL }

/**
 * opt_register_table - register a table of options
 * @table: the table of options
 * @desc: description of this subtable (for opt_usage()) or NULL.
 *
 * The table must be terminated by OPT_ENDTABLE.
 *
 * Example:
 * static int verbose = 0;
 * static struct opt_table opts[] = {
 * 	OPT_WITHOUT_ARG("--verbose", opt_inc_intval, &verbose,
 *			"Verbose mode (can be specified more than once)"),
 * 	OPT_WITHOUT_ARG("-v", opt_inc_intval, &verbose,
 *			"Verbose mode (can be specified more than once)"),
 * 	OPT_WITHOUT_ARG("--usage", opt_usage_and_exit,
 * 			"args...\nA silly test program.",
 *			"Print this message."),
 * 	OPT_ENDTABLE
 * };
 *
 * ...
 *	opt_register_table(opts, NULL);
 */
void opt_register_table(const struct opt_table *table, const char *desc);

/**
 * opt_register_noarg - register an option with no arguments
 * @names: the names of the option eg. "--foo", "-f" or "--foo|-f|--foobar".
 * @cb: the callback when the option is found.
 * @arg: the argument to hand to @cb.
 * @desc: the verbose description of the option (for opt_usage()), or NULL.
 *
 * This is used for registering a single commandline option which takes
 * no argument.
 *
 * The callback is of type "char *cb(type *)", "char *cb(const type *)"
 * or "char *cb(void *)", where "type" is the type of the @arg
 * argument.
 *
 * If the @cb returns non-NULL, opt_parse() will stop parsing, use the
 * returned string to form an error message for errlog(), free() the
 * string and return false.
 */
#define opt_register_noarg(names, cb, arg, desc)			\
	_opt_register((names), OPT_CB_NOARG((cb), (arg)), (arg), (desc))

/**
 * opt_register_arg - register an option with an arguments
 * @names: the names of the option eg. "--foo", "-f" or "--foo|-f|--foobar".
 * @cb: the callback when the option is found.
 * @show: the callback to print the value in get_usage (or NULL)
 * @arg: the argument to hand to @cb.
 * @desc: the verbose description of the option (for opt_usage()), or NULL.
 *
 * This is used for registering a single commandline option which takes
 * an argument.
 *
 * The callback is of type "char *cb(const char *, type *)",
 * "char *cb(const char *, const type *)" or "char *cb(const char *, void *)",
 * where "type" is the type of the @arg argument.  The first argument to the
 * @cb is the argument found on the commandline.
 *
 * At least one of @longopt and @shortopt must be non-zero.  If the
 * @cb returns false, opt_parse() will stop parsing and return false.
 *
 * Example:
 * static char *explode(const char *optarg, void *unused)
 * {
 *	errx(1, "BOOM! %s", optarg);
 * }
 * ...
 *	opt_register_arg("--explode|--boom", explode, NULL, NULL, opt_hidden);
 */
#define opt_register_arg(names, cb, show, arg, desc)			\
	_opt_register((names), OPT_CB_ARG((cb), (show), (arg)), (arg), (desc))

/**
 * opt_parse - parse arguments.
 * @argc: pointer to argc
 * @argv: argv array.
 * @errlog: the function to print errors
 *
 * This iterates through the command line and calls callbacks registered with
 * opt_register_table()/opt_register_arg()/opt_register_noarg().  If there
 * are unknown options, missing arguments or a callback returns false, then
 * an error message is printed and false is returned.
 *
 * On success, argc and argv are adjusted so only the non-option elements
 * remain, and true is returned.
 *
 * Example:
 *	if (!opt_parse(&argc, argv, opt_log_stderr)) {
 *		printf("You screwed up, aborting!\n");
 *		exit(1);
 *	}
 *
 * See Also:
 *	opt_log_stderr, opt_log_stderr_exit
 */
bool opt_parse(int *argc, char *argv[], void (*errlog)(const char *fmt, ...));

/**
 * opt_free_table - free the table.
 *
 * This frees the internal memory. Call this as the last
 * opt function.
 */
void opt_free_table(void);

/**
 * opt_log_stderr - print message to stderr.
 * @fmt: printf-style format.
 *
 * This is a helper for opt_parse, to print errors to stderr.
 *
 * See Also:
 *	opt_log_stderr_exit
 */
void opt_log_stderr(const char *fmt, ...);

/**
 * opt_log_stderr_exit - print message to stderr, then exit(1)
 * @fmt: printf-style format.
 *
 * Just like opt_log_stderr, only then does exit(1).  This means that
 * when handed to opt_parse, opt_parse will never return false.
 *
 * Example:
 *	// This never returns false; just exits if there's an erorr.
 *	opt_parse(&argc, argv, opt_log_stderr_exit);
 */
void opt_log_stderr_exit(const char *fmt, ...);

/**
 * opt_invalid_argument - helper to allocate an "Invalid argument '%s'" string
 * @arg: the argument which was invalid.
 *
 * This is a helper for callbacks to return a simple error string.
 */
char *opt_invalid_argument(const char *arg);

/**
 * opt_usage - create usage message
 * @argv0: the program name
 * @extra: extra details to print after the initial command, or NULL.
 *
 * Creates a usage message, with the program name, arguments, some extra details
 * and a table of all the options with their descriptions.  If an option has
 * description opt_hidden, it is not shown here.
 *
 * If "extra" is NULL, then the extra information is taken from any
 * registered option which calls opt_usage_and_exit().  This avoids duplicating
 * that string in the common case.
 *
 * The result should be passed to free().
 */
char *opt_usage(const char *argv0, const char *extra);

/**
 * opt_hidden - string for undocumented options.
 *
 * This can be used as the desc parameter if you want an option not to be
 * shown by opt_usage().
 */
extern const char opt_hidden[];

/* Maximum length of arg to show in opt_usage */
#define OPT_SHOW_LEN 80

/* Standard helpers.  You can write your own: */
/* Sets the @b to true. */
char *opt_set_bool(bool *b);
/* Sets @b based on arg: (yes/no/true/false). */
char *opt_set_bool_arg(const char *arg, bool *b);
void opt_show_bool(char buf[OPT_SHOW_LEN], const bool *b);
/* The inverse */
char *opt_set_invbool(bool *b);
void opt_show_invbool(char buf[OPT_SHOW_LEN], const bool *b);
/* Sets @b based on !arg: (yes/no/true/false). */
char *opt_set_invbool_arg(const char *arg, bool *b);

/* Set a char *. */
char *opt_set_charp(const char *arg, char **p);
void opt_show_charp(char buf[OPT_SHOW_LEN], char *const *p);

/* Set an integer value, various forms.  Sets to 1 on arg == NULL. */
char *opt_set_intval(const char *arg, int *i);
void opt_show_intval(char buf[OPT_SHOW_LEN], const int *i);
char *opt_set_floatval(const char *arg, float *f);
void opt_show_floatval(char buf[OPT_SHOW_LEN], const float *f);
char *opt_set_uintval(const char *arg, unsigned int *ui);
void opt_show_uintval(char buf[OPT_SHOW_LEN], const unsigned int *ui);
char *opt_set_longval(const char *arg, long *l);
void opt_show_longval(char buf[OPT_SHOW_LEN], const long *l);
char *opt_set_ulongval(const char *arg, unsigned long *ul);
void opt_show_ulongval(char buf[OPT_SHOW_LEN], const unsigned long *ul);

/* Increment. */
char *opt_inc_intval(int *i);

/* Display version string to stdout, exit(0). */
char *opt_version_and_exit(const char *version);

/* Display usage string to stdout, exit(0). */
char *opt_usage_and_exit(const char *extra);

/* Below here are private declarations. */
/* You can use this directly to build tables, but the macros will ensure
 * consistency and type safety. */
enum opt_type {
	OPT_NOARG = 1,		/* -f|--foo */
	OPT_HASARG = 2,		/* -f arg|--foo=arg|--foo arg */
	OPT_SUBTABLE = 4,	/* Actually, longopt points to a subtable... */
	OPT_END = 8,		/* End of the table. */
};

struct opt_table {
	const char *names; /* pipe-separated names, --longopt or -s */
	enum opt_type type;
	char *(*cb)(void *arg); /* OPT_NOARG */
	char *(*cb_arg)(const char *optarg, void *arg); /* OPT_HASARG */
	void (*show)(char buf[OPT_SHOW_LEN], const void *arg);
	union {
		const void *carg;
		void *arg;
		size_t tlen;
	} u;
	const char *desc;
};

/* Resolves to the four parameters for non-arg callbacks. */
#define OPT_CB_NOARG(cb, arg)				\
	OPT_NOARG,					\
	typesafe_cb_cast3(char *(*)(void *),	\
			  char *(*)(typeof(*(arg))*),	\
			  char *(*)(const typeof(*(arg))*),	\
			  char *(*)(const void *), (cb)),	\
	NULL, NULL

/* Resolves to the four parameters for arg callbacks. */
#define OPT_CB_ARG(cb, show, arg)					\
	OPT_HASARG, NULL,						\
	typesafe_cb_cast3(char *(*)(const char *,void *),	\
			  char *(*)(const char *, typeof(*(arg))*),	\
			  char *(*)(const char *, const typeof(*(arg))*), \
			  char *(*)(const char *, const void *),	\
			  (cb)),					\
	typesafe_cb_cast(void (*)(char buf[], const void *),		\
			 void (*)(char buf[], const typeof(*(arg))*), (show))

/* Non-typesafe register function. */
void _opt_register(const char *names, enum opt_type type,
		   char *(*cb)(void *arg),
		   char *(*cb_arg)(const char *optarg, void *arg),
		   void (*show)(char buf[OPT_SHOW_LEN], const void *arg),
		   const void *arg, const char *desc);

/* We use this to get typechecking for OPT_SUBTABLE */
static inline int _check_is_entry(struct opt_table *e UNUSED) { return 0; }

#endif /* CCAN_OPT_H */
