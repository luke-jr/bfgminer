#include <ccan/opt/opt.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include "private.h"

/* Upper bound to sprintf this simple type?  Each 3 bits < 1 digit. */
#define CHAR_SIZE(type) (((sizeof(type)*CHAR_BIT + 2) / 3) + 1)

/* FIXME: asprintf module? */
static char *arg_bad(const char *fmt, const char *arg)
{
	char *str = malloc(strlen(fmt) + strlen(arg));
	sprintf(str, fmt, arg);
	return str;
}

char *opt_set_bool(bool *b)
{
	*b = true;
	return NULL;
}

char *opt_set_invbool(bool *b)
{
	*b = false;
	return NULL;
}

char *opt_set_bool_arg(const char *arg, bool *b)
{
	if (!strcasecmp(arg, "yes") || !strcasecmp(arg, "true"))
		return opt_set_bool(b);
	if (!strcasecmp(arg, "no") || !strcasecmp(arg, "false"))
		return opt_set_invbool(b);

	return opt_invalid_argument(arg);
}

char *opt_set_invbool_arg(const char *arg, bool *b)
{
	char *err = opt_set_bool_arg(arg, b);

	if (!err)
		*b = !*b;
	return err;
}

/* Set a char *. */
char *opt_set_charp(const char *arg, char **p)
{
	*p = (char *)arg;
	return NULL;
}

/* Set an integer value, various forms.  Sets to 1 on arg == NULL. */
char *opt_set_intval(const char *arg, int *i)
{
	long l;
	char *err = opt_set_longval(arg, &l);

	if (err)
		return err;
	*i = l;
	/* Beware truncation... */
	if (*i != l)
		return arg_bad("value '%s' does not fit into an integer", arg);
	return err;
}

char *opt_set_floatval(const char *arg, float *f)
{
	char *endp;

	errno = 0;
	*f = strtof(arg, &endp);
	if (*endp || !arg[0])
		return arg_bad("'%s' is not a number", arg);
	if (errno)
		return arg_bad("'%s' is out of range", arg);
	return NULL;
}

char *opt_set_uintval(const char *arg, unsigned int *ui)
{
	int i;
	char *err = opt_set_intval(arg, &i);

	if (err)
		return err;
	if (i < 0)
		return arg_bad("'%s' is negative", arg);
	*ui = i;
	return NULL;
}

char *opt_set_longval(const char *arg, long *l)
{
	char *endp;

	/* This is how the manpage says to do it.  Yech. */
	errno = 0;
	*l = strtol(arg, &endp, 0);
	if (*endp || !arg[0])
		return arg_bad("'%s' is not a number", arg);
	if (errno)
		return arg_bad("'%s' is out of range", arg);
	return NULL;
}

char *opt_set_ulongval(const char *arg, unsigned long *ul)
{
	long int l;
	char *err;
	
	err = opt_set_longval(arg, &l);
	if (err)
		return err;
	*ul = l;
	if (l < 0)
		return arg_bad("'%s' is negative", arg);
	return NULL;
}

char *opt_inc_intval(int *i)
{
	(*i)++;
	return NULL;
}

/* Display version string. */
char *opt_version_and_exit(const char *version)
{
	printf("%s\n", version);
	fflush(stdout);
	exit(0);
}

char *opt_usage_and_exit(const char *extra)
{
	printf("%s", opt_usage(opt_argv0, extra));
	fflush(stdout);
	exit(0);
}

void opt_show_bool(char buf[OPT_SHOW_LEN], const bool *b)
{
	strncpy(buf, *b ? "true" : "false", OPT_SHOW_LEN);
}

void opt_show_invbool(char buf[OPT_SHOW_LEN], const bool *b)
{
	strncpy(buf, *b ? "false" : "true", OPT_SHOW_LEN);
}

void opt_show_charp(char buf[OPT_SHOW_LEN], char *const *p)
{
	size_t len = strlen(*p);
	buf[0] = '"';
	if (len > OPT_SHOW_LEN - 2)
		len = OPT_SHOW_LEN - 2;
	strncpy(buf+1, *p, len);
	buf[1+len] = '"';
	if (len < OPT_SHOW_LEN - 2)
		buf[2+len] = '\0';
}

/* Set an integer value, various forms.  Sets to 1 on arg == NULL. */
void opt_show_intval(char buf[OPT_SHOW_LEN], const int *i)
{
	snprintf(buf, OPT_SHOW_LEN, "%i", *i);
}

void opt_show_floatval(char buf[OPT_SHOW_LEN], const float *f)
{
	snprintf(buf, OPT_SHOW_LEN, "%.1f", *f);
}

void opt_show_uintval(char buf[OPT_SHOW_LEN], const unsigned int *ui)
{
	snprintf(buf, OPT_SHOW_LEN, "%u", *ui);
}

void opt_show_longval(char buf[OPT_SHOW_LEN], const long *l)
{
	snprintf(buf, OPT_SHOW_LEN, "%li", *l);
}

void opt_show_ulongval(char buf[OPT_SHOW_LEN], const unsigned long *ul)
{
	snprintf(buf, OPT_SHOW_LEN, "%lu", *ul);
}
