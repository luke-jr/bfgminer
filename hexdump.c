/*
 * hexdump implementation without depenecies to *printf()
 * output is equal to 'hexdump -C'
 * should be compatible to 64bit architectures
 *
 * Copyright (c) 2009 Daniel Mack <daniel@caiaq.de>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#define hex_print(p) applog(LOG_DEBUG, "%s", p)

static char nibble[] = {
	'0', '1', '2', '3', '4', '5', '6', '7',
	'8', '9', 'a', 'b', 'c', 'd', 'e', 'f' };

#define BYTES_PER_LINE 0x10

void hexdump(const uint8_t *p, unsigned int len)
{
	unsigned int i, addr;
	unsigned int wordlen = sizeof(void*);
	unsigned char v, line[BYTES_PER_LINE * 5];

	for (addr = 0; addr < len; addr += BYTES_PER_LINE) {
		/* clear line */
		for (i = 0; i < sizeof(line); i++) {
			if (i == wordlen * 2 + 52 ||
			    i == wordlen * 2 + 69) {
			    	line[i] = '|';
				continue;
			}

			if (i == wordlen * 2 + 70) {
				line[i] = '\0';
				continue;
			}

			line[i] = ' ';
		}

		/* print address */
		for (i = 0; i < wordlen * 2; i++) {
			v = addr >> ((wordlen * 2 - i - 1) * 4);
			line[i] = nibble[v & 0xf];
		}

		/* dump content */
		for (i = 0; i < BYTES_PER_LINE; i++) {
			int pos = (wordlen * 2) + 3 + (i / 8);

			if (addr + i >= len)
				break;

			v = p[addr + i];
			line[pos + (i * 3) + 0] = nibble[v >> 4];
			line[pos + (i * 3) + 1] = nibble[v & 0xf];

			/* character printable? */
			line[(wordlen * 2) + 53 + i] =
				(v >= ' ' && v <= '~') ? v : '.';
		}

		hex_print(line);
	}
}
