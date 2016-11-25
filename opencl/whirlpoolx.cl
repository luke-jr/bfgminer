#ifndef WHIRLPOOLX_CL
#define WHIRLPOOLX_CL

/*
	Where are the other tables? You'll probably feel stupid when I tell you, but the T1 - T7
	tables are all copies of the T0 table, with every ulong rotated left by the table number
	of bytes. Basically, T1 is T0 rotated left 8 bits, T2 is T0 rotated left 16 bits, and so on.
	Makes one hell of a lot more sense to create them dynamically (and/or rotate instead), but
	few things in the stock miners make sense.
*/

__constant static const ulong T0_C[256] =
{
	0xD83078C018601818UL, 0x2646AF05238C2323UL, 0xB891F97EC63FC6C6UL, 0xFBCD6F13E887E8E8UL,
	0xCB13A14C87268787UL, 0x116D62A9B8DAB8B8UL, 0x0902050801040101UL, 0x0D9E6E424F214F4FUL,
	0x9B6CEEAD36D83636UL, 0xFF510459A6A2A6A6UL, 0x0CB9BDDED26FD2D2UL, 0x0EF706FBF5F3F5F5UL,
	0x96F280EF79F97979UL, 0x30DECE5F6FA16F6FUL, 0x6D3FEFFC917E9191UL, 0xF8A407AA52555252UL,
	0x47C0FD27609D6060UL, 0x35657689BCCABCBCUL, 0x372BCDAC9B569B9BUL, 0x8A018C048E028E8EUL,
	0xD25B1571A3B6A3A3UL, 0x6C183C600C300C0CUL, 0x84F68AFF7BF17B7BUL, 0x806AE1B535D43535UL,
	0xF53A69E81D741D1DUL, 0xB3DD4753E0A7E0E0UL, 0x21B3ACF6D77BD7D7UL, 0x9C99ED5EC22FC2C2UL,
	0x435C966D2EB82E2EUL, 0x29967A624B314B4BUL, 0x5DE121A3FEDFFEFEUL, 0xD5AE168257415757UL,
	0xBD2A41A815541515UL, 0xE8EEB69F77C17777UL, 0x926EEBA537DC3737UL, 0x9ED7567BE5B3E5E5UL,
	0x1323D98C9F469F9FUL, 0x23FD17D3F0E7F0F0UL, 0x20947F6A4A354A4AUL, 0x44A9959EDA4FDADAUL,
	0xA2B025FA587D5858UL, 0xCF8FCA06C903C9C9UL, 0x7C528D5529A42929UL, 0x5A1422500A280A0AUL,
	0x507F4FE1B1FEB1B1UL, 0xC95D1A69A0BAA0A0UL, 0x14D6DA7F6BB16B6BUL, 0xD917AB5C852E8585UL,
	0x3C677381BDCEBDBDUL, 0x8FBA34D25D695D5DUL, 0x9020508010401010UL, 0x07F503F3F4F7F4F4UL,
	0xDD8BC016CB0BCBCBUL, 0xD37CC6ED3EF83E3EUL, 0x2D0A112805140505UL, 0x78CEE61F67816767UL,
	0x97D55373E4B7E4E4UL, 0x024EBB25279C2727UL, 0x7382583241194141UL, 0xA70B9D2C8B168B8BUL,
	0xF6530151A7A6A7A7UL, 0xB2FA94CF7DE97D7DUL, 0x4937FBDC956E9595UL, 0x56AD9F8ED847D8D8UL,
	0x70EB308BFBCBFBFBUL, 0xCDC17123EE9FEEEEUL, 0xBBF891C77CED7C7CUL, 0x71CCE31766856666UL,
	0x7BA78EA6DD53DDDDUL, 0xAF2E4BB8175C1717UL, 0x458E460247014747UL, 0x1A21DC849E429E9EUL,
	0xD489C51ECA0FCACAUL, 0x585A99752DB42D2DUL, 0x2E637991BFC6BFBFUL, 0x3F0E1B38071C0707UL,
	0xAC472301AD8EADADUL, 0xB0B42FEA5A755A5AUL, 0xEF1BB56C83368383UL, 0xB666FF8533CC3333UL,
	0x5CC6F23F63916363UL, 0x12040A1002080202UL, 0x93493839AA92AAAAUL, 0xDEE2A8AF71D97171UL,
	0xC68DCF0EC807C8C8UL, 0xD1327DC819641919UL, 0x3B92707249394949UL, 0x5FAF9A86D943D9D9UL,
	0x31F91DC3F2EFF2F2UL, 0xA8DB484BE3ABE3E3UL, 0xB9B62AE25B715B5BUL, 0xBC0D9234881A8888UL,
	0x3E29C8A49A529A9AUL, 0x0B4CBE2D26982626UL, 0xBF64FA8D32C83232UL, 0x597D4AE9B0FAB0B0UL,
	0xF2CF6A1BE983E9E9UL, 0x771E33780F3C0F0FUL, 0x33B7A6E6D573D5D5UL, 0xF41DBA74803A8080UL,
	0x27617C99BEC2BEBEUL, 0xEB87DE26CD13CDCDUL, 0x8968E4BD34D03434UL, 0x3290757A483D4848UL,
	0x54E324ABFFDBFFFFUL, 0x8DF48FF77AF57A7AUL, 0x643DEAF4907A9090UL, 0x9DBE3EC25F615F5FUL,
	0x3D40A01D20802020UL, 0x0FD0D56768BD6868UL, 0xCA3472D01A681A1AUL, 0xB7412C19AE82AEAEUL,
	0x7D755EC9B4EAB4B4UL, 0xCEA8199A544D5454UL, 0x7F3BE5EC93769393UL, 0x2F44AA0D22882222UL,
	0x63C8E907648D6464UL, 0x2AFF12DBF1E3F1F1UL, 0xCCE6A2BF73D17373UL, 0x82245A9012481212UL,
	0x7A805D3A401D4040UL, 0x4810284008200808UL, 0x959BE856C32BC3C3UL, 0xDFC57B33EC97ECECUL,
	0x4DAB9096DB4BDBDBUL, 0xC05F1F61A1BEA1A1UL, 0x9107831C8D0E8D8DUL, 0xC87AC9F53DF43D3DUL,
	0x5B33F1CC97669797UL, 0x0000000000000000UL, 0xF983D436CF1BCFCFUL, 0x6E5687452BAC2B2BUL,
	0xE1ECB39776C57676UL, 0xE619B06482328282UL, 0x28B1A9FED67FD6D6UL, 0xC33677D81B6C1B1BUL,
	0x74775BC1B5EEB5B5UL, 0xBE432911AF86AFAFUL, 0x1DD4DF776AB56A6AUL, 0xEAA00DBA505D5050UL,
	0x578A4C1245094545UL, 0x38FB18CBF3EBF3F3UL, 0xAD60F09D30C03030UL, 0xC4C3742BEF9BEFEFUL,
	0xDA7EC3E53FFC3F3FUL, 0xC7AA1C9255495555UL, 0xDB591079A2B2A2A2UL, 0xE9C96503EA8FEAEAUL,
	0x6ACAEC0F65896565UL, 0x036968B9BAD2BABAUL, 0x4A5E93652FBC2F2FUL, 0x8E9DE74EC027C0C0UL,
	0x60A181BEDE5FDEDEUL, 0xFC386CE01C701C1CUL, 0x46E72EBBFDD3FDFDUL, 0x1F9A64524D294D4DUL,
	0x7639E0E492729292UL, 0xFAEABC8F75C97575UL, 0x360C1E3006180606UL, 0xAE0998248A128A8AUL,
	0x4B7940F9B2F2B2B2UL, 0x85D15963E6BFE6E6UL, 0x7E1C36700E380E0EUL, 0xE73E63F81F7C1F1FUL,
	0x55C4F73762956262UL, 0x3AB5A3EED477D4D4UL, 0x814D3229A89AA8A8UL, 0x5231F4C496629696UL,
	0x62EF3A9BF9C3F9F9UL, 0xA397F666C533C5C5UL, 0x104AB13525942525UL, 0xABB220F259795959UL,
	0xD015AE54842A8484UL, 0xC5E4A7B772D57272UL, 0xEC72DDD539E43939UL, 0x1698615A4C2D4C4CUL,
	0x94BC3BCA5E655E5EUL, 0x9FF085E778FD7878UL, 0xE570D8DD38E03838UL, 0x980586148C0A8C8CUL,
	0x17BFB2C6D163D1D1UL, 0xE4570B41A5AEA5A5UL, 0xA1D94D43E2AFE2E2UL, 0x4EC2F82F61996161UL,
	0x427B45F1B3F6B3B3UL, 0x3442A51521842121UL, 0x0825D6949C4A9C9CUL, 0xEE3C66F01E781E1EUL,
	0x6186522243114343UL, 0xB193FC76C73BC7C7UL, 0x4FE52BB3FCD7FCFCUL, 0x2408142004100404UL,
	0xE3A208B251595151UL, 0x252FC7BC995E9999UL, 0x22DAC44F6DA96D6DUL, 0x651A39680D340D0DUL,
	0x79E93583FACFFAFAUL, 0x69A384B6DF5BDFDFUL, 0xA9FC9BD77EE57E7EUL, 0x1948B43D24902424UL,
	0xFE76D7C53BEC3B3BUL, 0x9A4B3D31AB96ABABUL, 0xF081D13ECE1FCECEUL, 0x9922558811441111UL,
	0x8303890C8F068F8FUL, 0x049C6B4A4E254E4EUL, 0x667351D1B7E6B7B7UL, 0xE0CB600BEB8BEBEBUL,
	0xC178CCFD3CF03C3CUL, 0xFD1FBF7C813E8181UL, 0x4035FED4946A9494UL, 0x1CF30CEBF7FBF7F7UL,
	0x186F67A1B9DEB9B9UL, 0x8B265F98134C1313UL, 0x51589C7D2CB02C2CUL, 0x05BBB8D6D36BD3D3UL,
	0x8CD35C6BE7BBE7E7UL, 0x39DCCB576EA56E6EUL, 0xAA95F36EC437C4C4UL, 0x1B060F18030C0303UL,
	0xDCAC138A56455656UL, 0x5E88491A440D4444UL, 0xA0FE9EDF7FE17F7FUL, 0x884F3721A99EA9A9UL,
	0x6754824D2AA82A2AUL, 0x0A6B6DB1BBD6BBBBUL, 0x879FE246C123C1C1UL, 0xF1A602A253515353UL,
	0x72A58BAEDC57DCDCUL, 0x531627580B2C0B0BUL, 0x0127D39C9D4E9D9DUL, 0x2BD8C1476CAD6C6CUL,
	0xA462F59531C43131UL, 0xF3E8B98774CD7474UL, 0x15F109E3F6FFF6F6UL, 0x4C8C430A46054646UL,
	0xA5452609AC8AACACUL, 0xB50F973C891E8989UL, 0xB42844A014501414UL, 0xBADF425BE1A3E1E1UL,
	0xA62C4EB016581616UL, 0xF774D2CD3AE83A3AUL, 0x06D2D06F69B96969UL, 0x41122D4809240909UL,
	0xD7E0ADA770DD7070UL, 0x6F7154D9B6E2B6B6UL, 0x1EBDB7CED067D0D0UL, 0xD6C77E3BED93EDEDUL,
	0xE285DB2ECC17CCCCUL, 0x6884572A42154242UL, 0x2C2DC2B4985A9898UL, 0xED550E49A4AAA4A4UL,
	0x7550885D28A02828UL, 0x86B831DA5C6D5C5CUL, 0x6BED3F93F8C7F8F8UL, 0xC211A44486228686UL
};

