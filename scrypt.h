#ifndef SCRYPT_H
#define SCRYPT_H

#include "miner.h"

#ifdef USE_SCRYPT
extern bool scrypt_test(unsigned char *pdata, const unsigned char *ptarget,
			uint32_t nonce);
extern void scrypt_outputhash(struct work *work);

#else /* USE_SCRYPT */
static inline bool scrypt_test(__maybe_unused unsigned char *pdata,
			       __maybe_unused const unsigned char *ptarget,
			       __maybe_unused uint32_t nonce)
{
	return false;
}

static inline void scrypt_outputhash(__maybe_unused struct work *work)
{
}
#endif /* USE_SCRYPT */

#endif /* SCRYPT_H */
