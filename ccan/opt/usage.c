#include <ccan/opt/opt.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include "private.h"

/* We only use this for pointer comparisons. */
const char opt_hidden[1];

static unsigned write_short_options(char *str)
{
	unsigned int i, num = 0;
	const char *p;

	for (p = first_sopt(&i); p; p = next_sopt(p, &i)) {
		if (opt_table[i].desc != opt_hidden)
			str[num++] = *p;
	}
	return num;
}

#define OPT_SPACE_PAD "                    "

/* FIXME: Get all purdy. */
char *opt_usage(const char *argv0, const char *extra)
{
	unsigned int i, num, len;
	char *ret, *p;

	if (!extra) {
		extra = "";
		for (i = 0; i < opt_count; i++) {
			if (opt_table[i].cb == (void *)opt_usage_and_exit
			    && opt_table[i].u.carg) {
				extra = opt_table[i].u.carg;
				break;
			}
		}
	}

	/* An overestimate of our length. */
	len = strlen("Usage: %s ") + strlen(argv0)
		+ strlen("[-%.*s]") + opt_num_short + 1
		+ strlen(" ") + strlen(extra)
		+ strlen("\n");

	for (i = 0; i < opt_count; i++) {
		if (opt_table[i].type == OPT_SUBTABLE) {
			len += strlen("\n") + strlen(opt_table[i].desc)
				+ strlen(":\n");
		} else if (opt_table[i].desc != opt_hidden) {
			len += strlen(opt_table[i].names) + strlen(" <arg>");
			len += strlen(OPT_SPACE_PAD)
				+ strlen(opt_table[i].desc) + 1;
			if (opt_table[i].show) {
				len += strlen("(default: %s)")
					+ OPT_SHOW_LEN + sizeof("...");
			}
			len += strlen("\n");
		}
	}

	p = ret = malloc(len);
	if (!ret)
		return NULL;

	p += sprintf(p, "Usage: %s", argv0);
	p += sprintf(p, " [-");
	num = write_short_options(p);
	if (num) {
		p += num;
		p += sprintf(p, "]");
	} else {
		/* Remove start of single-entry options */
		p -= 3;
	}
	if (extra)
		p += sprintf(p, " %s", extra);
	p += sprintf(p, "\n");

	for (i = 0; i < opt_count; i++) {
		if (opt_table[i].desc == opt_hidden)
			continue;
		if (opt_table[i].type == OPT_SUBTABLE) {
			p += sprintf(p, "%s:\n", opt_table[i].desc);
			continue;
		}
		len = sprintf(p, "%s", opt_table[i].names);
		if (opt_table[i].type == OPT_HASARG
		    && !strchr(opt_table[i].names, ' ')
		    && !strchr(opt_table[i].names, '='))
			len += sprintf(p + len, " <arg>");
		len += sprintf(p + len, "%.*s",
			       len < strlen(OPT_SPACE_PAD)
			       ? (unsigned)strlen(OPT_SPACE_PAD) - len : 1,
			       OPT_SPACE_PAD);

		len += sprintf(p + len, "%s", opt_table[i].desc);
		if (opt_table[i].show) {
			char buf[OPT_SHOW_LEN + sizeof("...")];
			strcpy(buf + OPT_SHOW_LEN, "...");
			opt_table[i].show(buf, opt_table[i].u.arg);
			len += sprintf(p + len, " (default: %s)", buf);
		}
		p += len;
		p += sprintf(p, "\n");
	}
	*p = '\0';
	return ret;
}
