#ifndef T4C_CRYPTMESTOPH_CRYPT_H
#define T4C_CRYPTMESTOPH_CRYPT_H

#ifdef _WIN32
#include <windows.h>
#endif
#include "xorkey.h"

namespace TFCCrypt 
{   
	int DecryptS(unsigned char *&pBuffer, int &pBufferSize, unsigned int dwKey = 0xFFFFFFFF); // Decrypts pBuffer and returns true on success and false on failure
	int EncryptS(unsigned char *&pBuffer, int &pBufferSize, unsigned int dwKey = 0xFFFFFFFF); // Encrypts pBuffer and returns the seed used on encryption
	unsigned long DecryptS2(unsigned char *&pBuffer, int &pBufferSize,unsigned int dwKey = 0xFFFFFFFF);
}

#endif // T4C_CRYPTMESTOPH_CRYPT_H