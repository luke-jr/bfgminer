#ifndef ADL_SDK_H_
#define ADL_SDK_H_

#include "adl_structures.h"

typedef void*(
#ifdef WIN32
	__stdcall
#endif
*ADL_MAIN_MALLOC_CALLBACK)(int);

#endif /* ADL_SDK_H_ */
