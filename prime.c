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
#define nPrimeTableLimit 100000u //nMaxSieveSize

#define PRIME_COUNT 9592 //78498

static
unsigned vPrimes[PRIME_COUNT];
mpz_t vPrimorials[PRIME_COUNT];

static
int64_t GetTimeMicros()
{
	struct timeval tv;
	cgtime(&tv);
	return (tv.tv_sec * 1000000) + tv.tv_usec;
}

static
int64_t GetTimeMillis()
{
	return GetTimeMicros() / 1000;
}

static
bool error(const char *fmt, ...)
{
	puts(fmt);  // FIXME
	return false;
}

static
void GeneratePrimeTable()
{
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
			mpz_init(vPrimorials[i]);
			mpz_mul_ui(vPrimorials[i], *bnLastPrimorial, n);
			bnLastPrimorial = &vPrimorials[i];
			++i;
		}
	printf("GeneratePrimeTable() : prime table [1, %d] generated with %lu primes\n", nPrimeTableLimit, (unsigned long)i);
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
	if (!mpz_cmp_ui(r, 1))
		return true;
	
	// Failed Fermat test, calculate fractional length
	// nFractionalLength = ( (n-r) << nFractionalBits ) / n
	mpz_sub(r, *n, r);
	mpz_mul_2exp(r, r, nFractionalBits);
	mpz_fdiv_q(r, r, *n);
	unsigned int nFractionalLength = mpz_get_ui(r);
	
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
		return error("EulerLagrangeLifchitzPrimalityTest() : invalid n %% 8 = %d, %s", nMod8, (fSophieGermain? "first kind" : "second kind"));

	if (fPassedTest)
		return true;
	// Failed test, calculate fractional length
	
	// derive Fermat test remainder
	mpz_mul(r, r, r);
	mpz_fdiv_r(r, r, *n);
	
	// nFractionalLength = ( (n-r) << nFractionalBits ) / n
	mpz_sub(r, *n, r);
	mpz_mul_2exp(r, r, nFractionalBits);
	mpz_fdiv_q(r, r, *n);
	unsigned int nFractionalLength = mpz_get_ui(r);
	
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
	*pnProbableChainLength = 0;
	mpz_t N;
	mpz_init_set(N, *n);
	
	// Fermat test for n first
	if (!FermatProbablePrimalityTest(&N, pnProbableChainLength))
		return false;

	// Euler-Lagrange-Lifchitz test for the following numbers in chain
	while (true)
	{
		TargetIncrementLength(pnProbableChainLength);
		mpz_add(N, N, N);
		mpz_add_ui(N, N, (fSophieGermain? 1 : (-1)));
		if (fFermatTest)
		{
			if (!FermatProbablePrimalityTest(&N, pnProbableChainLength))
				break;
		}
		else
		{
			if (!EulerLagrangeLifchitzPrimalityTest(&N, fSophieGermain, pnProbableChainLength))
				break;
		}
	}

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
	// FIXME: if valid, free stuff?
	psieve->valid = false;
}

