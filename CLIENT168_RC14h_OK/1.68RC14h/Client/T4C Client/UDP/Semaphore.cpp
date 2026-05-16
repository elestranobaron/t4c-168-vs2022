#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "Semaphore.h"

#if defined(LINUX_PORT) && !defined(_WIN32)

CNMSemaphore::CNMSemaphore(unsigned char count, unsigned int waitsec) {
    WaitSec = static_cast<long>(waitsec);
    initCount = count;
    currentCount = 0;
    (void)initCount;
}

CNMSemaphore::~CNMSemaphore() {}

int CNMSemaphore::Wait() {
    std::unique_lock<std::mutex> lk(mtx);
    if (WaitSec > 0) {
        const bool ok = cv.wait_for(lk, std::chrono::milliseconds(static_cast<unsigned int>(WaitSec)),
                                     [&] { return currentCount > 0; });
        if (!ok) {
            return 1; /* NM_MUTEX_TIMEOUT */
        }
    } else {
        cv.wait(lk, [&] { return currentCount > 0; });
    }
    currentCount--;
    return 0;
}

int CNMSemaphore::Post() {
    std::lock_guard<std::mutex> lk(mtx);
    currentCount++;
    cv.notify_one();
    return 0;
}

#else

#include <windows.h>

CNMSemaphore::CNMSemaphore(unsigned char count, unsigned int waitsec) {
    WaitSec = waitsec;
    initCount = count;
    sa = NULL;
    semaphore = NULL;

    sa = (LPSECURITY_ATTRIBUTES)HeapAlloc(GetProcessHeap(), 0, sizeof(SECURITY_ATTRIBUTES));
    sa->nLength = sizeof(SECURITY_ATTRIBUTES);
    sa->lpSecurityDescriptor = NULL;
    sa->bInheritHandle = FALSE;

    if ((semaphore = CreateSemaphore(sa, 0, 0x7fffffff, 0)) == NULL) {
        return;
    }
}

CNMSemaphore::~CNMSemaphore() {
    if (sa)
        HeapFree(GetProcessHeap(), 0, sa);

    if (semaphore) {
        CloseHandle(semaphore);
        semaphore = NULL;
    }
}

int CNMSemaphore::Wait() {
    if (WaitSec) {
        DWORD retVal = WaitForSingleObject(semaphore, WaitSec);

        if (retVal == WAIT_TIMEOUT)
            return 1;
        if (retVal == WAIT_OBJECT_0)
            return 0;

        if (retVal == WAIT_FAILED) {
            return -1;
        }
    } else {
        if (WaitForSingleObject(semaphore, INFINITE) == WAIT_FAILED) {
            return -1;
        }
        return 0;
    }
    return -1;
}

int CNMSemaphore::Post() {
    if (!ReleaseSemaphore(semaphore, 1, NULL)) {
        return -1;
    }
    return 0;
}

#endif
