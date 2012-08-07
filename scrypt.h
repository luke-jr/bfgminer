#ifndef SCRYPT_H
#define SCRYPT_H

#ifdef USE_SCRYPT
extern bool scrypt_test(unsigned char *pdata, const unsigned char *ptarget, uint32_t nonce);
#else /* USE_SCRYPT */
static inline bool scrypt_test(unsigned char *pdata, const unsigned char *ptarget, uint32_t nonce)
{
	return false;
}
#endif /* USE_SCRYPT */

#endif /* SCRYPT_H */
