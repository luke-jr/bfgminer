/**********************************************************************************
 * Copyright (c) 2008-2010 The Khronos Group Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and/or associated documentation files (the
 * "Materials"), to deal in the Materials without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Materials, and to
 * permit persons to whom the Materials are furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Materials.
 *
 * THE MATERIALS ARE PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * MATERIALS OR THE USE OR OTHER DEALINGS IN THE MATERIALS.
 **********************************************************************************/

/* $Revision: 14830 $ on $Date: 2011-05-26 08:34:31 -0700 (Thu, 26 May 2011) $ */

#ifndef __CL_PLATFORM_H
#define __CL_PLATFORM_H

#ifdef __APPLE__
    /* Contains #defines for AVAILABLE_MAC_OS_X_VERSION_10_6_AND_LATER below */
    #include <AvailabilityMacros.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
#define CL_API_ENTRY
#define CL_API_CALL __stdcall
#else
#define CL_API_ENTRY
#define CL_API_CALL
#endif

#ifdef __APPLE__
#define CL_API_SUFFIX__VERSION_1_0   AVAILABLE_MAC_OS_X_VERSION_10_6_AND_LATER
#define CL_API_SUFFIX__VERSION_1_1   
#define CL_EXTENSION_WEAK_LINK       __attribute__((weak_import))       
#else
#define CL_API_SUFFIX__VERSION_1_0
#define CL_API_SUFFIX__VERSION_1_1
#define CL_EXTENSION_WEAK_LINK                         
#endif

#if (defined (_WIN32) && defined(_MSC_VER))

/* scalar types  */
typedef signed   __int8         cl_char;
typedef unsigned __int8         cl_uchar;
typedef signed   __int16        cl_short;
typedef unsigned __int16        cl_ushort;
typedef signed   __int32        cl_int;
typedef unsigned __int32        cl_uint;
typedef signed   __int64        cl_long;
typedef unsigned __int64        cl_ulong;

typedef unsigned __int16        cl_half;
typedef float                   cl_float;
typedef double                  cl_double;

/* Macro names and corresponding values defined by OpenCL */
#define CL_CHAR_BIT         8
#define CL_SCHAR_MAX        127
#define CL_SCHAR_MIN        (-127-1)
#define CL_CHAR_MAX         CL_SCHAR_MAX
#define CL_CHAR_MIN         CL_SCHAR_MIN
#define CL_UCHAR_MAX        255
#define CL_SHRT_MAX         32767
#define CL_SHRT_MIN         (-32767-1)
#define CL_USHRT_MAX        65535
#define CL_INT_MAX          2147483647
#define CL_INT_MIN          (-2147483647-1)
#define CL_UINT_MAX         0xffffffffU
#define CL_LONG_MAX         ((cl_long) 0x7FFFFFFFFFFFFFFFLL)
#define CL_LONG_MIN         ((cl_long) -0x7FFFFFFFFFFFFFFFLL - 1LL)
#define CL_ULONG_MAX        ((cl_ulong) 0xFFFFFFFFFFFFFFFFULL)

#define CL_FLT_DIG          6
#define CL_FLT_MANT_DIG     24
#define CL_FLT_MAX_10_EXP   +38
#define CL_FLT_MAX_EXP      +128
#define CL_FLT_MIN_10_EXP   -37
#define CL_FLT_MIN_EXP      -125
#define CL_FLT_RADIX        2
#define CL_FLT_MAX          340282346638528859811704183484516925440.0f
#define CL_FLT_MIN          1.175494350822287507969e-38f
#define CL_FLT_EPSILON      0x1.0p-23f

#define CL_DBL_DIG          15
#define CL_DBL_MANT_DIG     53
#define CL_DBL_MAX_10_EXP   +308
#define CL_DBL_MAX_EXP      +1024
#define CL_DBL_MIN_10_EXP   -307
#define CL_DBL_MIN_EXP      -1021
#define CL_DBL_RADIX        2
#define CL_DBL_MAX          179769313486231570814527423731704356798070567525844996598917476803157260780028538760589558632766878171540458953514382464234321326889464182768467546703537516986049910576551282076245490090389328944075868508455133942304583236903222948165808559332123348274797826204144723168738177180919299881250404026184124858368.0
#define CL_DBL_MIN          2.225073858507201383090e-308
#define CL_DBL_EPSILON      2.220446049250313080847e-16

#define CL_NAN              (CL_INFINITY - CL_INFINITY)
#define CL_HUGE_VALF        ((cl_float) 1e50)
#define CL_HUGE_VAL         ((cl_double) 1e500)
#define CL_MAXFLOAT         CL_FLT_MAX
#define CL_INFINITY         CL_HUGE_VALF

#define CL_CALLBACK         __stdcall

#else

#include <stdint.h>

/* scalar types  */
typedef int8_t          cl_char;
typedef uint8_t         cl_uchar;
typedef int16_t         cl_short    __attribute__((aligned(2)));
typedef uint16_t        cl_ushort   __attribute__((aligned(2)));
typedef int32_t         cl_int      __attribute__((aligned(4)));
typedef uint32_t        cl_uint     __attribute__((aligned(4)));
typedef int64_t         cl_long     __attribute__((aligned(8)));
typedef uint64_t        cl_ulong    __attribute__((aligned(8)));

typedef uint16_t        cl_half     __attribute__((aligned(2)));
typedef float           cl_float    __attribute__((aligned(4)));
typedef double          cl_double   __attribute__((aligned(8)));

