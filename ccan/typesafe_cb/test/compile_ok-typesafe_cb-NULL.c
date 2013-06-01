/*
 * Copyright 2011 Rusty Russell
 *
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation; either version 2.1 of the License, or (at your option)
 * any later version.  See LICENSE for more details.
 */

#include <ccan/typesafe_cb/typesafe_cb.h>
#include <stdlib.h>

/* NULL args for callback function should be OK for normal and _def. */

static void _register_callback(void (*cb)(const void *arg), const void *arg)
{
}

#define register_callback(cb, arg)				\
	_register_callback(typesafe_cb(void, const void *, (cb), (arg)), (arg))

int main(int argc, char *argv[])
{
	register_callback(NULL, "hello world");
	return 0;
}
