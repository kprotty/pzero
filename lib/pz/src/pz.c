/* Copyright (c) 2020 kprotty
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "pz.h"

PzPanicHandler __pz_panic_handler = NULL;

PZ_EXTERN void PzSetPanicHandler(PzPanicHandler panic_handler) {
    __pz_panic_handler = panic_handler;
}

PZ_EXTERN void PzCallPanicHandler(const char* file, int line_no, const char* fmt, ...) {
    PzPanicHandler panic_handler = __pz_panic_handler;

    if (PZ_UNLIKELY(panic_handler == NULL)) {
        #if defined(PZ_GCC)
            __builtin_trap();
        #else
            while (true) {}
        #endif
    }
    
    va_list args;
    va_start(args, fmt);
    (panic_handler)(file, line_no, fmt, args);
    va_end(args);

    PZ_UNREACHABLE();
}

#if defined(_WIN32)
    #include "windows.h"
    #if defined(__cplusplus)
        extern "C" {
    #endif

    PZ_EXTERN BOOL DllMain(
        HINSTANCE hinstDLL,
        DWORD dwReason,
        PVOID lpReserved
    ) {
        return TRUE;
    }

    PZ_EXTERN BOOL _DllMainCRTStartup(
        HINSTANCE hinstDLL,
        DWORD dwReason,
        PVOID lpReserved
    ) {
        return DllMain(hinstDLL, dwReason, lpReserved);
    }

    #if defined(__cplusplus)
        }
    #endif
#endif
