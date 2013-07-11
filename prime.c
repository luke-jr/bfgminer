#include "config.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/time.h>

#include <gmp.h>

#include "compat.h"
#include "miner.h"

#define nMaxSieveSize 1000000u
#define nPrimeTableLimit nMaxSieveSize
#define nPrimorialTableLimit 100000u

#define PRIME_COUNT 78498
#define PRIMORIAL_COUNT 9592

static
unsigned vPrimes[PRIME_COUNT];
mpz_t bnTwoInverses[PRIME_COUNT];
mpz_t vPrimorials[PRIMORIAL_COUNT];

static
int64_t GetTimeMicros()
{
	struct timeval tv;
	cgtime(&tv);
	return ((int64_t)tv.tv_sec * 1000000) + tv.tv_usec;
}

static
int64_t GetTimeMillis()
{
	return GetTimeMicros() / 1000;
}

static
int64_t GetTime()
{
	return GetTimeMicros() / 1000000;
}

static
bool error(const char *fmt, ...)
{
	puts(fmt);  // FIXME
	return false;
}

mpz_t bnTwo;

void GeneratePrimeTable()
{
	mpz_init_set_ui(bnTwo, 2);
	
	
	mpz_t bnOne;
	mpz_init_set_ui(bnOne, 1);
	
	mpz_t *bnLastPrimorial = &bnOne;
	unsigned i = 0;
	// Generate prime table using sieve of Eratosthenes
	bool vfComposite[nPrimeTableLimit] = {false};
	for (unsigned int nFactor = 2; nFactor * nFactor < nPrimeTableLimit; nFactor++)
	{
	    if (vfComposite[nFactor])
	        continue;
	    for (unsigned int nComposite = nFactor * nFactor; nComposite < nPrimeTableLimit; nComposite += nFactor)
	        vfComposite[nComposite] = true;
	}
	for (unsigned int n = 2; n < nPrimeTableLimit; n++)
		if (!vfComposite[n])
		{
			vPrimes[i] = n;
			
			if (n > 2)
			{
				// bnOne isn't 1 here, which is okay since it is no longer needed as 1 after prime 2
				mpz_init(bnTwoInverses[i]);
				mpz_set_ui(bnOne, n);
				if (!mpz_invert(bnTwoInverses[i], bnTwo, bnOne))
					quit(1, "mpz_invert of 2 failed for prime %u", n);
			}
			
			if (n < nPrimorialTableLimit)
			{
				mpz_init(vPrimorials[i]);
				mpz_mul_ui(vPrimorials[i], *bnLastPrimorial, n);
				bnLastPrimorial = &vPrimorials[i];
			}
			
			++i;
		}
	mpz_clear(bnOne);
	applog(LOG_DEBUG, "GeneratePrimeTable() : prime table [1, %d] generated with %lu primes", nPrimeTableLimit, (unsigned long)i);
}

#define nFractionalBits 24
#define TARGET_FRACTIONAL_MASK ((1u << nFractionalBits) - 1)
#define TARGET_LENGTH_MASK (~TARGET_FRACTIONAL_MASK)

// Check Fermat probable primality test (2-PRP): 2 ** (n-1) = 1 (mod n)
// true: n is probable prime
// false: n is composite; set fractional length in the nLength output
static
bool FermatProbablePrimalityTest(mpz_t *n, unsigned int *pnLength)
{
	mpz_t a, e, r;
	mpz_init_set_ui(a, 2); // base; Fermat witness
	mpz_init(e);
	mpz_sub_ui(e, *n, 1);
	mpz_init(r);
	
	mpz_powm(r, a, e, *n);
	mpz_clear(a);
	mpz_clear(e);
	if (!mpz_cmp_ui(r, 1))
	{
		mpz_clear(r);
		return true;
	}
	
	// Failed Fermat test, calculate fractional length
	// nFractionalLength = ( (n-r) << nFractionalBits ) / n
	mpz_sub(r, *n, r);
	mpz_mul_2exp(r, r, nFractionalBits);
	mpz_fdiv_q(r, r, *n);
	unsigned int nFractionalLength = mpz_get_ui(r);
	mpz_clear(r);
	
	if (nFractionalLength >= (1 << nFractionalBits))
		return error("FermatProbablePrimalityTest() : fractional assert");
	*pnLength = (*pnLength & TARGET_LENGTH_MASK) | nFractionalLength;
	return false;
}