/* Macro names and corresponding values defined by OpenCL */
#define CL_CHAR_BIT         8
#define CL_SCHAR_MAX        127
#define CL_SCHAR_MIN        (-127-1)
#define CL_CHAR_MAX         CL_SCHAR_MAX
#define CL_CHAR_MIN         CL_SCHAR_MIN
#define CL_UCHAR_MAX        255
#define CL_SHRT_MAX         32767
#define CL_SHRT_MIN         (-32767-1)
#define CL_USHRT_MAX        65535
#define CL_INT_MAX          2147483647
#define CL_INT_MIN          (-2147483647-1)
#define CL_UINT_MAX         0xffffffffU
#define CL_LONG_MAX         ((cl_long) 0x7FFFFFFFFFFFFFFFLL)
#define CL_LONG_MIN         ((cl_long) -0x7FFFFFFFFFFFFFFFLL - 1LL)
#define CL_ULONG_MAX        ((cl_ulong) 0xFFFFFFFFFFFFFFFFULL)

#define CL_FLT_DIG          6
#define CL_FLT_MANT_DIG     24
#define CL_FLT_MAX_10_EXP   +38
#define CL_FLT_MAX_EXP      +128
#define CL_FLT_MIN_10_EXP   -37
#define CL_FLT_MIN_EXP      -125
#define CL_FLT_RADIX        2
#define CL_FLT_MAX          0x1.fffffep127f
#define CL_FLT_MIN          0x1.0p-126f
#define CL_FLT_EPSILON      0x1.0p-23f

#define CL_DBL_DIG          15
#define CL_DBL_MANT_DIG     53
#define CL_DBL_MAX_10_EXP   +308
#define CL_DBL_MAX_EXP      +1024
#define CL_DBL_MIN_10_EXP   -307
#define CL_DBL_MIN_EXP      -1021
#define CL_DBL_RADIX        2
#define CL_DBL_MAX          0x1.fffffffffffffp1023
#define CL_DBL_MIN          0x1.0p-1022
#define CL_DBL_EPSILON      0x1.0p-52

#if (defined( __GNUC__ ) || defined( __IBMC__ ))
   #define CL_HUGE_VALF     __builtin_huge_valf()
   #define CL_HUGE_VAL      __builtin_huge_val()
   #define CL_NAN           __builtin_nanf( "" )
#else
   #define CL_HUGE_VALF     ((cl_float) 1e50)
   #define CL_HUGE_VAL      ((cl_double) 1e500)
   float nanf( const char * );
   #define CL_NAN           nanf( "" )  
#endif
#define CL_MAXFLOAT         CL_FLT_MAX
#define CL_INFINITY         CL_HUGE_VALF

#define CL_CALLBACK         

#endif

#include <stddef.h>

/* Mirror types to GL types. Mirror types allow us to avoid deciding which headers to load based on whether we are using GL or GLES here. */
typedef unsigned int cl_GLuint;
typedef int          cl_GLint;
typedef unsigned int cl_GLenum;

/*
 * Vector types 
 *
 *  Note:   OpenCL requires that all types be naturally aligned. 
 *          This means that vector types must be naturally aligned.
 *          For example, a vector of four floats must be aligned to
 *          a 16 byte boundary (calculated as 4 * the natural 4-byte 
 *          alignment of the float).  The alignment qualifiers here
 *          will only function properly if your compiler supports them
 *          and if you don't actively work to defeat them.  For example,
 *          in order for a cl_float4 to be 16 byte aligned in a struct,
 *          the start of the struct must itself be 16-byte aligned. 
 *
 *          Maintaining proper alignment is the user's responsibility.
 */

/* Define basic vector types */
#if defined( __VEC__ )
   #include <altivec.h>   /* may be omitted depending on compiler. AltiVec spec provides no way to detect whether the header is required. */
   typedef vector unsigned char     __cl_uchar16;
   typedef vector signed char       __cl_char16;
   typedef vector unsigned short    __cl_ushort8;
   typedef vector signed short      __cl_short8;
   typedef vector unsigned int      __cl_uint4;
   typedef vector signed int        __cl_int4;
   typedef vector float             __cl_float4;
   #define  __CL_UCHAR16__  1
   #define  __CL_CHAR16__   1
   #define  __CL_USHORT8__  1
   #define  __CL_SHORT8__   1
   #define  __CL_UINT4__    1
   #define  __CL_INT4__     1
   #define  __CL_FLOAT4__   1
#endif

#if defined( __SSE__ )
    #if defined( __MINGW64__ )
        #include <intrin.h>
    #else
        #include <xmmintrin.h>
    #endif
    #if defined( __GNUC__ )
        typedef float __cl_float4   __attribute__((vector_size(16)));
    #else
        typedef __m128 __cl_float4;
    #endif
    #define __CL_FLOAT4__   1
#endif