__constant static const ulong ROUND_CONSTANTS[10] = 
{
	0x4F01B887E8C62318UL, 0x52916F79F5D2A636UL, 0x357B0CA38E9BBC60UL, 0x57FE4B2EC2D7E01DUL,
	0xDA4AF09FE5377715UL, 0x856BA0B10A29C958UL, 0x67053ECBF4105DBDUL, 0xD8957DA78B4127E4UL,
	0x9E4717DD667CEEFBUL, 0x33835AAD07BF2DCAUL
};

/*
	That BYTE macro was criminal. AMD has an instruction that is quite useful for this purpose - Bitfield Extract.
	The AMD OpenCL compiler is often VERY stupid, and cannot be relied on to compile ridiculous code into clever
	instructions like BFE. However, remember two things about the amd_bfe built-in function: One, while it's preferable
	to convoluted multiplications (*shudder*), bitshifts, and AND masks, as it compiles to one instruction - it requires
	the OPENCL_EXTENSION pragma to enable cl_amd_media_ops2 (example below), and two, it can only work on uints and below, 
	not ulongs. As you can see, for the extraction of bits from the high 32, I shift the upper 32 bits down and cast to
	uint to fix this.
*/

#pragma OPENCL EXTENSION cl_amd_media_ops2 : enable