static
unsigned int TargetGetLength(unsigned int nBits)
{
	return ((nBits & TARGET_LENGTH_MASK) >> nFractionalBits);
}

static
void TargetIncrementLength(unsigned int *pnBits)
{
    *pnBits += (1 << nFractionalBits);
}

// Test probable primality of n = 2p +/- 1 based on Euler, Lagrange and Lifchitz
// fSophieGermain:
//   true:  n = 2p+1, p prime, aka Cunningham Chain of first kind
//   false: n = 2p-1, p prime, aka Cunningham Chain of second kind
// Return values
//   true: n is probable prime
//   false: n is composite; set fractional length in the nLength output
static
bool EulerLagrangeLifchitzPrimalityTest(mpz_t *n, bool fSophieGermain, unsigned int *pnLength)
{
	mpz_t a, e, r;
	mpz_init_set_ui(a, 2);
	mpz_init(e);
	mpz_sub_ui(e, *n, 1);
	mpz_fdiv_q_2exp(e, e, 1);
	mpz_init(r);
	
	mpz_powm(r, a, e, *n);
	mpz_clear(a);
	mpz_clear(e);
	unsigned nMod8 = mpz_fdiv_ui(*n, 8);
	bool fPassedTest = false;
	if (fSophieGermain && (nMod8 == 7)) // Euler & Lagrange
		fPassedTest = !mpz_cmp_ui(r, 1);
	else if (nMod8 == (fSophieGermain ? 3 : 5)) // Lifchitz
	{
		mpz_t mp;
		mpz_init_set_ui(mp, 1);
		mpz_add(mp, r, mp);
		fPassedTest = !mpz_cmp(mp, *n);
		mpz_clear(mp);
	}
	else if ((!fSophieGermain) && (nMod8 == 1)) // LifChitz
		fPassedTest = !mpz_cmp_ui(r, 1);
	else
	{
		mpz_clear(r);
		return error("EulerLagrangeLifchitzPrimalityTest() : invalid n %% 8 = %d, %s", nMod8, (fSophieGermain? "first kind" : "second kind"));
	}

	if (fPassedTest)
	{
		mpz_clear(r);
		return true;
	}
	// Failed test, calculate fractional length
	
	// derive Fermat test remainder
	mpz_mul(r, r, r);
	mpz_fdiv_r(r, r, *n);
	
	// nFractionalLength = ( (n-r) << nFractionalBits ) / n
	mpz_sub(r, *n, r);
	mpz_mul_2exp(r, r, nFractionalBits);
	mpz_fdiv_q(r, r, *n);
	unsigned int nFractionalLength = mpz_get_ui(r);
	mpz_clear(r);
	
	if (nFractionalLength >= (1 << nFractionalBits))
		return error("EulerLagrangeLifchitzPrimalityTest() : fractional assert");
	*pnLength = (*pnLength & TARGET_LENGTH_MASK) | nFractionalLength;
	return false;
}

// Test Probable Cunningham Chain for: n
// fSophieGermain:
//   true - Test for Cunningham Chain of first kind (n, 2n+1, 4n+3, ...)
//   false - Test for Cunningham Chain of second kind (n, 2n-1, 4n-3, ...)
// Return value:
//   true - Probable Cunningham Chain found (length at least 2)
//   false - Not Cunningham Chain
static
bool ProbableCunninghamChainTest(mpz_t *n, bool fSophieGermain, bool fFermatTest, unsigned int *pnProbableChainLength)
{
#ifdef SUPERDEBUG
	printf("ProbableCunninghamChainTest(");
	mpz_out_str(stdout, 0x10, *n);
	printf(", %d, %d, %u)\n", (int)fSophieGermain, (int)fFermatTest, *pnProbableChainLength);
#endif
	
	*pnProbableChainLength = 0;
	mpz_t N;
	mpz_init_set(N, *n);
	
	// Fermat test for n first
	if (!FermatProbablePrimalityTest(&N, pnProbableChainLength))
	{
		mpz_clear(N);
		return false;
	}
#ifdef SUPERDEBUG
	printf("N=");
	mpz_out_str(stdout, 0x10, N);
	printf("\n");
#endif

	// Euler-Lagrange-Lifchitz test for the following numbers in chain
	while (true)
	{
		TargetIncrementLength(pnProbableChainLength);
		mpz_add(N, N, N);
		if (fSophieGermain)
			mpz_add_ui(N, N, 1);
		else
			mpz_sub_ui(N, N, 1);
		if (fFermatTest)
		{
			if (!FermatProbablePrimalityTest(&N, pnProbableChainLength))
				break;
		}
		else
		{
#ifdef SUPERDEBUG
			if (!fSophieGermain)
			{
				printf("EulerLagrangeLifchitzPrimalityTest(");
				mpz_out_str(stdout, 0x10, N);
				printf(", 1, %d)\n", *pnProbableChainLength);
			}
#endif
			if (!EulerLagrangeLifchitzPrimalityTest(&N, fSophieGermain, pnProbableChainLength))
				break;
		}
	}
	mpz_clear(N);

#ifdef SUPERDEBUG
	printf("PCCT => %u (%u)\n", TargetGetLength(*pnProbableChainLength), *pnProbableChainLength);
#endif
	return (TargetGetLength(*pnProbableChainLength) >= 2);
}