#if defined( __SSE2__ )
    #if defined( __MINGW64__ )
        #include <intrin.h>
    #else
        #include <emmintrin.h>
    #endif
    #if defined( __GNUC__ )
        typedef cl_uchar    __cl_uchar16    __attribute__((vector_size(16)));
        typedef cl_char     __cl_char16     __attribute__((vector_size(16)));
        typedef cl_ushort   __cl_ushort8    __attribute__((vector_size(16)));
        typedef cl_short    __cl_short8     __attribute__((vector_size(16)));
        typedef cl_uint     __cl_uint4      __attribute__((vector_size(16)));
        typedef cl_int      __cl_int4       __attribute__((vector_size(16)));
        typedef cl_ulong    __cl_ulong2     __attribute__((vector_size(16)));
        typedef cl_long     __cl_long2      __attribute__((vector_size(16)));
        typedef cl_double   __cl_double2    __attribute__((vector_size(16)));
    #else
        typedef __m128i __cl_uchar16;
        typedef __m128i __cl_char16;
        typedef __m128i __cl_ushort8;
        typedef __m128i __cl_short8;
        typedef __m128i __cl_uint4;
        typedef __m128i __cl_int4;
        typedef __m128i __cl_ulong2;
        typedef __m128i __cl_long2;
        typedef __m128d __cl_double2;
    #endif
    #define __CL_UCHAR16__  1
    #define __CL_CHAR16__   1
    #define __CL_USHORT8__  1
    #define __CL_SHORT8__   1
    #define __CL_INT4__     1
    #define __CL_UINT4__    1
    #define __CL_ULONG2__   1
    #define __CL_LONG2__    1
    #define __CL_DOUBLE2__  1
#endif

#if defined( __MMX__ )
    #include <mmintrin.h>
    #if defined( __GNUC__ )
        typedef cl_uchar    __cl_uchar8     __attribute__((vector_size(8)));
        typedef cl_char     __cl_char8      __attribute__((vector_size(8)));
        typedef cl_ushort   __cl_ushort4    __attribute__((vector_size(8)));
        typedef cl_short    __cl_short4     __attribute__((vector_size(8)));
        typedef cl_uint     __cl_uint2      __attribute__((vector_size(8)));
        typedef cl_int      __cl_int2       __attribute__((vector_size(8)));
        typedef cl_ulong    __cl_ulong1     __attribute__((vector_size(8)));
        typedef cl_long     __cl_long1      __attribute__((vector_size(8)));
        typedef cl_float    __cl_float2     __attribute__((vector_size(8)));
    #else
        typedef __m64       __cl_uchar8;
        typedef __m64       __cl_char8;
        typedef __m64       __cl_ushort4;
        typedef __m64       __cl_short4;
        typedef __m64       __cl_uint2;
        typedef __m64       __cl_int2;
        typedef __m64       __cl_ulong1;
        typedef __m64       __cl_long1;
        typedef __m64       __cl_float2;
    #endif
    #define __CL_UCHAR8__   1
    #define __CL_CHAR8__    1
    #define __CL_USHORT4__  1
    #define __CL_SHORT4__   1
    #define __CL_INT2__     1
    #define __CL_UINT2__    1
    #define __CL_ULONG1__   1
    #define __CL_LONG1__    1
    #define __CL_FLOAT2__   1
#endif

#if defined( __AVX__ )
    #if defined( __MINGW64__ )
        #include <intrin.h>
    #else
        #include <immintrin.h> 
    #endif
    #if defined( __GNUC__ )
        typedef cl_float    __cl_float8     __attribute__((vector_size(32)));
        typedef cl_double   __cl_double4    __attribute__((vector_size(32)));
    #else
        typedef __m256      __cl_float8;
        typedef __m256d     __cl_double4;
    #endif
    #define __CL_FLOAT8__   1
    #define __CL_DOUBLE4__  1
#endif

/* Define alignment keys */
#if (defined( __GNUC__ ) || defined( __IBMC__ ))
    #define CL_ALIGNED(_x)          __attribute__ ((aligned(_x)))
#elif defined( _WIN32) && (_MSC_VER)
    /* Alignment keys neutered on windows because MSVC can't swallow function arguments with alignment requirements     */
    /* http://msdn.microsoft.com/en-us/library/373ak2y1%28VS.71%29.aspx                                                 */
    /* #include <crtdefs.h>                                                                                             */
    /* #define CL_ALIGNED(_x)          _CRT_ALIGN(_x)                                                                   */
    #define CL_ALIGNED(_x)
#else
   #warning  Need to implement some method to align data here
   #define  CL_ALIGNED(_x)
#endif

/* Indicate whether .xyzw, .s0123 and .hi.lo are supported */
#if (defined( __GNUC__) ||  defined( __IBMC__ )) && ! defined( __STRICT_ANSI__ )
    /* .xyzw and .s0123...{f|F} are supported */
    #define CL_HAS_NAMED_VECTOR_FIELDS 1
    /* .hi and .lo are supported */
    #define CL_HAS_HI_LO_VECTOR_FIELDS 1
#endif

/* Define cl_vector types */

/* ---- cl_charn ---- */
typedef union
{
    cl_char  CL_ALIGNED(2) s[2];
#if (defined( __GNUC__) ||  defined( __IBMC__ )) && ! defined( __STRICT_ANSI__ )
   __extension__ struct{ cl_char  x, y; };
   __extension__ struct{ cl_char  s0, s1; };
   __extension__ struct{ cl_char  lo, hi; };
#endif
#if defined( __CL_CHAR2__) 
    __cl_char2     v2;
#endif
}cl_char2;

typedef union
{
    cl_char  CL_ALIGNED(4) s[4];
#if (defined( __GNUC__) ||  defined( __IBMC__ )) && ! defined( __STRICT_ANSI__ )
   __extension__ struct{ cl_char  x, y, z, w; };
   __extension__ struct{ cl_char  s0, s1, s2, s3; };
   __extension__ struct{ cl_char2 lo, hi; };
#endif
#if defined( __CL_CHAR2__) 
    __cl_char2     v2[2];
#endif
#if defined( __CL_CHAR4__) 
    __cl_char4     v4;
#endif
}cl_char4;

