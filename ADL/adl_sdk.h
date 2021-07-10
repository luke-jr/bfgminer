/* The statements-of-fact provided herein are intended to be compatible with
 * AMD ADL's library. AMD is the creator and copyright holder of the ADL
 * library this interface describes, and therefore also defined this interface
 * originally.
 * These free interfaces were created by Luke Dashjr <luke+freeadl@dashjr.org>
 * As interfaces/APIs cannot be copyrighted, there is no license needed in the
 * USA and probably many other jurisdictions.
 * If your jurisdiction rules otherwise, the header is offered by Luke Dashjr
 * under the MIT license, but you are responsible for determining who your
 * jurisdiction considers to be the copyright holder in such a case.
 *
 * THE INFORMATION IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE INFORMATION OR THE USE OR OTHER DEALINGS IN THE
 * INFORMATION.
 */

#ifndef ADL_SDK_H_
#define ADL_SDK_H_

#include "adl_structures.h"

typedef void*(
#ifdef __stdcall
	__stdcall
#endif
*ADL_MAIN_MALLOC_CALLBACK)(int);

#endif /* ADL_SDK_H_ */