static
unsigned int TargetFromInt(unsigned int nLength)
{
    return (nLength << nFractionalBits);
}

// Test probable prime chain for: nOrigin
// Return value:
//   true - Probable prime chain found (one of nChainLength meeting target)
//   false - prime chain too short (none of nChainLength meeting target)
static
bool ProbablePrimeChainTest(mpz_t *bnPrimeChainOrigin, unsigned int nBits, bool fFermatTest, unsigned int *pnChainLengthCunningham1, unsigned int *pnChainLengthCunningham2, unsigned int *pnChainLengthBiTwin)
{
	*pnChainLengthCunningham1 = 0;
	*pnChainLengthCunningham2 = 0;
	*pnChainLengthBiTwin = 0;
	
	mpz_t mp;
	mpz_init(mp);
	
	// Test for Cunningham Chain of first kind
	mpz_sub_ui(mp, *bnPrimeChainOrigin, 1);
	ProbableCunninghamChainTest(&mp, true, fFermatTest, pnChainLengthCunningham1);
	// Test for Cunningham Chain of second kind
	mpz_add_ui(mp, *bnPrimeChainOrigin, 1);
	ProbableCunninghamChainTest(&mp, false, fFermatTest, pnChainLengthCunningham2);
	mpz_clear(mp);
	// Figure out BiTwin Chain length
	// BiTwin Chain allows a single prime at the end for odd length chain
	*pnChainLengthBiTwin = (TargetGetLength(*pnChainLengthCunningham1) > TargetGetLength(*pnChainLengthCunningham2)) ? (*pnChainLengthCunningham2 + TargetFromInt(TargetGetLength(*pnChainLengthCunningham2)+1)) : (*pnChainLengthCunningham1 + TargetFromInt(TargetGetLength(*pnChainLengthCunningham1)));
	
	return (*pnChainLengthCunningham1 >= nBits || *pnChainLengthCunningham2 >= nBits || *pnChainLengthBiTwin >= nBits);
}

struct SieveOfEratosthenes {
	bool valid;
	
	unsigned int nSieveSize; // size of the sieve
	unsigned int nBits; // target of the prime chain to search for
	mpz_t hashBlockHeader; // block header hash
	mpz_t bnFixedFactor; // fixed factor to derive the chain

	// bitmaps of the sieve, index represents the variable part of multiplier
	bool vfCompositeCunningham1[1000000];
	bool vfCompositeCunningham2[1000000];
	bool vfCompositeBiTwin[1000000];

	unsigned int nPrimeSeq; // prime sequence number currently being processed
	unsigned int nCandidateMultiplier; // current candidate for power test
};

static
void psieve_reset(struct SieveOfEratosthenes *psieve)
{
	mpz_clear(psieve->hashBlockHeader);
	mpz_clear(psieve->bnFixedFactor);
	psieve->valid = false;
}

static
void psieve_init(struct SieveOfEratosthenes *psieve, unsigned nSieveSize, unsigned nBits, mpz_t *hashBlockHeader, mpz_t *bnFixedMultiplier)
{
	assert(!psieve->valid);
	*psieve = (struct SieveOfEratosthenes){
		.valid = true,
		.nSieveSize = nSieveSize,
		.nBits = nBits,
	};
	
	mpz_init_set(psieve->hashBlockHeader, *hashBlockHeader);
	mpz_init(psieve->bnFixedFactor);
	mpz_mul(psieve->bnFixedFactor, *bnFixedMultiplier, *hashBlockHeader);
}