typedef union
{
    cl_char   CL_ALIGNED(8) s[8];
#if (defined( __GNUC__) ||  defined( __IBMC__ )) && ! defined( __STRICT_ANSI__ )
   __extension__ struct{ cl_char  x, y, z, w; };
   __extension__ struct{ cl_char  s0, s1, s2, s3, s4, s5, s6, s7; };
   __extension__ struct{ cl_char4 lo, hi; };
#endif
#if defined( __CL_CHAR2__) 
    __cl_char2     v2[4];
#endif
#if defined( __CL_CHAR4__) 
    __cl_char4     v4[2];
#endif
#if defined( __CL_CHAR8__ )
    __cl_char8     v8;
#endif
}cl_char8;

typedef union
{
    cl_char  CL_ALIGNED(16) s[16];
#if (defined( __GNUC__) ||  defined( __IBMC__ )) && ! defined( __STRICT_ANSI__ )
   __extension__ struct{ cl_char  x, y, z, w, __spacer4, __spacer5, __spacer6, __spacer7, __spacer8, __spacer9, sa, sb, sc, sd, se, sf; };
   __extension__ struct{ cl_char  s0, s1, s2, s3, s4, s5, s6, s7, s8, s9, sA, sB, sC, sD, sE, sF; };
   __extension__ struct{ cl_char8 lo, hi; };
#endif
#if defined( __CL_CHAR2__) 
    __cl_char2     v2[8];
#endif
#if defined( __CL_CHAR4__) 
    __cl_char4     v4[4];
#endif
#if defined( __CL_CHAR8__ )
    __cl_char8     v8[2];
#endif
#if defined( __CL_CHAR16__ )
    __cl_char16    v16;
#endif
}cl_char16;


/* ---- cl_ucharn ---- */
typedef union
{
    cl_uchar  CL_ALIGNED(2) s[2];
#if (defined( __GNUC__) ||  defined( __IBMC__ )) && ! defined( __STRICT_ANSI__ )
   __extension__ struct{ cl_uchar  x, y; };
   __extension__ struct{ cl_uchar  s0, s1; };
   __extension__ struct{ cl_uchar  lo, hi; };
#endif
#if defined( __cl_uchar2__) 
    __cl_uchar2     v2;
#endif
}cl_uchar2;

typedef union
{
    cl_uchar  CL_ALIGNED(4) s[4];
#if (defined( __GNUC__) ||  defined( __IBMC__ )) && ! defined( __STRICT_ANSI__ )
   __extension__ struct{ cl_uchar  x, y, z, w; };
   __extension__ struct{ cl_uchar  s0, s1, s2, s3; };
   __extension__ struct{ cl_uchar2 lo, hi; };
#endif
#if defined( __CL_UCHAR2__) 
    __cl_uchar2     v2[2];
#endif
#if defined( __CL_UCHAR4__) 
    __cl_uchar4     v4;
#endif
}cl_uchar4;

typedef union
{
    cl_uchar   CL_ALIGNED(8) s[8];
#if (defined( __GNUC__) ||  defined( __IBMC__ )) && ! defined( __STRICT_ANSI__ )
   __extension__ struct{ cl_uchar  x, y, z, w; };
   __extension__ struct{ cl_uchar  s0, s1, s2, s3, s4, s5, s6, s7; };
   __extension__ struct{ cl_uchar4 lo, hi; };
#endif
#if defined( __CL_UCHAR2__) 
    __cl_uchar2     v2[4];
#endif
#if defined( __CL_UCHAR4__) 
    __cl_uchar4     v4[2];
#endif
#if defined( __CL_UCHAR8__ )
    __cl_uchar8     v8;
#endif
}cl_uchar8;

typedef union
{
    cl_uchar  CL_ALIGNED(16) s[16];
#if (defined( __GNUC__) ||  defined( __IBMC__ )) && ! defined( __STRICT_ANSI__ )
   __extension__ struct{ cl_uchar  x, y, z, w, __spacer4, __spacer5, __spacer6, __spacer7, __spacer8, __spacer9, sa, sb, sc, sd, se, sf; };
   __extension__ struct{ cl_uchar  s0, s1, s2, s3, s4, s5, s6, s7, s8, s9, sA, sB, sC, sD, sE, sF; };
   __extension__ struct{ cl_uchar8 lo, hi; };
#endif
#if defined( __CL_UCHAR2__) 
    __cl_uchar2     v2[8];
#endif
#if defined( __CL_UCHAR4__) 
    __cl_uchar4     v4[4];
#endif
#if defined( __CL_UCHAR8__ )
    __cl_uchar8     v8[2];
#endif
#if defined( __CL_UCHAR16__ )
    __cl_uchar16    v16;
#endif
}cl_uchar16;


/* ---- cl_shortn ---- */
typedef union
{
    cl_short  CL_ALIGNED(4) s[2];
#if (defined( __GNUC__) ||  defined( __IBMC__ )) && ! defined( __STRICT_ANSI__ )
   __extension__ struct{ cl_short  x, y; };
   __extension__ struct{ cl_short  s0, s1; };
   __extension__ struct{ cl_short  lo, hi; };
#endif
#if defined( __CL_SHORT2__) 
    __cl_short2     v2;
#endif
}cl_short2;

typedef union
{
    cl_short  CL_ALIGNED(8) s[4];
#if (defined( __GNUC__) ||  defined( __IBMC__ )) && ! defined( __STRICT_ANSI__ )
   __extension__ struct{ cl_short  x, y, z, w; };
   __extension__ struct{ cl_short  s0, s1, s2, s3; };
   __extension__ struct{ cl_short2 lo, hi; };
#endif
#if defined( __CL_SHORT2__) 
    __cl_short2     v2[2];
#endif
#if defined( __CL_SHORT4__) 
    __cl_short4     v4;
#endif
}cl_short4;

