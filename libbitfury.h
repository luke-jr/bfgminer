/**
 *   libbitfury.h - headers for Bitfury chip/board library
 *
 *   Copyright (c) 2013 bitfury
 *   Copyright (c) 2013 legkodymov
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License version 2 as
 *   published by the Free Software Foundation.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 *   General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, see http://www.gnu.org/licenses/.
**/
#ifndef __LIBBITFURY_H__
#define __LIBBITFURY_H__

extern int libbitfury_detectChips(void);
int libbitfury_sendHashData(unsigned char *midstate, unsigned m7,
						 unsigned ntime, unsigned nbits,
						 unsigned nnonce);

int libbitfury_readHashData(unsigned int *res);

#endif /* __LIBBITFURY_H__ */
