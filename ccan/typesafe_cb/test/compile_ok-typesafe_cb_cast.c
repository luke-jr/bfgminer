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

struct foo {
	int x;
};

struct bar {
	int x;
};

struct baz {
	int x;
};

struct any {
	int x;
};

static void take_any(struct any *any)
{
}

int main(int argc, char *argv[])
{
	/* Otherwise we get unused warnings for these. */
	struct foo *foo = NULL;
	struct bar *bar = NULL;
	struct baz *baz = NULL;

	take_any(typesafe_cb_cast3(struct any *,
				   struct foo *, struct bar *, struct baz *,
				   foo));
	take_any(typesafe_cb_cast3(struct any *, 
				   struct foo *, struct bar *, struct baz *,
				   bar));
	take_any(typesafe_cb_cast3(struct any *, 
				   struct foo *, struct bar *, struct baz *,
				   baz));
	return 0;
}