static
void psieve_init(struct SieveOfEratosthenes *psieve, unsigned nSieveSize, unsigned nBits, mpz_t *hashBlockHeader, mpz_t *bnFixedMultiplier)
{
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
		return error("CSieveOfEratosthenes::Weave(): BN_mod_inverse of fixed factor failed for prime #%u=%u", psieve->nPrimeSeq, nPrime);
	mpz_t bnTwo, bnTwoInverse;
	mpz_init_set_ui(bnTwo, 2);
	mpz_init(bnTwoInverse);
	if (!mpz_invert(bnTwoInverse, bnTwo, p))
		return error("CSieveOfEratosthenes::Weave(): BN_mod_inverse of 2 failed for prime #%u=%u", psieve->nPrimeSeq, nPrime);

	mpz_t mp;
	mpz_init(mp);
	
	// Weave the sieve for the prime
	unsigned int nChainLength = TargetGetLength(psieve->nBits);
	for (unsigned int nBiTwinSeq = 0; nBiTwinSeq < 2 * nChainLength; nBiTwinSeq++)
	{
		// Find the first number that's divisible by this prime
		int nDelta = ((nBiTwinSeq % 2 == 0) ? (-1) : 1);
		mpz_mul_ui(mp, bnFixedInverse, nPrime - nDelta);
		unsigned int nSolvedMultiplier = mpz_fdiv_ui(mp, nPrime);
		
		if (nBiTwinSeq % 2 == 1)
			mpz_mul(bnFixedInverse, bnFixedInverse, bnTwoInverse); // for next number in chain

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

// Mine probable prime chain of form: n = h * p# +/- 1
bool MineProbablePrimeChain(struct SieveOfEratosthenes *psieve, const uint8_t *header, mpz_t *hash, mpz_t *bnFixedMultiplier, bool *pfNewBlock, unsigned *pnTriedMultiplier, unsigned *pnProbableChainLength, unsigned *pnTests, unsigned *pnPrimesHit)
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
		psieve_init(psieve, nMaxSieveSize, *pnbits, hash, bnFixedMultiplier);
		while (psieve_Weave(psieve));
// 		printf("MineProbablePrimeChain() : new sieve (%u/%u) ready in %uus\n", psieve_GetCandidateCount(psieve), nMaxSieveSize, (unsigned int) (GetTimeMicros() - nStart));
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
			return false;
		}
		mpz_mul(bnChainOrigin, *hash, *bnFixedMultiplier);
		mpz_mul_ui(bnChainOrigin, bnChainOrigin, *pnTriedMultiplier);
		unsigned int nChainLengthCunningham1 = 0;
		unsigned int nChainLengthCunningham2 = 0;
		unsigned int nChainLengthBiTwin = 0;
		if (ProbablePrimeChainTest(&bnChainOrigin, *pnbits, false, &nChainLengthCunningham1, &nChainLengthCunningham2, &nChainLengthBiTwin))
		{
// TODO		    block.bnPrimeChainMultiplier = *bnFixedMultiplier * *pnTriedMultiplier;
// 		    printf("Probable prime chain found for block=%s!!\n  Target: %s\n  Length: (%s %s %s)\n", block.GetHash().GetHex().c_str(),
// 		    TargetToString(nbits).c_str(), TargetToString(nChainLengthCunningham1).c_str(), TargetToString(nChainLengthCunningham2).c_str(), TargetToString(nChainLengthBiTwin).c_str());
			*pnProbableChainLength = nChainLengthCunningham1;
			if (*pnProbableChainLength < nChainLengthCunningham2)
				*pnProbableChainLength = nChainLengthCunningham2;
			if (*pnProbableChainLength < nChainLengthBiTwin)
				*pnProbableChainLength = nChainLengthBiTwin;
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
	mpz_import(*hash, 4, 1, 8, -1, 0, hashb);
}

unsigned int nPrimorialHashFactor = 7;
int64_t nTimeExpected = 0;   // time expected to prime chain (micro-second)
int64_t nTimeExpectedPrev = 0; // time expected to prime chain last time
bool fIncrementPrimorial = true; // increase or decrease primorial factor
unsigned current_prime = 3;  // index 3 is prime number 7
int64_t nHPSTimerStart = 0;

void prime(uint8_t *header)
{
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
			return;
		++*nonce;
	}
	{
		char hex[9];
		bin2hex(hex, nonce, 4);
		fprintf(stderr, "Pass 1 found: %s\n", hex);
	}
	
	// primorial fixed multiplier
	mpz_t bnPrimorial;
	mpz_init(bnPrimorial);
	unsigned int nRoundTests = 0;
	unsigned int nRoundPrimesHit = 0;
	int64_t nPrimeTimerStart = GetTimeMicros();
	if (nTimeExpected > nTimeExpectedPrev)
	    fIncrementPrimorial = !fIncrementPrimorial;
	nTimeExpectedPrev = nTimeExpected;
	// dynamic adjustment of primorial multiplier
	if (fIncrementPrimorial)
	{
		++current_prime;
		if (current_prime >= PRIME_COUNT)
			quit(1, "primorial increment overflow");
	}
	else if (vPrimes[current_prime] > nPrimorialHashFactor)
	{
		if (!current_prime)
			quit(1, "primorial decrement overflow");
		--current_prime;
	}
	mpz_set(bnPrimorial, vPrimorials[current_prime]);
	
	
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
			++current_prime;
			if (current_prime >= PRIME_COUNT)
				quit(1, "primorial minimum overflow");
			mpz_set(bnPrimorial, vPrimorials[current_prime]);
		}
		
		mpz_t bnFixedMultiplier;
		mpz_init(bnFixedMultiplier);
	    // bnFixedMultiplier = (bnPrimorial > nHashFactor) ? (bnPrimorial / nHashFactor) : 1
		if (mpz_cmp_ui(bnPrimorial, nHashFactor) > 0)
		{
			mpz_t bnHashFactor;
			mpz_init_set_ui(bnHashFactor, nHashFactor);
			mpz_fdiv_q(bnFixedMultiplier, bnPrimorial, bnHashFactor);
		}
		else
			mpz_set_ui(bnFixedMultiplier, 1);
		
		
		// mine for prime chain
		unsigned int nProbableChainLength;
		if (MineProbablePrimeChain(&sieve, header, &hash, &bnFixedMultiplier, &fNewBlock, &nTriedMultiplier, &nProbableChainLength, &nTests, &nPrimesHit))
		{
// TODO			CheckWork(pblock, *pwalletMain, reservekey);
			fprintf(stderr, "CHECK WORK\n");
			break;
		}
		nRoundTests += nTests;
		nRoundPrimesHit += nPrimesHit;

	    // Meter primes/sec
	    static int64_t nPrimeCounter;
	    static int64_t nTestCounter;
	    if (nHPSTimerStart == 0)
	    {
	        nHPSTimerStart = GetTimeMillis();
	        nPrimeCounter = 0;
	        nTestCounter = 0;
	    }
	    else
	    {
	        nPrimeCounter += nPrimesHit;
	        nTestCounter += nTests;
	    }