// Weave sieve for the next prime in table
// Return values:
//   True  - weaved another prime; nComposite - number of composites removed
//   False - sieve already completed
static
bool psieve_Weave(struct SieveOfEratosthenes *psieve)
{
	unsigned nPrime = vPrimes[psieve->nPrimeSeq];
	if (psieve->nPrimeSeq >= PRIME_COUNT || nPrime >= psieve->nSieveSize)
		return false;  // sieve has been completed
	if (mpz_fdiv_ui(psieve->bnFixedFactor, nPrime) == 0)
	{
		// Nothing in the sieve is divisible by this prime
		++psieve->nPrimeSeq;
		return true;
	}
	// Find the modulo inverse of fixed factor
	mpz_t bnFixedInverse, p;
	mpz_init(bnFixedInverse);
	mpz_init_set_ui(p, nPrime);
	if (!mpz_invert(bnFixedInverse, psieve->bnFixedFactor, p))
	{
		mpz_clear(p);
		mpz_clear(bnFixedInverse);
		return error("CSieveOfEratosthenes::Weave(): BN_mod_inverse of fixed factor failed for prime #%u=%u", psieve->nPrimeSeq, nPrime);
	}
	mpz_t *pbnTwoInverse = &bnTwoInverses[psieve->nPrimeSeq];

	// Weave the sieve for the prime
	unsigned int nChainLength = TargetGetLength(psieve->nBits);
	for (unsigned int nBiTwinSeq = 0; nBiTwinSeq < 2 * nChainLength; nBiTwinSeq++)
	{
		// Find the first number that's divisible by this prime
		int nDelta = ((nBiTwinSeq % 2 == 0) ? (-1) : 1);
		mpz_mul_ui(p, bnFixedInverse, nPrime - nDelta);
		unsigned int nSolvedMultiplier = mpz_fdiv_ui(p, nPrime);
		
		if (nBiTwinSeq % 2 == 1)
			mpz_mul(bnFixedInverse, bnFixedInverse, *pbnTwoInverse); // for next number in chain

		if (nBiTwinSeq < nChainLength)
			for (unsigned int nVariableMultiplier = nSolvedMultiplier; nVariableMultiplier < psieve->nSieveSize; nVariableMultiplier += nPrime)
				psieve->vfCompositeBiTwin[nVariableMultiplier] = true;
		if (((nBiTwinSeq & 1u) == 0))
			for (unsigned int nVariableMultiplier = nSolvedMultiplier; nVariableMultiplier < psieve->nSieveSize; nVariableMultiplier += nPrime)
				psieve->vfCompositeCunningham1[nVariableMultiplier] = true;
		if (((nBiTwinSeq & 1u) == 1u))
			for (unsigned int nVariableMultiplier = nSolvedMultiplier; nVariableMultiplier < psieve->nSieveSize; nVariableMultiplier += nPrime)
				psieve->vfCompositeCunningham2[nVariableMultiplier] = true;
	}
	mpz_clear(p);
	mpz_clear(bnFixedInverse);
	++psieve->nPrimeSeq;
	return true;
}

static
bool psieve_GetNextCandidateMultiplier(struct SieveOfEratosthenes *psieve, unsigned int *pnVariableMultiplier)
{
	while (true)
	{
		psieve->nCandidateMultiplier++;
		if (psieve->nCandidateMultiplier >= psieve->nSieveSize)
		{
			psieve->nCandidateMultiplier = 0;
			return false;
		}
		if (!psieve->vfCompositeCunningham1[psieve->nCandidateMultiplier] ||
			!psieve->vfCompositeCunningham2[psieve->nCandidateMultiplier] ||
			!psieve->vfCompositeBiTwin[psieve->nCandidateMultiplier])
			{
				*pnVariableMultiplier = psieve->nCandidateMultiplier;
				return true;
			}
	}
}

// Get total number of candidates for power test
static
unsigned int psieve_GetCandidateCount(struct SieveOfEratosthenes *psieve)
{
	unsigned int nCandidates = 0;
	for (unsigned int nMultiplier = 0; nMultiplier < psieve->nSieveSize; nMultiplier++)
	{
		if (!psieve->vfCompositeCunningham1[nMultiplier] || !psieve->vfCompositeCunningham2[nMultiplier] || !psieve->vfCompositeBiTwin[nMultiplier])
		nCandidates++;
	}
	return nCandidates;
}