typedef union
{
    cl_short   CL_ALIGNED(16) s[8];
#if (defined( __GNUC__) ||  defined( __IBMC__ )) && ! defined( __STRICT_ANSI__ )
   __extension__ struct{ cl_short  x, y, z, w; };
   __extension__ struct{ cl_short  s0, s1, s2, s3, s4, s5, s6, s7; };
   __extension__ struct{ cl_short4 lo, hi; };
#endif
#if defined( __CL_SHORT2__) 
    __cl_short2     v2[4];
#endif
#if defined( __CL_SHORT4__) 
    __cl_short4     v4[2];
#endif
#if defined( __CL_SHORT8__ )
    __cl_short8     v8;
#endif
}cl_short8;

typedef union
{
    cl_short  CL_ALIGNED(32) s[16];
#if (defined( __GNUC__) ||  defined( __IBMC__ )) && ! defined( __STRICT_ANSI__ )
   __extension__ struct{ cl_short  x, y, z, w, __spacer4, __spacer5, __spacer6, __spacer7, __spacer8, __spacer9, sa, sb, sc, sd, se, sf; };
   __extension__ struct{ cl_short  s0, s1, s2, s3, s4, s5, s6, s7, s8, s9, sA, sB, sC, sD, sE, sF; };
   __extension__ struct{ cl_short8 lo, hi; };
#endif
#if defined( __CL_SHORT2__) 
    __cl_short2     v2[8];
#endif
#if defined( __CL_SHORT4__) 
    __cl_short4     v4[4];
#endif
#if defined( __CL_SHORT8__ )
    __cl_short8     v8[2];
#endif
#if defined( __CL_SHORT16__ )
    __cl_short16    v16;
#endif
}cl_short16;


/* ---- cl_ushortn ---- */
typedef union
{
    cl_ushort  CL_ALIGNED(4) s[2];
#if (defined( __GNUC__) ||  defined( __IBMC__ )) && ! defined( __STRICT_ANSI__ )
   __extension__ struct{ cl_ushort  x, y; };
   __extension__ struct{ cl_ushort  s0, s1; };
   __extension__ struct{ cl_ushort  lo, hi; };
#endif
#if defined( __CL_USHORT2__) 
    __cl_ushort2     v2;
#endif
}cl_ushort2;

typedef union
{
    cl_ushort  CL_ALIGNED(8) s[4];
#if (defined( __GNUC__) ||  defined( __IBMC__ )) && ! defined( __STRICT_ANSI__ )
   __extension__ struct{ cl_ushort  x, y, z, w; };
   __extension__ struct{ cl_ushort  s0, s1, s2, s3; };
   __extension__ struct{ cl_ushort2 lo, hi; };
#endif
#if defined( __CL_USHORT2__) 
    __cl_ushort2     v2[2];
#endif
#if defined( __CL_USHORT4__) 
    __cl_ushort4     v4;
#endif
}cl_ushort4;

typedef union
{
    cl_ushort   CL_ALIGNED(16) s[8];
#if (defined( __GNUC__) ||  defined( __IBMC__ )) && ! defined( __STRICT_ANSI__ )
   __extension__ struct{ cl_ushort  x, y, z, w; };
   __extension__ struct{ cl_ushort  s0, s1, s2, s3, s4, s5, s6, s7; };
   __extension__ struct{ cl_ushort4 lo, hi; };
#endif
#if defined( __CL_USHORT2__) 
    __cl_ushort2     v2[4];
#endif
#if defined( __CL_USHORT4__) 
    __cl_ushort4     v4[2];
#endif
#if defined( __CL_USHORT8__ )
    __cl_ushort8     v8;
#endif
}cl_ushort8;

typedef union
{
    cl_ushort  CL_ALIGNED(32) s[16];
#if (defined( __GNUC__) ||  defined( __IBMC__ )) && ! defined( __STRICT_ANSI__ )
   __extension__ struct{ cl_ushort  x, y, z, w, __spacer4, __spacer5, __spacer6, __spacer7, __spacer8, __spacer9, sa, sb, sc, sd, se, sf; };
   __extension__ struct{ cl_ushort  s0, s1, s2, s3, s4, s5, s6, s7, s8, s9, sA, sB, sC, sD, sE, sF; };
   __extension__ struct{ cl_ushort8 lo, hi; };
#endif
#if defined( __CL_USHORT2__) 
    __cl_ushort2     v2[8];
#endif
#if defined( __CL_USHORT4__) 
    __cl_ushort4     v4[4];
#endif
#if defined( __CL_USHORT8__ )
    __cl_ushort8     v8[2];
#endif
#if defined( __CL_USHORT16__ )
    __cl_ushort16    v16;
#endif
}cl_ushort16;

/* ---- cl_intn ---- */
typedef union
{
    cl_int  CL_ALIGNED(8) s[2];
#if (defined( __GNUC__) ||  defined( __IBMC__ )) && ! defined( __STRICT_ANSI__ )
   __extension__ struct{ cl_int  x, y; };
   __extension__ struct{ cl_int  s0, s1; };
   __extension__ struct{ cl_int  lo, hi; };
#endif
#if defined( __CL_INT2__) 
    __cl_int2     v2;
#endif
}cl_int2;

