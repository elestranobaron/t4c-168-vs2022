#pragma once

/**
 * Préambule Linux pour CommCenter / files UDP (Mutex, Semaphore, Lock).
 * À inclure avant tout header client qui attendait windows.h.
 */
#if defined(LINUX_PORT) && !defined(_WIN32) && !defined(_WIN64)

#include "network/SocketCompatibility.h"
#include "network/IOCPCompat.h"

#include <SDL3/SDL.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <strings.h>

#ifndef stricmp
#define stricmp strcasecmp
#endif

#ifndef __cdecl
#define __cdecl
#endif

#ifndef THREAD_PRIORITY_HIGHEST
#define THREAD_PRIORITY_HIGHEST 2
#endif
#ifndef THREAD_PRIORITY_ABOVE_NORMAL
#define THREAD_PRIORITY_ABOVE_NORMAL 1
#endif
#ifndef THREAD_PRIORITY_NORMAL
#define THREAD_PRIORITY_NORMAL 0
#endif

/* -------------------------------------------------------------------------- */
/* Types Win32 usuels                                                         */
/* -------------------------------------------------------------------------- */

#ifndef BYTE
typedef unsigned char BYTE;
#endif
#ifndef UINT
typedef unsigned int UINT;
#endif
#ifndef WORD
typedef std::uint16_t WORD;
#endif
#ifndef USHORT
typedef WORD USHORT;
#endif
#ifndef DWORD
typedef std::uint32_t DWORD;
#endif
#ifndef ULONG
typedef DWORD ULONG;
#endif
#ifndef BOOL
typedef int BOOL;
#endif
#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#ifndef LPVOID
typedef void *LPVOID;
#endif
#ifndef LPCSTR
typedef const char *LPCSTR;
#endif
#ifndef LPSTR
typedef char *LPSTR;
#endif
#ifndef LPBYTE
typedef BYTE *LPBYTE;
#endif
#ifndef LPOVERLAPPED
typedef void *LPOVERLAPPED;
#endif

typedef long HRESULT;
#ifndef S_OK
#define S_OK static_cast<HRESULT>(0L)
#endif

typedef struct _SECURITY_ATTRIBUTES {
    DWORD nLength;
    LPVOID lpSecurityDescriptor;
    BOOL bInheritHandle;
} SECURITY_ATTRIBUTES, *LPSECURITY_ATTRIBUTES;

typedef struct _OSVERSIONINFOA {
    DWORD dwOSVersionInfoSize;
    DWORD dwMajorVersion;
    DWORD dwMinorVersion;
    DWORD dwBuildNumber;
    DWORD dwPlatformId;
    char szCSDVersion[128];
} OSVERSIONINFOA, *LPOSVERSIONINFOA;

typedef OSVERSIONINFOA OSVERSIONINFO;

#ifndef VER_PLATFORM_WIN32_NT
#define VER_PLATFORM_WIN32_NT 2
#endif

#ifndef INFINITE
#define INFINITE 0xFFFFFFFFu
#endif

#ifndef WAIT_OBJECT_0
#define WAIT_OBJECT_0 0u
#endif
#ifndef WAIT_FAILED
#define WAIT_FAILED static_cast<DWORD>(0xFFFFFFFFu)
#endif

/* Note : ne pas définir WAIT_TIMEOUT ici — collision possible avec <sys/wait.h>. */

/* -------------------------------------------------------------------------- */
/* API process / thread (impl. dans T4CLinuxCommPort.cpp)                     */
/* -------------------------------------------------------------------------- */

typedef void(__cdecl *linux_beginthread_proc_t)(void *);

uintptr_t linux_beginthreadex_compat(linux_beginthread_proc_t proc, unsigned stack_size, void *arg);

DWORD linux_wait_for_single_object(uintptr_t handle, DWORD milliseconds);

BOOL linux_terminate_thread(uintptr_t handle, DWORD exit_code);

BOOL linux_set_thread_priority(uintptr_t handle, int priority);

DWORD linux_get_current_thread_id(void);

BOOL linux_get_version_ex_a(OSVERSIONINFOA *info);

HRESULT linux_co_initialize(void *reserved);

void linux_co_uninitialize(void);

void linux_output_debug_string_a(const char *msg);

/** Réveille les threads bloqués sur GetQueuedCompletionStatus pour un port émulé. */
int IOCPCompatCancelIo(HANDLE port_handle);

/* Alias attendus par CommCenter.cpp / headers historiques */
#ifndef _beginthread
#define _beginthread(fn, stack, arg) reinterpret_cast<HANDLE>(linux_beginthreadex_compat((fn), (stack), (arg)))
#endif

#ifndef WaitForSingleObject
#define WaitForSingleObject(h, ms) linux_wait_for_single_object(reinterpret_cast<uintptr_t>(h), (ms))
#endif

#ifndef TerminateThread
#define TerminateThread(h, code) linux_terminate_thread(reinterpret_cast<uintptr_t>(h), (code))
#endif

#ifndef SetThreadPriority
#define SetThreadPriority(h, p) linux_set_thread_priority(reinterpret_cast<uintptr_t>(h), (p))
#endif

#ifndef GetCurrentThreadId
#define GetCurrentThreadId() linux_get_current_thread_id()
#endif

#ifndef GetVersionEx
#define GetVersionEx linux_get_version_ex_a
#endif

#ifndef CoInitialize
#define CoInitialize(x) linux_co_initialize(x)
#endif

#ifndef CoUninitialize
#define CoUninitialize() linux_co_uninitialize()
#endif

#ifndef OutputDebugString
#define OutputDebugString(x) linux_output_debug_string_a(x)
#endif

#ifndef CancelIo
#define CancelIo(h) IOCPCompatCancelIo((h))
#endif

#ifndef GetTickCount
#define GetTickCount() static_cast<DWORD>(SDL_GetTicks())
#endif

#ifndef timeGetTime
#define timeGetTime() static_cast<DWORD>(SDL_GetTicks())
#endif

#ifndef Sleep
#define Sleep(ms) SDL_Delay(static_cast<Uint32>(ms))
#endif

#endif /* LINUX_PORT && !_WIN32 */
