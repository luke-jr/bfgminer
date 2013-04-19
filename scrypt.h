#ifndef SCRYPT_H
#define SCRYPT_H

#include "miner.h"

#ifdef USE_SCRYPT
extern int scrypt_test(unsigned char *pdata, const unsigned char *ptarget,
			uint32_t nonce);
extern void scrypt_regenhash(struct work *work);

#else /* USE_SCRYPT */
static inline int scrypt_test(__maybe_unused unsigned char *pdata,
			       __maybe_unused const unsigned char *ptarget,
			       __maybe_unused uint32_t nonce)
{
	return 0;
}

static inline void scrypt_regenhash(__maybe_unused struct work *work)
{
}
#endif /* USE_SCRYPT */

#endif /* SCRYPT_H */