typedef union
{
    cl_int  CL_ALIGNED(16) s[4];
#if (defined( __GNUC__) ||  defined( __IBMC__ )) && ! defined( __STRICT_ANSI__ )
   __extension__ struct{ cl_int  x, y, z, w; };
   __extension__ struct{ cl_int  s0, s1, s2, s3; };
   __extension__ struct{ cl_int2 lo, hi; };
#endif
#if defined( __CL_INT2__) 
    __cl_int2     v2[2];
#endif
#if defined( __CL_INT4__) 
    __cl_int4     v4;
#endif
}cl_int4;

typedef union
{
    cl_int   CL_ALIGNED(32) s[8];
#if (defined( __GNUC__) ||  defined( __IBMC__ )) && ! defined( __STRICT_ANSI__ )
   __extension__ struct{ cl_int  x, y, z, w; };
   __extension__ struct{ cl_int  s0, s1, s2, s3, s4, s5, s6, s7; };
   __extension__ struct{ cl_int4 lo, hi; };
#endif
#if defined( __CL_INT2__) 
    __cl_int2     v2[4];
#endif
#if defined( __CL_INT4__) 
    __cl_int4     v4[2];
#endif
#if defined( __CL_INT8__ )
    __cl_int8     v8;
#endif
}cl_int8;

typedef union
{
    cl_int  CL_ALIGNED(64) s[16];
#if (defined( __GNUC__) ||  defined( __IBMC__ )) && ! defined( __STRICT_ANSI__ )
   __extension__ struct{ cl_int  x, y, z, w, __spacer4, __spacer5, __spacer6, __spacer7, __spacer8, __spacer9, sa, sb, sc, sd, se, sf; };
   __extension__ struct{ cl_int  s0, s1, s2, s3, s4, s5, s6, s7, s8, s9, sA, sB, sC, sD, sE, sF; };
   __extension__ struct{ cl_int8 lo, hi; };
#endif
#if defined( __CL_INT2__) 
    __cl_int2     v2[8];
#endif
#if defined( __CL_INT4__) 
    __cl_int4     v4[4];
#endif
#if defined( __CL_INT8__ )
    __cl_int8     v8[2];
#endif
#if defined( __CL_INT16__ )
    __cl_int16    v16;
#endif
}cl_int16;


/* ---- cl_uintn ---- */
typedef union
{
    cl_uint  CL_ALIGNED(8) s[2];
#if (defined( __GNUC__) ||  defined( __IBMC__ )) && ! defined( __STRICT_ANSI__ )
   __extension__ struct{ cl_uint  x, y; };
   __extension__ struct{ cl_uint  s0, s1; };
   __extension__ struct{ cl_uint  lo, hi; };
#endif
#if defined( __CL_UINT2__) 
    __cl_uint2     v2;
#endif
}cl_uint2;

typedef union
{
    cl_uint  CL_ALIGNED(16) s[4];
#if (defined( __GNUC__) ||  defined( __IBMC__ )) && ! defined( __STRICT_ANSI__ )
   __extension__ struct{ cl_uint  x, y, z, w; };
   __extension__ struct{ cl_uint  s0, s1, s2, s3; };
   __extension__ struct{ cl_uint2 lo, hi; };
#endif
#if defined( __CL_UINT2__) 
    __cl_uint2     v2[2];
#endif
#if defined( __CL_UINT4__) 
    __cl_uint4     v4;
#endif
}cl_uint4;

typedef union
{
    cl_uint   CL_ALIGNED(32) s[8];
#if (defined( __GNUC__) ||  defined( __IBMC__ )) && ! defined( __STRICT_ANSI__ )
   __extension__ struct{ cl_uint  x, y, z, w; };
   __extension__ struct{ cl_uint  s0, s1, s2, s3, s4, s5, s6, s7; };
   __extension__ struct{ cl_uint4 lo, hi; };
#endif
#if defined( __CL_UINT2__) 
    __cl_uint2     v2[4];
#endif
#if defined( __CL_UINT4__) 
    __cl_uint4     v4[2];
#endif
#if defined( __CL_UINT8__ )
    __cl_uint8     v8;
#endif
}cl_uint8;

typedef union
{
    cl_uint  CL_ALIGNED(64) s[16];
#if (defined( __GNUC__) ||  defined( __IBMC__ )) && ! defined( __STRICT_ANSI__ )
   __extension__ struct{ cl_uint  x, y, z, w, __spacer4, __spacer5, __spacer6, __spacer7, __spacer8, __spacer9, sa, sb, sc, sd, se, sf; };
   __extension__ struct{ cl_uint  s0, s1, s2, s3, s4, s5, s6, s7, s8, s9, sA, sB, sC, sD, sE, sF; };
   __extension__ struct{ cl_uint8 lo, hi; };
#endif
#if defined( __CL_UINT2__) 
    __cl_uint2     v2[8];
#endif
#if defined( __CL_UINT4__) 
    __cl_uint4     v4[4];
#endif
#if defined( __CL_UINT8__ )
    __cl_uint8     v8[2];
#endif
#if defined( __CL_UINT16__ )
    __cl_uint16    v16;
#endif
}cl_uint16;

/* ---- cl_longn ---- */
typedef union
{
    cl_long  CL_ALIGNED(16) s[2];
#if (defined( __GNUC__) ||  defined( __IBMC__ )) && ! defined( __STRICT_ANSI__ )
   __extension__ struct{ cl_long  x, y; };
   __extension__ struct{ cl_long  s0, s1; };
   __extension__ struct{ cl_long  lo, hi; };
#endif
#if defined( __CL_LONG2__) 
    __cl_long2     v2;
#endif
}cl_long2;