// Mine probable prime chain of form: n = h * p# +/- 1
bool MineProbablePrimeChain(struct SieveOfEratosthenes *psieve, const uint8_t *header, mpz_t *hash, mpz_t *bnFixedMultiplier, bool *pfNewBlock, unsigned *pnTriedMultiplier, unsigned *pnProbableChainLength, unsigned *pnTests, unsigned *pnPrimesHit, struct work *work)
{
	const uint32_t *pnbits = (void*)&header[72];
	*pnProbableChainLength = 0;
	*pnTests = 0;
	*pnPrimesHit = 0;

	if (*pfNewBlock && psieve->valid)
	{
	    // Must rebuild the sieve
	    psieve_reset(psieve);
	}
	*pfNewBlock = false;

	int64_t nStart, nCurrent; // microsecond timer
	if (!psieve->valid)
	{
		// Build sieve
		nStart = GetTimeMicros();
#ifdef SUPERDEBUG
		fprintf(stderr, "psieve_init(?, %u, %08x, ", nMaxSieveSize, *pnbits);
		mpz_out_str(stderr, 0x10, *hash);
		fprintf(stderr, ", ");
		mpz_out_str(stderr, 0x10, *bnFixedMultiplier);
		fprintf(stderr, ")\n");
#endif
		psieve_init(psieve, nMaxSieveSize, *pnbits, hash, bnFixedMultiplier);
		while (psieve_Weave(psieve));
 		applog(LOG_DEBUG, "MineProbablePrimeChain() : new sieve (%u/%u) ready in %uus", psieve_GetCandidateCount(psieve), nMaxSieveSize, (unsigned int) (GetTimeMicros() - nStart));
	}

	mpz_t bnChainOrigin;
	mpz_init(bnChainOrigin);

	nStart = GetTimeMicros();
	nCurrent = nStart;

	while (nCurrent - nStart < 10000 && nCurrent >= nStart)
	{
		++*pnTests;
		if (!psieve_GetNextCandidateMultiplier(psieve, pnTriedMultiplier))
		{
			// power tests completed for the sieve
			psieve_reset(psieve);
			*pfNewBlock = true; // notify caller to change nonce
			mpz_clear(bnChainOrigin);
			return false;
		}
#ifdef SUPERDEBUG
		printf("nTriedMultiplier=%d\n", *pnTriedMultiplier=640150);
#endif
		mpz_mul(bnChainOrigin, *hash, *bnFixedMultiplier);
		mpz_mul_ui(bnChainOrigin, bnChainOrigin, *pnTriedMultiplier);
		unsigned int nChainLengthCunningham1 = 0;
		unsigned int nChainLengthCunningham2 = 0;
		unsigned int nChainLengthBiTwin = 0;
#ifdef SUPERDEBUG
		printf("ProbablePrimeChainTest(bnChainOrigin=");
		mpz_out_str(stdout, 0x10, bnChainOrigin);
		printf(", nbits=%08lx, false, %d, %d, %d)\n", (unsigned long)*pnbits, nChainLengthCunningham1, nChainLengthCunningham2, nChainLengthBiTwin);
#endif
		if (ProbablePrimeChainTest(&bnChainOrigin, *pnbits, false, &nChainLengthCunningham1, &nChainLengthCunningham2, &nChainLengthBiTwin))
		{
			// bnChainOrigin is not used again, so recycled here for the result

			// block.bnPrimeChainMultiplier = *bnFixedMultiplier * *pnTriedMultiplier;
			mpz_mul_ui(bnChainOrigin, *bnFixedMultiplier, *pnTriedMultiplier);
			
			size_t exportsz, resultoff;
			uint8_t *export = mpz_export(NULL, &exportsz, -1, 1, -1, 0, bnChainOrigin);
			assert(exportsz < 250);  // FIXME: bitcoin varint
			resultoff = 1;
			if (export[0] & 0x80)
				++resultoff;
			uint8_t *result = malloc(exportsz + resultoff);
			result[0] = exportsz + resultoff - 1;
			result[1] = '\0';
			memcpy(&result[resultoff], export, exportsz);
			if (mpz_sgn(bnChainOrigin) < 0)
				result[1] |= 0x80;
			free(export);
			
			work->sig = result;
			work->sigsz = exportsz + resultoff;
			
			char hex[1 + (work->sigsz * 2)];
			bin2hex(hex, work->sig, work->sigsz);
			applog(LOG_DEBUG, "SIGNATURE: %s\n", hex);
			
			
// 		    printf("Probable prime chain found for block=%s!!\n  Target: %s\n  Length: (%s %s %s)\n", block.GetHash().GetHex().c_str(),
// 		    TargetToString(nbits).c_str(), TargetToString(nChainLengthCunningham1).c_str(), TargetToString(nChainLengthCunningham2).c_str(), TargetToString(nChainLengthBiTwin).c_str());
			applog(LOG_DEBUG, "Probable prime chain found for block");
			*pnProbableChainLength = nChainLengthCunningham1;
			if (*pnProbableChainLength < nChainLengthCunningham2)
				*pnProbableChainLength = nChainLengthCunningham2;
			if (*pnProbableChainLength < nChainLengthBiTwin)
				*pnProbableChainLength = nChainLengthBiTwin;
			mpz_clear(bnChainOrigin);
		    return true;
		}
		*pnProbableChainLength = nChainLengthCunningham1;
		if (*pnProbableChainLength < nChainLengthCunningham2)
			*pnProbableChainLength = nChainLengthCunningham2;
		if (*pnProbableChainLength < nChainLengthBiTwin)
			*pnProbableChainLength = nChainLengthBiTwin;
		if(TargetGetLength(*pnProbableChainLength) >= 1)
		    ++*pnPrimesHit;

		nCurrent = GetTimeMicros();
	}
	mpz_clear(bnChainOrigin);
	return false; // stop as timed out
}

