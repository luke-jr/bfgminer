#include <ccan/tap/tap.h>
#include <stdarg.h>
#include <setjmp.h>
#include <stdlib.h>
#include <stdarg.h>
#include "utils.h"
#include <ccan/opt/opt.c>
#include <ccan/opt/usage.c>
#include <ccan/opt/helpers.c>
#include <ccan/opt/parse.c>

static char *my_cb(void *p)
{
	return NULL;
}

static void reset_options(void)
{
	free(opt_table);
	opt_table = NULL;
	opt_count = opt_num_short = opt_num_short_arg = opt_num_long = 0;
}

/* Test helpers. */
int main(int argc, char *argv[])
{
	char *output;
	char *longname = strdup("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
	char *shortname = strdup("shortname");

	plan_tests(48);
	opt_register_table(subtables, NULL);
	opt_register_noarg("--kkk|-k", my_cb, NULL, "magic kkk option");
	opt_register_noarg("-?", opt_usage_and_exit, "<MyArgs>...",
			   "This message");
	opt_register_arg("--longname", opt_set_charp, opt_show_charp,
			 &longname, "a really long option default");
	opt_register_arg("--shortname", opt_set_charp, opt_show_charp,
			 &shortname, "a short option default");
	output = opt_usage("my name", "ExTrA Args");
	diag("%s", output);
	ok1(strstr(output, "Usage: my name"));
	ok1(strstr(output, "--jjj|-j|--lll|-l <arg>"));
	ok1(strstr(output, "ExTrA Args"));
	ok1(strstr(output, "-a "));
	ok1(strstr(output, " Description of a\n"));
	ok1(strstr(output, "-b <arg>"));
	ok1(strstr(output, " Description of b (default: b)\n"));
	ok1(strstr(output, "--ddd "));
	ok1(strstr(output, " Description of ddd\n"));
	ok1(strstr(output, "--eee <filename> "));
	ok1(strstr(output, " (default: eee)\n"));
	ok1(strstr(output, "long table options:\n"));
	ok1(strstr(output, "--ggg|-g "));
	ok1(strstr(output, " Description of ggg\n"));
	ok1(strstr(output, "-h|--hhh <arg>"));
	ok1(strstr(output, " Description of hhh\n"));
	ok1(strstr(output, "--kkk|-k"));
	ok1(strstr(output, "magic kkk option"));
	/* This entry is hidden. */
	ok1(!strstr(output, "--mmm|-m"));
	free(output);

	/* NULL should use string from registered options. */
	output = opt_usage("my name", NULL);
	diag("%s", output);
	ok1(strstr(output, "Usage: my name"));
	ok1(strstr(output, "--jjj|-j|--lll|-l <arg>"));
	ok1(strstr(output, "<MyArgs>..."));
	ok1(strstr(output, "-a "));
	ok1(strstr(output, " Description of a\n"));
	ok1(strstr(output, "-b <arg>"));
	ok1(strstr(output, " Description of b (default: b)\n"));
	ok1(strstr(output, "--ddd "));
	ok1(strstr(output, " Description of ddd\n"));
	ok1(strstr(output, "--eee <filename> "));
	ok1(strstr(output, " (default: eee)\n"));
	ok1(strstr(output, "long table options:\n"));
	ok1(strstr(output, "--ggg|-g "));
	ok1(strstr(output, " Description of ggg\n"));
	ok1(strstr(output, "-h|--hhh <arg>"));
	ok1(strstr(output, " Description of hhh\n"));
	ok1(strstr(output, "--kkk|-k"));
	ok1(strstr(output, "magic kkk option"));
	ok1(strstr(output, "--longname"));
	ok1(strstr(output, "a really long option default"));
	ok1(strstr(output, "(default: \"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"...)"));
	ok1(strstr(output, "--shortname"));
	ok1(strstr(output, "a short option default"));
	ok1(strstr(output, "(default: \"shortname\")"));
	/* This entry is hidden. */
	ok1(!strstr(output, "--mmm|-m"));
	free(output);

	reset_options();
	/* Empty table test. */
	output = opt_usage("nothing", NULL);
	ok1(strstr(output, "Usage: nothing \n"));
	free(output);

	/* No short args. */
	opt_register_noarg("--aaa", test_noarg, NULL, "AAAAll");
	output = opt_usage("onearg", NULL);
	ok1(strstr(output, "Usage: onearg \n"));
	ok1(strstr(output, "--aaa"));
	ok1(strstr(output, "AAAAll"));
	free(output);

	free(shortname);
	free(longname);
	return exit_status();
}