typedef union
{
    cl_long  CL_ALIGNED(32) s[4];
#if (defined( __GNUC__) ||  defined( __IBMC__ )) && ! defined( __STRICT_ANSI__ )
   __extension__ struct{ cl_long  x, y, z, w; };
   __extension__ struct{ cl_long  s0, s1, s2, s3; };
   __extension__ struct{ cl_long2 lo, hi; };
#endif
#if defined( __CL_LONG2__) 
    __cl_long2     v2[2];
#endif
#if defined( __CL_LONG4__) 
    __cl_long4     v4;
#endif
}cl_long4;

typedef union
{
    cl_long   CL_ALIGNED(64) s[8];
#if (defined( __GNUC__) ||  defined( __IBMC__ )) && ! defined( __STRICT_ANSI__ )
   __extension__ struct{ cl_long  x, y, z, w; };
   __extension__ struct{ cl_long  s0, s1, s2, s3, s4, s5, s6, s7; };
   __extension__ struct{ cl_long4 lo, hi; };
#endif
#if defined( __CL_LONG2__) 
    __cl_long2     v2[4];
#endif
#if defined( __CL_LONG4__) 
    __cl_long4     v4[2];
#endif
#if defined( __CL_LONG8__ )
    __cl_long8     v8;
#endif
}cl_long8;

typedef union
{
    cl_long  CL_ALIGNED(128) s[16];
#if (defined( __GNUC__) ||  defined( __IBMC__ )) && ! defined( __STRICT_ANSI__ )
   __extension__ struct{ cl_long  x, y, z, w, __spacer4, __spacer5, __spacer6, __spacer7, __spacer8, __spacer9, sa, sb, sc, sd, se, sf; };
   __extension__ struct{ cl_long  s0, s1, s2, s3, s4, s5, s6, s7, s8, s9, sA, sB, sC, sD, sE, sF; };
   __extension__ struct{ cl_long8 lo, hi; };
#endif
#if defined( __CL_LONG2__) 
    __cl_long2     v2[8];
#endif
#if defined( __CL_LONG4__) 
    __cl_long4     v4[4];
#endif
#if defined( __CL_LONG8__ )
    __cl_long8     v8[2];
#endif
#if defined( __CL_LONG16__ )
    __cl_long16    v16;
#endif
}cl_long16;


/* ---- cl_ulongn ---- */
typedef union
{
    cl_ulong  CL_ALIGNED(16) s[2];
#if (defined( __GNUC__) ||  defined( __IBMC__ )) && ! defined( __STRICT_ANSI__ )
   __extension__ struct{ cl_ulong  x, y; };
   __extension__ struct{ cl_ulong  s0, s1; };
   __extension__ struct{ cl_ulong  lo, hi; };
#endif
#if defined( __CL_ULONG2__) 
    __cl_ulong2     v2;
#endif
}cl_ulong2;

typedef union
{
    cl_ulong  CL_ALIGNED(32) s[4];
#if (defined( __GNUC__) ||  defined( __IBMC__ )) && ! defined( __STRICT_ANSI__ )
   __extension__ struct{ cl_ulong  x, y, z, w; };
   __extension__ struct{ cl_ulong  s0, s1, s2, s3; };
   __extension__ struct{ cl_ulong2 lo, hi; };
#endif
#if defined( __CL_ULONG2__) 
    __cl_ulong2     v2[2];
#endif
#if defined( __CL_ULONG4__) 
    __cl_ulong4     v4;
#endif
}cl_ulong4;

typedef union
{
    cl_ulong   CL_ALIGNED(64) s[8];
#if (defined( __GNUC__) ||  defined( __IBMC__ )) && ! defined( __STRICT_ANSI__ )
   __extension__ struct{ cl_ulong  x, y, z, w; };
   __extension__ struct{ cl_ulong  s0, s1, s2, s3, s4, s5, s6, s7; };
   __extension__ struct{ cl_ulong4 lo, hi; };
#endif
#if defined( __CL_ULONG2__) 
    __cl_ulong2     v2[4];
#endif
#if defined( __CL_ULONG4__) 
    __cl_ulong4     v4[2];
#endif
#if defined( __CL_ULONG8__ )
    __cl_ulong8     v8;
#endif
}cl_ulong8;

typedef union
{
    cl_ulong  CL_ALIGNED(128) s[16];
#if (defined( __GNUC__) ||  defined( __IBMC__ )) && ! defined( __STRICT_ANSI__ )
   __extension__ struct{ cl_ulong  x, y, z, w, __spacer4, __spacer5, __spacer6, __spacer7, __spacer8, __spacer9, sa, sb, sc, sd, se, sf; };
   __extension__ struct{ cl_ulong  s0, s1, s2, s3, s4, s5, s6, s7, s8, s9, sA, sB, sC, sD, sE, sF; };
   __extension__ struct{ cl_ulong8 lo, hi; };
#endif
#if defined( __CL_ULONG2__) 
    __cl_ulong2     v2[8];
#endif
#if defined( __CL_ULONG4__) 
    __cl_ulong4     v4[4];
#endif
#if defined( __CL_ULONG8__ )
    __cl_ulong8     v8[2];
#endif
#if defined( __CL_ULONG16__ )
    __cl_ulong16    v16;
#endif
}cl_ulong16;


/* --- cl_floatn ---- */

typedef union
{
    cl_float  CL_ALIGNED(8) s[2];
#if (defined( __GNUC__) ||  defined( __IBMC__ )) && ! defined( __STRICT_ANSI__ )
   __extension__ struct{ cl_float  x, y; };
   __extension__ struct{ cl_float  s0, s1; };
   __extension__ struct{ cl_float  lo, hi; };
#endif
#if defined( __CL_FLOAT2__) 
    __cl_float2     v2;
#endif
}cl_float2;