// Checks that the high bit is set, and low bit is clear (ie, divisible by 2)
static
bool check_ends(const uint8_t *hash)
{
	return (hash[31] & 0x80) && !(hash[0] & 1);
}

static inline
void set_mpz_to_hash(mpz_t *hash, const uint8_t *hashb)
{
	mpz_import(*hash, 8, -1, 4, -1, 0, hashb);
}

struct prime_longterms {
	unsigned int nPrimorialHashFactor;
	int64_t nTimeExpected;   // time expected to prime chain (micro-second)
	int64_t nTimeExpectedPrev; // time expected to prime chain last time
	bool fIncrementPrimorial; // increase or decrease primorial factor
	unsigned current_prime;
	int64_t nHPSTimerStart;
	int64_t nLogTime;
	int64_t nPrimeCounter;
	int64_t nTestCounter;
};

static
struct prime_longterms *get_prime_longterms()
{
	struct bfgtls_data *bfgtls = get_bfgtls();
	
	struct prime_longterms *pl = bfgtls->prime_longterms;
	if (unlikely(!pl))
	{
		pl = bfgtls->prime_longterms = malloc(sizeof(*pl));
		*pl = (struct prime_longterms){
			.nPrimorialHashFactor = 7,
			.fIncrementPrimorial = true,
			.current_prime = 3,  // index 3 is prime number 7
			.nHPSTimerStart = GetTimeMillis(),
		};
	}
	return pl;
}

