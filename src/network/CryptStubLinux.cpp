#if defined(LINUX_PORT) && !defined(_WIN32)

#include "ComPacketHeader.h"

#ifdef USE_CLIENT_CONNECTION

#include "network/CryptStubLinux.h"

int TFCCrypt::DecryptC(LPBYTE &pBuffer, int &pBufferSize, unsigned int dwKey) {
    (void)pBuffer;
    (void)pBufferSize;
    (void)dwKey;
    (void)m_dwKEY;
    return 1;
}

int TFCCrypt::EncryptC(LPBYTE &pBuffer, int &pBufferSize, unsigned int dwKey) {
    (void)pBuffer;
    (void)pBufferSize;
    (void)dwKey;
    (void)m_dwKEY;
    return 1;
}

DWORD TFCCrypt::EncryptC2(LPBYTE &pBuffer, int &pBufferSize, unsigned int dwKey) {
    (void)pBuffer;
    (void)pBufferSize;
    (void)dwKey;
    return 0;
}

#endif /* USE_CLIENT_CONNECTION */

#endif
