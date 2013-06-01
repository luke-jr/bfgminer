/*
 * Copyright 2011 Rusty Russell
 *
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option) any
 * later version.  See LICENSE for more details.
 */

#include <ccan/compiler/compiler.h>
#include <ccan/tap/tap.h>

int main(int argc, char *argv[])
{
	plan_tests(2);

	ok1(!IS_COMPILE_CONSTANT(argc));
#if HAVE_BUILTIN_CONSTANT_P
	ok1(IS_COMPILE_CONSTANT(7));
#else
	pass("If !HAVE_BUILTIN_CONSTANT_P, IS_COMPILE_CONSTANT always false");
#endif
	return exit_status();
}