bool prime(uint8_t *header, struct work *work)
{
	struct prime_longterms *pl = get_prime_longterms();
	bool rv = false;
	
	uint32_t *nonce = (void*)(&header[76]);
	unsigned char hashb[32];
	mpz_t hash, bnPrimeMin;
	
	mpz_init(hash);
	mpz_init_set_ui(bnPrimeMin, 1);
	mpz_mul_2exp(bnPrimeMin, bnPrimeMin, 255);
	
	bool fNewBlock = true;
	unsigned int nTriedMultiplier = 0;
	struct SieveOfEratosthenes sieve = {
		.valid = false,
	};
	
	const unsigned nHashFactor = 210;
	// a valid header must hash to have the MSB set, and a multiple of nHashFactor
	while (true)
	{
		gen_hash(header, hashb, 80);
		if (check_ends(hashb))
		{
			set_mpz_to_hash(&hash, hashb);
			if (!mpz_fdiv_ui(hash, 105))
				break;
		}
		if (unlikely(*nonce == 0xffffffff))
		{
			mpz_clear(hash);
			mpz_clear(bnPrimeMin);
			return false;
		}
		++*nonce;
	}
	{
		char hex[9];
		bin2hex(hex, nonce, 4);
		applog(LOG_DEBUG, "Pass 1 found: %s", hex);
	}
	
	// primorial fixed multiplier
	mpz_t bnPrimorial;
	mpz_init(bnPrimorial);
	unsigned int nRoundTests = 0;
	unsigned int nRoundPrimesHit = 0;
	int64_t nPrimeTimerStart = GetTimeMicros();
	if (pl->nTimeExpected > pl->nTimeExpectedPrev)
	    pl->fIncrementPrimorial = !pl->fIncrementPrimorial;
	pl->nTimeExpectedPrev = pl->nTimeExpected;
	// dynamic adjustment of primorial multiplier
	if (pl->fIncrementPrimorial)
	{
		++pl->current_prime;
		if (pl->current_prime >= PRIMORIAL_COUNT)
			quit(1, "primorial increment overflow");
	}
	else if (vPrimes[pl->current_prime] > pl->nPrimorialHashFactor)
	{
		if (!pl->current_prime)
			quit(1, "primorial decrement overflow");
		--pl->current_prime;
	}
	mpz_set(bnPrimorial, vPrimorials[pl->current_prime]);
	
	
	while (true)
	{
		unsigned int nTests = 0;
		unsigned int nPrimesHit = 0;
		
		mpz_t bnMultiplierMin;
		// bnMultiplierMin = bnPrimeMin * nHashFactor / hash + 1
		mpz_init(bnMultiplierMin);
		mpz_mul_ui(bnMultiplierMin, bnPrimeMin, nHashFactor);
		mpz_fdiv_q(bnMultiplierMin, bnMultiplierMin, hash);
		mpz_add_ui(bnMultiplierMin, bnMultiplierMin, 1);
		
		while (mpz_cmp(bnPrimorial, bnMultiplierMin) < 0)
		{
			++pl->current_prime;
			if (pl->current_prime >= PRIMORIAL_COUNT)
				quit(1, "primorial minimum overflow");
			mpz_set(bnPrimorial, vPrimorials[pl->current_prime]);
		}
		mpz_clear(bnMultiplierMin);
		
		mpz_t bnFixedMultiplier;
		mpz_init(bnFixedMultiplier);
	    // bnFixedMultiplier = (bnPrimorial > nHashFactor) ? (bnPrimorial / nHashFactor) : 1
		if (mpz_cmp_ui(bnPrimorial, nHashFactor) > 0)
		{
			mpz_t bnHashFactor;
			mpz_init_set_ui(bnHashFactor, nHashFactor);
			mpz_fdiv_q(bnFixedMultiplier, bnPrimorial, bnHashFactor);
			mpz_clear(bnHashFactor);
		}
		else
			mpz_set_ui(bnFixedMultiplier, 1);
#ifdef SUPERDEBUG
		fprintf(stderr,"bnFixedMultiplier=");
		mpz_out_str(stderr, 0x10, bnFixedMultiplier);
		fprintf(stderr, " nPrimorialMultiplier=%u nTriedMultiplier=%u\n", vPrimes[pl->current_prime], nTriedMultiplier);
#endif
		
		
		// mine for prime chain
		unsigned int nProbableChainLength;
		if (MineProbablePrimeChain(&sieve, header, &hash, &bnFixedMultiplier, &fNewBlock, &nTriedMultiplier, &nProbableChainLength, &nTests, &nPrimesHit, work))
		{
// TODO			CheckWork(pblock, *pwalletMain, reservekey);
			mpz_clear(bnFixedMultiplier);
			rv = true;
			break;
		}
		mpz_clear(bnFixedMultiplier);
		nRoundTests += nTests;
		nRoundPrimesHit += nPrimesHit;

	    // Meter primes/sec
	    if (pl->nHPSTimerStart == 0)
	    {
	        pl->nHPSTimerStart = GetTimeMillis();
	        pl->nPrimeCounter = 0;
	        pl->nTestCounter = 0;
	    }
	    else
	    {
	        pl->nPrimeCounter += nPrimesHit;
	        pl->nTestCounter += nTests;
	    }
		
		
		if (GetTimeMillis() - pl->nHPSTimerStart > 60000)
		{
			double dPrimesPerMinute = 60000.0 * pl->nPrimeCounter / (GetTimeMillis() - pl->nHPSTimerStart);
			double dPrimesPerSec = dPrimesPerMinute / 60.0;
			double dTestsPerMinute = 60000.0 * pl->nTestCounter / (GetTimeMillis() - pl->nHPSTimerStart);
			pl->nHPSTimerStart = GetTimeMillis();
			pl->nPrimeCounter = 0;
			pl->nTestCounter = 0;
			if (GetTime() - pl->nLogTime > 60)
			{
				pl->nLogTime = GetTime();
				applog(LOG_NOTICE, "primemeter %9.0f prime/h %9.0f test/h %5dpps", dPrimesPerMinute * 60.0, dTestsPerMinute * 60.0, (int)dPrimesPerSec);
			}
		}
		
		
	    // Check for stop or if block needs to be rebuilt
	    // TODO
// 	    boost::this_thread::interruption_point();
// 	    if (vNodes.empty())
// 	        break;
	    if (fNewBlock /*|| pblock->nNonce >= 0xffff0000*/)
	        break;
// 	    if (nTransactionsUpdated != nTransactionsUpdatedLast && GetTime() - nStart > 60)
// 	        break;
// 	    if (pindexPrev != pindexBest)
// 	        break;
	}
	mpz_clear(bnPrimorial);

	// Primecoin: estimate time to block
	pl->nTimeExpected = (GetTimeMicros() - nPrimeTimerStart) / max(1u, nRoundTests);
	pl->nTimeExpected = pl->nTimeExpected * max(1u, nRoundTests) / max(1u, nRoundPrimesHit);
//TODO
// 	for (unsigned int n = 1; n < TargetGetLength(pblock->nBits); n++)
// 	     nTimeExpected = nTimeExpected * max(1u, nRoundTests) * 3 / max(1u, nRoundPrimesHit);
	applog(LOG_DEBUG, "PrimecoinMiner() : Round primorial=%u tests=%u primes=%u expected=%us", vPrimes[pl->current_prime], nRoundTests, nRoundPrimesHit, (unsigned int)(pl->nTimeExpected/1000000));

	mpz_clear(hash);
	mpz_clear(bnPrimeMin);
	
	return rv;
}