#if 0
	    if (GetTimeMillis() - nHPSTimerStart > 60000)
	    {
	        static CCriticalSection cs;
	        {
	            LOCK(cs);
	            if (GetTimeMillis() - nHPSTimerStart > 60000)
	            {
	                double dPrimesPerMinute = 60000.0 * nPrimeCounter / (GetTimeMillis() - nHPSTimerStart);
	                dPrimesPerSec = dPrimesPerMinute / 60.0;
	                double dTestsPerMinute = 60000.0 * nTestCounter / (GetTimeMillis() - nHPSTimerStart);
	                nHPSTimerStart = GetTimeMillis();
	                nPrimeCounter = 0;
	                nTestCounter = 0;
	                static int64 nLogTime = 0;
	                if (GetTime() - nLogTime > 60)
	                {
	                    nLogTime = GetTime();
	                    printf("%s primemeter %9.0f prime/h %9.0f test/h\n", DateTimeStrFormat("%Y-%m-%d %H:%M:%S", nLogTime).c_str(), dPrimesPerMinute * 60.0, dTestsPerMinute * 60.0);
	                }
	            }
	        }
	    }
#endif

	    // Check for stop or if block needs to be rebuilt
	    // TODO
// 	    boost::this_thread::interruption_point();
// 	    if (vNodes.empty())
// 	        break;
// 	    if (fNewBlock || pblock->nNonce >= 0xffff0000)
// 	        break;
// 	    if (nTransactionsUpdated != nTransactionsUpdatedLast && GetTime() - nStart > 60)
// 	        break;
// 	    if (pindexPrev != pindexBest)
// 	        break;
	}

	// Primecoin: estimate time to block
	nTimeExpected = (GetTimeMicros() - nPrimeTimerStart) / max(1u, nRoundTests);
	nTimeExpected = nTimeExpected * max(1u, nRoundTests) / max(1u, nRoundPrimesHit);
//TODO
// 	for (unsigned int n = 1; n < TargetGetLength(pblock->nBits); n++)
// 	     nTimeExpected = nTimeExpected * max(1u, nRoundTests) * 3 / max(1u, nRoundPrimesHit);
	printf("PrimecoinMiner() : Round primorial=%u tests=%u primes=%u expected=%us\n", vPrimes[current_prime], nRoundTests, nRoundPrimesHit, (unsigned int)(nTimeExpected/1000000));
}

void main()
{
	GeneratePrimeTable();
	unsigned char array[80] = {
		0x02, 0x00, 0x00, 0x00,
		
		0x06, 0x21, 0x15, 0xa0, 0xb9, 0x7d, 0x83, 0x26, 0xff, 0xad, 0x2b, 0x82, 0x46, 0x25, 0x4e, 0x67,
		0xf9, 0x3a, 0xfb, 0x6a, 0xf5, 0xa2, 0x78, 0x80, 0x13, 0x53, 0xc7, 0x4d, 0xba, 0x17, 0x3d, 0x96,
		
		0xee, 0x52, 0x24, 0xd0, 0xf6, 0xcd, 0x53, 0x50, 0x8c, 0x4b, 0x63, 0x39, 0x1d, 0x28, 0x86, 0x9d,
		0x35, 0x21, 0xeb, 0x8d, 0x43, 0xbe, 0x82, 0xcf, 0x58, 0x48, 0x1d, 0xa0, 0xd0, 0xe4, 0x13, 0x72,
		
		0x30, 0xb3, 0xd9, 0x51,
		
		0x00, 0x00, 0x00, 0x07,
		
		0x1d, 0x00, 0x00, 0x00
	};
	prime(array);
}