/*
	Note that while the compiler is pretty much clinically brain-dead half the time, it CAN do very basic things reliably.
	This is why I reduced the complexity of my BYTELO and BYTEHI macros to a new BYTE one (I left the former two for demonstration.)
	It resolves the two usages of the ternary operator into constants at compile time, after it inlines the macros, because every time
	I use BYTE, the y argument is known at compile time. Therefore, while it's a bit less easy to read, it's more compact to use one macro.
*/

//#define BYTELO(x, y)		(amd_bfe((uint)(x), (y), 8U))
//#define BYTEHI(x, y)		(amd_bfe((uint)((x) >> 32), (y) - 32U, 8U))

#define BYTE(x, y)			(amd_bfe((uint)((x) >> ((y >= 32U) ? 32U : 0U)), (y) - (((y) >= 32) ? 32U : 0), 8U))

/*
	Macro here to differentiate between the round implementations for Hawaii and Tonga versus all of the earlier cards; I'm most interested
	in making sure it works well for Tahiti and Pitcairn, though. More on why they're different below.
*/

#if defined(__Hawaii__) || defined(__Tonga__)
	
	#define W_ROUND(in, i0, i1, i2, i3, i4, i5, i6, i7)	(T0[BYTE(in.s ## i0, 0U)] ^ T1[BYTE(in.s ## i1, 8U)] ^ T2[BYTE(in.s ## i2, 16U)] ^ T3[BYTE(in.s ## i3, 24U)] ^ \
															rotate(T0[BYTE(in.s ## i4, 32U)], 32UL) ^ rotate(T0[BYTE(in.s ## i5, 40U)], 40UL) ^ rotate(T0[BYTE(in.s ## i6, 48U)], 48UL) ^ \
															rotate(T0[BYTE(in.s ## i7, 56U)], 56UL))
	
#else
	
	#define W_ROUND(in, i0, i1, i2, i3, i4, i5, i6, i7)	(T0[BYTE(in.s ## i0, 0U)] ^ T1[BYTE(in.s ## i1, 8U)] ^ rotate(T0[BYTE(in.s ## i2, 16U)], 16UL) ^ rotate(T0[BYTE(in.s ## i3, 24U)], 24UL) ^ \
															rotate(T0[BYTE(in.s ## i4, 32U)], 32UL) ^ rotate(T0[BYTE(in.s ## i5, 40U)], 40UL) ^ rotate(T0[BYTE(in.s ## i6, 48U)], 48UL) ^ \
															rotate(T0[BYTE(in.s ## i7, 56U)], 56UL))
	
#endif

/*
	The kernel parameters probably look odd, and the reason for that is likely another thing that will make you feel
	like you should have thought of it before now - the first execution of Whirlpool is actually constant! It does
	not depend on the value of the nonce in any way. So, I precompute it every time there's new work, and pass it to
	the kernel. Simple, easy increase - almost makes me wonder if it was an intentional oversight...
	
	Anyways, since we consumed the first part of the block making the midstate (Whirlpool consumes 64 bytes, or a
	ulong8, remember), this leaves us with one ulong, a uint, and our nonce, which is the global ID. So, input is
	therefore our input is the first ulong after the eight consumed by the midstate hash operation, then the low
	32 bits of the second. This would be ulongs number 8 and the low half 9, if you had the whole thing in a ulong
	array. The nonce (global ID) goes into where the high part of 9 would go, and then the input must be terminated
	with a '1' bit. Since it's supposed to be a big-endian '1' bit, I use the little-endian representation, that being
	0x80. After that, the input must be padded with zeros, and the last block terminated by the length of the input that
	was processed, as a 64-bit big-endian integer. Note, this includes all previous whole blocks that have been processed;
	many hash functions work this way, see "Merkle–Damgard construction" on Google for more information on this type of
	hash function construction. Long story short, it enhances security versus just padding to the end of the block with
	zeros, or some other constant, and it defines a system for padding to the end of a block (even with an odd number of
	bits) so that everyone who hashes the same thing gets the same hash.
	
	In case you didn't figure it out, the pointer to the block data was replaced by two constant ulongs containing the
	values of the block data, indexes 8 and 9, when indexed 64 bits at a time. Those are input0 and input1.
*/

__attribute__((reqd_work_group_size(WORKSIZE, 1, 1)))
__kernel void search(const ulong8 midstate, const ulong input0, const ulong input1, __global uint *output, const ulong target)
{
	/*
		Note that if you don't specify a type label, the variables automatically go in private memory if they can,
		but I'm being explicit here, so there's no question.
		
		Also, I find ulong8 to be a lot cleaner here, as we often operate with 8 ulongs at a time. Why the hell would
		you loop the XOR operations and shit constantly? Besides the fact it's ugly as hell, it's also more complex...
		and this compiler is bad even with simple code, sometimes.
	*/
	
	__private uint gid = get_global_id(0);
	__private ulong8 n, h = midstate;
	__local ulong T0[256], T1[256];
	
	/*
		Hawaii and Tonga both either have more LDS than their earlier brothers (speaking specifically of Tahiti and Pitcairn based GPUs),
		or they allow more waves in flight with more LDS in use. Either way, GPUs based on Hawaii and newer chips, such as Tonga, seem
		to benefit from more LDS usage here, as it doesn't seem to hurt the amount of waves they may have in flight at a time.
	*/
		
	
	#if defined(__Hawaii__) || defined(__Tonga__)
		
		__local ulong T2[256], T3[256];
		
	#endif
	
	#if WORKSIZE == 256
		
		__private uint lid = get_local_id(0);
		
		T0[lid] = T0_C[lid];
		T1[lid] = rotate(T0_C[lid], 8UL);
		
		#if defined(__Hawaii__) || defined(__Tonga__)
			
			T2[lid] = rotate(T0_C[lid], 16UL);
			T3[lid] = rotate(T0_C[lid], 24UL);
			
		#endif
		
	#else
		
		for(uint lid = get_local_id(0); lid < 256; lid += WORKSIZE)
		{
			T0[lid] = T0_C[lid];
			T1[lid] = rotate(T0_C[lid], 8UL);
			
			#if defined(__Hawaii__) || defined(__Tonga__)
				
				T2[lid] = rotate(T0_C[lid], 16UL);
				T3[lid] = rotate(T0_C[lid], 24UL);
				
			#endif			
		}
		
	#endif
	
	mem_fence(CLK_LOCAL_MEM_FENCE);
	
	n = (ulong8)(input0, (input1 & 0x00000000FFFFFFFF) | ((ulong)gid << 32), 0x0000000000000080, 0, 0, 0, 0, 0x8002000000000000) ^ h;

	/*
	
	// Just for fun, this loop could also be written like so:
	
	#pragma unroll 2
	for(int i = 0; i < 20; ++i)
	{
		ulong8 t;
		
		t.s0 = W_ROUND(((i & 1) ? n : h), 0, 7, 6, 5, 4, 3, 2, 1) ^ ((i & 1) ? 0 : ROUND_CONSTANTS[i >> 1]);
		t.s1 = W_ROUND(((i & 1) ? n : h), 1, 0, 7, 6, 5, 4, 3, 2);
		t.s2 = W_ROUND(((i & 1) ? n : h), 2, 1, 0, 7, 6, 5, 4, 3);
		t.s3 = W_ROUND(((i & 1) ? n : h), 3, 2, 1, 0, 7, 6, 5, 4);
		t.s4 = W_ROUND(((i & 1) ? n : h), 4, 3, 2, 1, 0, 7, 6, 5);
		t.s5 = W_ROUND(((i & 1) ? n : h), 5, 4, 3, 2, 1, 0, 7, 6);
		t.s6 = W_ROUND(((i & 1) ? n : h), 6, 5, 4, 3, 2, 1, 0, 7);
		t.s7 = W_ROUND(((i & 1) ? n : h), 7, 6, 5, 4, 3, 2, 1, 0);
		
		h = ((i & 1) ? h : t);
		n = ((i & 1) ? h ^ t : n);
	}
	
	// On second thought, that might be cleaner looking with if statements... meh.
	
	*/
	
	/*
		Whirlpool is actually based on a block cipher that is designed much like Rijndael (AES), but is unlikely to be used,
		in my opinion, for encryption purposes - due to the rather large size of the state, and as such, has much larger
		tables to deal with when trying to make an efficient implementation. Basically, in the Whirlpool specification,
		it shows how it is based off of a block cipher they named W, which I've renamed ROUND_ELT here, as I find it more
		appropriate. It works VERY much like Rijndael internally, therefore, it can be put into tables quite easily. As for the
		key schedule, that differs substantially from Rijndael - each round key is simply an execution of the W round function
		on the key. Here, the round keys are calculated each round - that is, they are generated as needed, rather than in a
		seperate loop. This is why there are technically two iterations of Whirlpool in the loop below, one to calculate the
		key for the round, another to calculate the state, and then they are XOR'd in the AddRoundKey step - The SubBytes,
		ShiftColumns, and MixRows steps having been computed using the tables in LDS. Whirlpool, like Rijndael, can be computed
		without the use of tables, but for some odd reason, it seems almost no one on the internet has ever done it. Even the
		official reference implementations of Whirlpool are devoid of an implmentation that does not rely on precomputed tables.
		I have found one that doesn't, and then rewrote it to bitslice the S-box used in SubBytes and greatly simplify the
		finite field multiplications used in MixRows, to do Whirlpool with exactly zero table lookups. It's fucking awesome,
		but sadly quite slow on GPU. Should be the shit on FPGA, though. It will be located at the following URL when I get
		around to cleaning it up and shit:
		
		https://ottrbutt.com/miner/wpl_bitslice_final.c
		
		However, you can see the messy, yet fully functional version now at this URL:
		
		https://ottrbutt.com/miner/wpltest.c
	*/
	
	// This loop is rolled up for a reason, by the way. I know what you're thinking - unrolling helped last time! Go ahead, try it.
	
	#pragma unroll 1
	for(int i = 0; i < 10; ++i)
	{
		ulong8 t;
		
		t.s0 = W_ROUND(h, 0, 7, 6, 5, 4, 3, 2, 1) ^ ROUND_CONSTANTS[i];
		t.s1 = W_ROUND(h, 1, 0, 7, 6, 5, 4, 3, 2);
		t.s2 = W_ROUND(h, 2, 1, 0, 7, 6, 5, 4, 3);
		t.s3 = W_ROUND(h, 3, 2, 1, 0, 7, 6, 5, 4);
		t.s4 = W_ROUND(h, 4, 3, 2, 1, 0, 7, 6, 5);
		t.s5 = W_ROUND(h, 5, 4, 3, 2, 1, 0, 7, 6);
		t.s6 = W_ROUND(h, 6, 5, 4, 3, 2, 1, 0, 7);
		t.s7 = W_ROUND(h, 7, 6, 5, 4, 3, 2, 1, 0);
		
		h = t;
		
		t.s0 = W_ROUND(n, 0, 7, 6, 5, 4, 3, 2, 1);
		t.s1 = W_ROUND(n, 1, 0, 7, 6, 5, 4, 3, 2);
		t.s2 = W_ROUND(n, 2, 1, 0, 7, 6, 5, 4, 3);
		t.s3 = W_ROUND(n, 3, 2, 1, 0, 7, 6, 5, 4);
		t.s4 = W_ROUND(n, 4, 3, 2, 1, 0, 7, 6, 5);
		t.s5 = W_ROUND(n, 5, 4, 3, 2, 1, 0, 7, 6);
		t.s6 = W_ROUND(n, 6, 5, 4, 3, 2, 1, 0, 7);
		t.s7 = W_ROUND(n, 7, 6, 5, 4, 3, 2, 1, 0);
		
		n = t ^ h;
	}
	
	/*
		The end of Whirlpool would have me XOR the input (in the midstate variable) with the current state (in the n variable), but as
		we only need the third ulong to tell if this nonce is a winner, we may as well only XOR what we need to. The compiler will most
		likely apply this optimization by itself, but I prefer to ensure the compiler doesn't fuck my code up, at least, as much as I
		reasonably can.
		
		You can not use atomic_inc() here if you like, but it's cleaner to do so, as two shares may be found at the same time, doing
		God knows what to the output array. It's unlikely, but possible, so I use atomic_inc() whenever I make miners.
		
		The original SWAP4 macro was rather stupid - their little rotate trick will be faster on CPU, but GPUs tend to prefer vector
		operations, even if they don't have hardware vectors, like AMD's GCN cards (7xxx and up, in case you haven't done your homework.)
		Therefore, explicit OpenCL cast to uchar4, reverse bytes, and explicit cast back to uint should be quicker, not that it matters much.
	*/
	
	if((midstate.s3 ^ n.s3 ^ midstate.s5 ^ n.s5) <= target) output[atomic_inc(output+0xFF)] = as_uint(as_uchar4(gid).s3210);
}

#endif	// WHIRLPOOLX_CL