#if 0
void pmain()
{
	setbuf(stderr, NULL);
	setbuf(stdout, NULL);
	GeneratePrimeTable();
	unsigned char array[80] = {
		0x02,0x00,0x00,0x00,
		0x59,0xf7,0x56,0x1c,0x21,0x25,0xc1,0xad,0x0d,0xee,0xbd,0x05,0xb8,0x41,0x38,0xab,
		0x2e,0xfb,0x65,0x40,0xc8,0xc7,0xa3,0xef,0x90,0x3d,0x75,0x8c,0x03,0x1c,0x7a,0xcc,
		0x8d,0x27,0x4d,0xeb,0x7b,0x6a,0xf8,0xe0,0x44,0x2d,0x7c,0xf6,0xb9,0x71,0x12,0xd8,
		0x61,0x60,0x5b,0x1f,0xa5,0xa3,0xf7,0x4f,0x61,0xe3,0x59,0x67,0x03,0xc2,0xfb,0x56,
		0xed,0x78,0xdb,0x51,
		0xd5,0xbe,0x38,0x07,
		0xe8,0x02,0x00,0x00,
	};
	prime(array);
}
#endif

bool scanhash_prime(struct thr_info *thr, const unsigned char *pmidstate, unsigned char *pdata, unsigned char *phash1, unsigned char *phash, const unsigned char *ptarget, uint32_t max_nonce, uint32_t *last_nonce, uint32_t nonce)
{
	struct work *work = (struct work *)(&pmidstate[-offsetof(struct work, midstate)]);
	
	unsigned char header[80];
	swap32yes(header, pdata, 80 / 4);
#if 0
	memcpy(header,(unsigned char[80]){
		0x02,0x00,0x00,0x00,
		0x59,0xf7,0x56,0x1c,0x21,0x25,0xc1,0xad,0x0d,0xee,0xbd,0x05,0xb8,0x41,0x38,0xab,
		0x2e,0xfb,0x65,0x40,0xc8,0xc7,0xa3,0xef,0x90,0x3d,0x75,0x8c,0x03,0x1c,0x7a,0xcc,
		0x8d,0x27,0x4d,0xeb,0x7b,0x6a,0xf8,0xe0,0x44,0x2d,0x7c,0xf6,0xb9,0x71,0x12,0xd8,
		0x61,0x60,0x5b,0x1f,0xa5,0xa3,0xf7,0x4f,0x61,0xe3,0x59,0x67,0x03,0xc2,0xfb,0x56,
		0xed,0x78,0xdb,0x51,
		0xd5,0xbe,0x38,0x07,
		0xe8,0x02,0x00,0x00,
	},80);
#endif
	bool rv = prime(header, work);
	swap32yes(pdata, header, 80 / 4);
	return rv;
}

