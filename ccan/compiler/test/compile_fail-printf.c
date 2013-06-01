/*
 * Copyright 2011 Rusty Russell
 *
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option) any
 * later version.  See LICENSE for more details.
 */

#include <ccan/compiler/compiler.h>

static void PRINTF_FMT(2,3) my_printf(int x, const char *fmt, ...)
{
}

int main(int argc, char *argv[])
{
	unsigned int i = 0;

	my_printf(1, "Not a pointer "
#ifdef FAIL
		  "%p",
#if !HAVE_ATTRIBUTE_PRINTF
#error "Unfortunately we don't fail if !HAVE_ATTRIBUTE_PRINTF."
#endif
#else
		  "%i",
#endif
		  i);
	return 0;
}
