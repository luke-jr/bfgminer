#ifndef _RIPEMD160_H
#define _RIPEMD160_H

#include <stdint.h>
#include <string.h>

#define RIPEMD160_BLOCKSIZE	64

typedef struct {
	uint8_t buf[RIPEMD160_BLOCKSIZE];
	size_t pos;
	uint32_t h[5];
} ripemd160_ctx;

void inline ripemd160_init(ripemd160_ctx *ctx)
{
	ctx->pos = 0;
	ctx->h[0] = 0x67452301;
	ctx->h[1] = 0xEFCDAB89;
	ctx->h[2] = 0x98BADCFE;
	ctx->h[3] = 0x10325476;
	ctx->h[4] = 0xC3D2E1F0;
}

void ripemd160_update(ripemd160_ctx *ctx, const uint8_t *data, size_t size);
void ripemd160_final(ripemd160_ctx *ctx, uint8_t *digest);
void ripemd160(const uint8_t *data, size_t size, uint8_t *digest);

#endif