typedef union
{
    cl_float  CL_ALIGNED(16) s[4];
#if (defined( __GNUC__) ||  defined( __IBMC__ )) && ! defined( __STRICT_ANSI__ )
   __extension__ struct{ cl_float   x, y, z, w; };
   __extension__ struct{ cl_float   s0, s1, s2, s3; };
   __extension__ struct{ cl_float2  lo, hi; };
#endif
#if defined( __CL_FLOAT2__) 
    __cl_float2     v2[2];
#endif
#if defined( __CL_FLOAT4__) 
    __cl_float4     v4;
#endif
}cl_float4;

typedef union
{
    cl_float   CL_ALIGNED(32) s[8];
#if (defined( __GNUC__) ||  defined( __IBMC__ )) && ! defined( __STRICT_ANSI__ )
   __extension__ struct{ cl_float   x, y, z, w; };
   __extension__ struct{ cl_float   s0, s1, s2, s3, s4, s5, s6, s7; };
   __extension__ struct{ cl_float4  lo, hi; };
#endif
#if defined( __CL_FLOAT2__) 
    __cl_float2     v2[4];
#endif
#if defined( __CL_FLOAT4__) 
    __cl_float4     v4[2];
#endif
#if defined( __CL_FLOAT8__ )
    __cl_float8     v8;
#endif
}cl_float8;

typedef union
{
    cl_float  CL_ALIGNED(64) s[16];
#if (defined( __GNUC__) ||  defined( __IBMC__ )) && ! defined( __STRICT_ANSI__ )
   __extension__ struct{ cl_float  x, y, z, w, __spacer4, __spacer5, __spacer6, __spacer7, __spacer8, __spacer9, sa, sb, sc, sd, se, sf; };
   __extension__ struct{ cl_float  s0, s1, s2, s3, s4, s5, s6, s7, s8, s9, sA, sB, sC, sD, sE, sF; };
   __extension__ struct{ cl_float8 lo, hi; };
#endif
#if defined( __CL_FLOAT2__) 
    __cl_float2     v2[8];
#endif
#if defined( __CL_FLOAT4__) 
    __cl_float4     v4[4];
#endif
#if defined( __CL_FLOAT8__ )
    __cl_float8     v8[2];
#endif
#if defined( __CL_FLOAT16__ )
    __cl_float16    v16;
#endif
}cl_float16;

/* --- cl_doublen ---- */

typedef union
{
    cl_double  CL_ALIGNED(16) s[2];
#if (defined( __GNUC__) ||  defined( __IBMC__ )) && ! defined( __STRICT_ANSI__ )
   __extension__ struct{ cl_double  x, y; };
   __extension__ struct{ cl_double s0, s1; };
   __extension__ struct{ cl_double lo, hi; };
#endif
#if defined( __CL_DOUBLE2__) 
    __cl_double2     v2;
#endif
}cl_double2;

typedef union
{
    cl_double  CL_ALIGNED(32) s[4];
#if (defined( __GNUC__) ||  defined( __IBMC__ )) && ! defined( __STRICT_ANSI__ )
   __extension__ struct{ cl_double  x, y, z, w; };
   __extension__ struct{ cl_double  s0, s1, s2, s3; };
   __extension__ struct{ cl_double2 lo, hi; };
#endif
#if defined( __CL_DOUBLE2__) 
    __cl_double2     v2[2];
#endif
#if defined( __CL_DOUBLE4__) 
    __cl_double4     v4;
#endif
}cl_double4;

typedef union
{
    cl_double   CL_ALIGNED(64) s[8];
#if (defined( __GNUC__) ||  defined( __IBMC__ )) && ! defined( __STRICT_ANSI__ )
   __extension__ struct{ cl_double  x, y, z, w; };
   __extension__ struct{ cl_double  s0, s1, s2, s3, s4, s5, s6, s7; };
   __extension__ struct{ cl_double4 lo, hi; };
#endif
#if defined( __CL_DOUBLE2__) 
    __cl_double2     v2[4];
#endif
#if defined( __CL_DOUBLE4__) 
    __cl_double4     v4[2];
#endif
#if defined( __CL_DOUBLE8__ )
    __cl_double8     v8;
#endif
}cl_double8;

typedef union
{
    cl_double  CL_ALIGNED(128) s[16];
#if (defined( __GNUC__) ||  defined( __IBMC__ )) && ! defined( __STRICT_ANSI__ )
   __extension__ struct{ cl_double  x, y, z, w, __spacer4, __spacer5, __spacer6, __spacer7, __spacer8, __spacer9, sa, sb, sc, sd, se, sf; };
   __extension__ struct{ cl_double  s0, s1, s2, s3, s4, s5, s6, s7, s8, s9, sA, sB, sC, sD, sE, sF; };
   __extension__ struct{ cl_double8 lo, hi; };
#endif
#if defined( __CL_DOUBLE2__) 
    __cl_double2     v2[8];
#endif
#if defined( __CL_DOUBLE4__) 
    __cl_double4     v4[4];
#endif
#if defined( __CL_DOUBLE8__ )
    __cl_double8     v8[2];
#endif
#if defined( __CL_DOUBLE16__ )
    __cl_double16    v16;
#endif
}cl_double16;

  
#ifdef __cplusplus
}
#endif

#endif  /* __CL_PLATFORM_H  */
