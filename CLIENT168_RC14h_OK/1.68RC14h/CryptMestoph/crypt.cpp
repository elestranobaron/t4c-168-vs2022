#include "crypt.h"

#include <cstring>

#ifndef _WIN32
#include <cstdlib>
#include <sys/time.h>

static unsigned long GetTickCount() {
    struct timeval tv {};
    gettimeofday(&tv, nullptr);
    return static_cast<unsigned long>(tv.tv_sec * 1000u + static_cast<unsigned>(tv.tv_usec / 1000));
}
#endif

#define HEADER_SIZE						2
#define MAX_PASS						1
#define PASS_BREAK						50
#define MIN_PASS						1
#define TYPE_MASK						0x0004

#if defined(LINUX_PORT)
#include <cstdio>
#include <vector>

#include "network/T4CNetworkDebugLog.h"

namespace {

static void DecryptS_AppendWireHex(char *out, size_t cap, const unsigned char *d, int n)
{
	if (!out || cap < 8) {
		return;
	}
	if (n <= 0) {
		snprintf(out, cap, "(empty)");
		return;
	}
	size_t w = 0;
	const int head = (n > 56) ? 28 : n;
	for (int i = 0; i < head && w + 4 < cap; ++i) {
		w += snprintf(out + w, cap - w, "%02x ", d[i]);
	}
	if (n > 56) {
		w += snprintf(out + w, cap - w, "... ");
		const int tailFrom = n - 28;
		for (int i = tailFrom; i < n && w + 4 < cap; ++i) {
			w += snprintf(out + w, cap - w, "%02x ", d[i]);
		}
	}
}

/** Replay non destructif sur copie : mêmes boucles que DecryptS, logs Matrix si checksum échoue. */
static void DecryptS_LogChecksumFailureReplay(const unsigned char *wireSnap, int nBuf, unsigned int dwKeyParam)
{
	if (!wireSnap || nBuf < HEADER_SIZE + 1) {
		return;
	}

	char hexDump[896];
	DecryptS_AppendWireHex(hexDump, sizeof(hexDump), wireSnap, nBuf);
	const unsigned chkWire = wireSnap[static_cast<size_t>(nBuf) - 1u];

	T4CNetworkDebugLogKind(
	    T4CMatrixLogKind::Warn,
	    "[DecryptS] ECHEC checksum — nBufferLen=%d dwKey=0x%08x hdr_cipher_fil=%02x%02x "
	    "dern.oct_fil(checksum envoyé)=0x%02x — dump(head…tail): %s",
	    nBuf, static_cast<unsigned>(dwKeyParam),
	    wireSnap[0], wireSnap[1],
	    chkWire & 0xFFu,
	    hexDump);

	if (nBuf >= 4) {
		T4CNetworkDebugLogKind(
		    T4CMatrixLogKind::Warn,
		    "[DecryptS] 4 derniers octets fil brut: %02x %02x %02x %02x",
		    wireSnap[nBuf - 4], wireSnap[nBuf - 3], wireSnap[nBuf - 2], wireSnap[nBuf - 1]);
	}

	T4CNetworkDebugLogKind(
	    T4CMatrixLogKind::Warn,
	    "[DecryptS] clés (index i/0x200): XorKey1[0]=0x%02x XorKey2[0]=0x%02x",
	    XorKey1[0], XorKey2[0]);

	std::vector<unsigned char> work(static_cast<size_t>(nBuf));
	std::memcpy(work.data(), wireSnap, static_cast<size_t>(nBuf));

	unsigned char cbChkSum = 0;
	unsigned int chkWide = 0;
	int i = 0;
	for (i = 0; i < HEADER_SIZE; ++i) {
		const unsigned char byte = work[static_cast<size_t>(i)];
		cbChkSum += work[static_cast<size_t>(i)];
		chkWide = (chkWide + static_cast<unsigned int>(byte)) & 0xFFu;
		T4CNetworkDebugLogKind(
		    T4CMatrixLogKind::Warn,
		    "[DecryptS] en-tête checksum i=%d octet_avant_xor=0x%02x k1[i/512]=0x%02x roll_char_après_add=%02x roll_wide8=%02x",
		    i, byte & 0xFFu,
		    XorKey1[i / /*%*/ 0x200],
		    static_cast<unsigned>(cbChkSum),
		    chkWide & 0xFFu);
		work[static_cast<size_t>(i)] ^= XorKey1[i / /*%*/ 0x200];
	}

	unsigned short wVal = 0;
	std::memcpy(&wVal, work.data(), sizeof(wVal));

	int dwOffset = 0;
	if (wVal & TYPE_MASK) {
		dwOffset = 4;
	}

	T4CNetworkDebugLogKind(
	    T4CMatrixLogKind::Warn,
	    "[DecryptS] wVal(déchiffré header)=0x%04x TYPE_MASK=%s dwOffset=%d borne_boucle_corps "
	    "i in [%d … %d) (exclusive) — dernier_index_corps=(%d)",
	    static_cast<unsigned>(wVal),
	    (wVal & TYPE_MASK) ? "oui(+4 trailing seed)" : "non",
	    dwOffset, HEADER_SIZE, nBuf - dwOffset, (nBuf - dwOffset) - 1);

	const int bodyUpper = nBuf - dwOffset;
	for (i = HEADER_SIZE; i < bodyUpper; ++i) {
		const unsigned char byte = work[static_cast<size_t>(i)];
		const unsigned char rollBeforeChar = cbChkSum;

		cbChkSum += work[static_cast<size_t>(i)];
		chkWide = (chkWide + static_cast<unsigned int>(byte)) & 0xFFu;

		const bool milestone = (((i - HEADER_SIZE) % 8) == 0) || (i >= bodyUpper - 4);
		if (milestone) {
			T4CNetworkDebugLogKind(
			    T4CMatrixLogKind::Warn,
			    "[DecryptS] corps checksum i=%d octet_xor_avant=0x%02x k2[i/512]=0x%02x "
			    "roll_char_avant_octet=%02x roll_char_après_octet=%02x roll_wide8=%02x",
			    i, byte & 0xFFu, XorKey2[i / /*%*/ 0x200],
			    static_cast<unsigned>(rollBeforeChar),
			    static_cast<unsigned>(cbChkSum),
			    chkWide & 0xFFu);
		}
		work[static_cast<size_t>(i)] ^= XorKey2[i / /*%*/ 0x200];
	}

	/* EncryptS somme les octets [0 .. n-2] XOR k1 puis pose l’octet n-1 = -somme.
	 * Avec TYPE_MASK, la boucle corps arrête à n-dwOffset-1 ; les dwOffset derniers octets
	 * (seed + checksum) ne sont pas XOR k2 mais doivent entrer dans la somme comme sur le fil. */
	if (dwOffset > 0 && nBuf > HEADER_SIZE + 1) {
		for (i = nBuf - dwOffset; i < nBuf; ++i) {
			const unsigned char byte = work[static_cast<size_t>(i)];
			cbChkSum += byte;
			chkWide = (chkWide + static_cast<unsigned int>(byte)) & 0xFFu;
		}
		T4CNetworkDebugLogKind(
		    T4CMatrixLogKind::Warn,
		    "[DecryptS] queue TYPE_MASK: ajout somme fil indices [%d..%d] (dwOffset=%d) roll_char=%02x",
		    nBuf - dwOffset, nBuf - 1, dwOffset, static_cast<unsigned>(cbChkSum));
	}

	const unsigned rollFinalChar = static_cast<unsigned char>(cbChkSum);

	T4CNetworkDebugLogKind(
	    T4CMatrixLogKind::Warn,
	    "[DecryptS] FIN checksum phase1: résultat roll (unsigned_char)=%02u (≠0 ⇒ return -1) "
	    "| octet dernier_fil=0x%02x pas dans boucle corps si dwOffset=0 | "
	    "(roll_fil + dernier_fil) mod256=%02x (si schéma “total octets paquet ≡0”, à comparer avec serveur)",
	    rollFinalChar,
	    chkWire & 0xFFu,
	    (rollFinalChar + chkWire) & 0xFFu);
}

} /* namespace */

#endif /* LINUX_PORT */

/******************************************************************************/
int TFCCrypt::DecryptS(unsigned char *&pBuffer, int &pBufferSize, unsigned int dwKey)
/******************************************************************************/
{
	if (pBufferSize < HEADER_SIZE + 1) 
	{
		return 0; ///Packet dont have Data
	}

#if defined(LINUX_PORT)
	std::vector<unsigned char> decrypt_s_wire_snap;
	if (pBufferSize > 0 && pBufferSize <= 4096 && pBuffer != nullptr) {
		decrypt_s_wire_snap.assign(pBuffer, pBuffer + static_cast<size_t>(pBufferSize));
	}
#endif
	
	// first valid checksum...
	unsigned char cbChkSum = 0;
	int i;

	for(i=0;i<HEADER_SIZE;i++) 
	{
		cbChkSum += pBuffer[i];
		pBuffer[i] ^= XorKey1[i / /*%*/ 0x200];
	}

	unsigned short wVal;
	memcpy(&wVal,pBuffer,2);

	int dwOffset = 0;
	if(wVal & TYPE_MASK) 
	{
		dwOffset = 4;
	}

	for(i = HEADER_SIZE; i < pBufferSize - dwOffset; i++)
	{
		cbChkSum += pBuffer[i];
		pBuffer[i] ^= XorKey2[i / /*%*/ 0x200];
	}

	/* Même invariant que EncryptS : avec TYPE_MASK, les (dwOffset) derniers octets (dont checksum)
	 * ne passent pas par la boucle corps mais participent à la somme modulo 256. */
	if(dwOffset > 0 && pBufferSize > HEADER_SIZE + 1) {
		for(i = pBufferSize - dwOffset; i < pBufferSize; i++){
			cbChkSum += pBuffer[i];
		}
	}

   if(cbChkSum) 
   {
#if defined(LINUX_PORT)
	   const int snapped = static_cast<int>(decrypt_s_wire_snap.size());
	   if (snapped == pBufferSize && !decrypt_s_wire_snap.empty()) {
		   DecryptS_LogChecksumFailureReplay(decrypt_s_wire_snap.data(), pBufferSize, dwKey);
	   } else {
		   T4CNetworkDebugLogKind(
		       T4CMatrixLogKind::Warn,
		       "[DecryptS] ECHEC checksum — replay impossible snap_len=%d pBufferSize=%d",
		       snapped, pBufferSize);
	   }
#endif
	   return -1; // invalid checksum
   }

   // En fonction de l'id on ajoute le cryptage 1.50
   if(wVal & TYPE_MASK)
   {
	   // need tu uncrypt data.... again
	   unsigned char crypt1 = pBuffer[pBufferSize - 4];
	   unsigned char crypt2 = pBuffer[pBufferSize - 3];
	   unsigned char crypt3 = pBuffer[pBufferSize - 2];
	   unsigned char crypt4 = pBuffer[pBufferSize - 1];
	   unsigned long seedNumber = 0 ^ (crypt3) ^ (crypt2 << 8) ^ (crypt4 << 16) ^ (crypt1 << 24);
	   unsigned char *pBuf = pBuffer + 2;
	   int dwlenght = pBufferSize - 3 - 4;
	   if(!TFCCrypt::DecryptS2(pBuf,dwlenght,seedNumber))
	   {
		   return -1;
	   }
	   pBufferSize -= 4; // remove seed data...
   }
   return 0;
}
/******************************************************************************/
int TFCCrypt::EncryptS(unsigned char *&pBuffer, int &pBufferSize, unsigned int dwKey)
/******************************************************************************/
{
	if (pBufferSize < HEADER_SIZE + 1) 
	{
		return 0; ///Packet dont have Data
	}

	//first calculate checksum...
	unsigned char cbChkSum = 0;
	for(int i = 0; i < pBufferSize - 1; i++)
	{
		pBuffer[i] ^= XorKey1[i / /*%*/ 0x200];
		cbChkSum += pBuffer[i];
	}
	pBuffer[pBufferSize - 1] = 256 - cbChkSum;

	return 0;
}
/******************************************************************************/
unsigned long TFCCrypt::DecryptS2(unsigned char *&pBuffer, int &pBufferSize,unsigned int dwKey)
/******************************************************************************/
{
	if (pBufferSize < 1) 
	{
		return false; // if data dont have at least 1 character, give up.
	}
	
	unsigned char *vBuffer = pBuffer;

	unsigned char crypt1 = (unsigned char)((dwKey >> 24) & 0x0FF);
	unsigned char crypt2 = (unsigned char)((dwKey >> 8 ) & 0x0FF);
	unsigned char crypt3 = (unsigned char)((dwKey      ) & 0x0FF);
	unsigned char crypt4 = (unsigned char)((dwKey >> 16) & 0x0FF);
	unsigned long seedNumber = 0 ^ (crypt3) ^ (crypt2 << 8) ^ (crypt4 << 16) ^ (crypt1 << 24);

	// seedNumber CANT be 0.
	// If someone tries to decrypt using this seed, just pick a random one and give a wrong result.
	if (seedNumber == 0) 
	{
		seedNumber = GetTickCount();
	}
	srand( seedNumber );

	int len = pBufferSize;
	unsigned long key = 0;
	unsigned char keytool[4];
	int reali = 0;
	int i = 0;
	int j = 0;
	unsigned char seed[10];
	unsigned char multiplier[10];
	int nbPasses = MAX_PASS  - ( len / PASS_BREAK );
	if( nbPasses < MIN_PASS ) 
	{
		nbPasses = MIN_PASS;
	}
	int *seedstore = 0;
	int *istore;
	int *jstore;
	seedstore = (int*)malloc( (len * nbPasses ) * sizeof( int ));
	istore = (int*)malloc( (len * nbPasses ) * sizeof( int ));
	jstore = (int*)malloc( (len * nbPasses ) * sizeof( int ));
	
	for(i = 0; i < 10; i++ )
	{
		seed[i] = rand();
		multiplier[i] = rand();
	}
	
	for(i = 0; i < len * nbPasses; i++)
	{
		seedstore[i] = (seed[rand() % 10] * multiplier[ rand() %10]) + rand();
	}

	for(i = 0; i < len * nbPasses; i++ )
	{
		reali = ( i % len );
		jstore[i] = ( rand() % len );
		if( jstore[i] == reali )
		{
			if( reali == 0)
			{
				jstore[i] = 1;
			}
			else
			{
				jstore[i] = 0;
			}
		}
		istore[i] = rand() % 21;
	}

	unsigned char byteswap1, byteswap2, byteswap3, byteswap4, inchar1, inchar2;
	for( i = (len * nbPasses ) - 1; i >= 0; i-- )
	{
		reali = ( i % len );
		j = jstore[i];
		inchar1 = vBuffer[reali];
		inchar2 = vBuffer[j];		
		switch( istore[i] )
		{
			case 0:
			{				
				byteswap1 = inchar1 & 0xF0;
				byteswap2 = inchar1 & 0x0F;
				byteswap3 = inchar2 & 0xF0;
				byteswap4 = inchar2 & 0x0F;
				vBuffer[reali] = byteswap1 | byteswap4;
				vBuffer[j] = byteswap2 | byteswap3;
				break;
			}
			case 1:
			{
				byteswap1 = inchar1 & 0xF0;
				byteswap2 = (inchar1 & 0x0F) << 4;
				byteswap3 = (inchar2 & 0xF0) >> 4;											
				byteswap4 = inchar2 & 0x0F;					
				vBuffer[reali] = byteswap1 | byteswap4;
				vBuffer[j] = byteswap2 | byteswap3;
				break;
			}
			case 2:
			{
				byteswap1 = (inchar1 & 0xF0) >> 4;
				byteswap2 = (inchar1 & 0x0F) << 4;
				byteswap3 = (inchar2 & 0xF0) >> 4;
				byteswap4 = (inchar2 & 0x0F) << 4;
				vBuffer[reali] = byteswap1 | byteswap2;
				vBuffer[j] = byteswap3 | byteswap4;
				break;
			}
			case 3:
			{
				byteswap1 = inchar1 & 0xF0;
				byteswap2 = (inchar1 & 0x0F) << 4;
				byteswap3 = (inchar2 & 0xF0) >> 4;
				byteswap4 = inchar2 & 0x0F;
				vBuffer[reali] = byteswap2 | byteswap3;
				vBuffer[j] = byteswap1 | byteswap4;
				break;
			}
			case 4:
			{
				byteswap1 = (inchar1 & 0xF0) >> 4;
				byteswap2 = (inchar1 & 0x0F) << 4;
				byteswap3 = inchar2 & 0xF0;
				byteswap4 = inchar2 & 0x0F;
				vBuffer[reali] = byteswap2 | byteswap4;
				vBuffer[j] = byteswap3 | byteswap1;
				break;
			}
			case 5:
			{				
				byteswap1 = inchar1 & 0xF0;
				byteswap2 = inchar1 & 0x0F;
				byteswap3 = (inchar2 & 0xF0) >> 4;
				byteswap4 = (inchar2 & 0x0F) << 4;
				vBuffer[reali] = byteswap1 | byteswap3;
				vBuffer[j] = byteswap2 | byteswap4;
				break;
			}
			case 6:
			{
				byteswap1 = (inchar1 & 0xF0) >> 4;
				byteswap2 = (inchar1 & 0x0F) << 4;
				byteswap3 = (inchar2 & 0xF0) >> 4;
				byteswap4 = (inchar2 & 0x0F) << 4;
				vBuffer[reali] = byteswap2 | byteswap3;
				vBuffer[j] = byteswap1 | byteswap4;
				break;
			}
			case 7:
			{
				byteswap1 = inchar1 & 0xF0;
				byteswap2 = (inchar1 & 0x0F) << 4;
				byteswap3 = (inchar2 & 0xF0) >> 4;
				byteswap4 = inchar2 & 0x0F;				
				vBuffer[reali] = byteswap1 | byteswap3;
				vBuffer[j] = byteswap2 | byteswap4;
				break;
			}
			case 8:
			{
				byteswap1 = inchar1 & 0xF0;
				byteswap2 = (inchar1 & 0x0F) << 4;
				byteswap3 = (inchar2 & 0xF0) >> 4;
				byteswap4 = inchar2 & 0x0F;
				vBuffer[reali] = byteswap2 | byteswap4;
				vBuffer[j] = byteswap1 | byteswap3;
				break;
			}
			case 9:
			{
				byteswap1 = (inchar1 & 0xF0) >> 4;
				byteswap2 = (inchar1 & 0x0F) << 4;
				byteswap3 = inchar2 & 0xF0;
				byteswap4 = inchar2 & 0x0F;
				vBuffer[reali] = byteswap1 | byteswap3;
				vBuffer[j] = byteswap2 | byteswap4;
				break;
			}
			case 10:
			{
				byteswap1 = (inchar1 & 0xF0) >> 4;
				byteswap2 = inchar1 & 0x0F;
				byteswap3 = inchar2 & 0xF0;
				byteswap4 = (inchar2 & 0x0F) << 4;
				vBuffer[reali] = byteswap4 | byteswap2;
				vBuffer[j] = byteswap3 | byteswap1;
				break;
			}
			case 11:
			{
				byteswap1 = (inchar1 & 0xF0) >> 4;
				byteswap2 = inchar1 & 0x0F;
				byteswap3 = inchar2 & 0xF0;
				byteswap4 = (inchar2 & 0x0F) << 4;
				vBuffer[reali] = byteswap1 | byteswap4;
				vBuffer[j] = byteswap3 | byteswap2;
				break;
			}
			case 12:
			{
				byteswap1 = (inchar1 & 0xF0) >> 4;
				byteswap2 = (inchar1 & 0x0F) << 4;
				byteswap3 = (inchar2 & 0xF0) >> 4;
				byteswap4 = (inchar2 & 0x0F) << 4;
				vBuffer[reali] = byteswap4 | byteswap3;
				vBuffer[j] = byteswap2 | byteswap1;
				break;
			}
			case 13:
			{
				byteswap1 = inchar1 & 0xF0;
				byteswap2 = inchar1 & 0x0F;
				byteswap3 = inchar2 & 0xF0;
				byteswap4 = inchar2 & 0x0F;
				vBuffer[reali] = byteswap3 | byteswap4;
				vBuffer[j] = byteswap1 | byteswap2;
				break;
			}
			case 14:
			{
				byteswap1 = (inchar1 & 0xF0) >> 4;
				byteswap2 = inchar1 & 0x0F;
				byteswap3 = inchar2 & 0xF0;
				byteswap4 = (inchar2 & 0x0F) << 4;
				vBuffer[reali] = byteswap1 | byteswap3;
				vBuffer[j] = byteswap2 | byteswap4;
				break;
			}
			case 15:
			{
				byteswap1 = inchar1 & 0xF0;
				byteswap2 = inchar1 & 0x0F;
				byteswap3 = inchar2 & 0xF0;
				byteswap4 = inchar2 & 0x0F;
				vBuffer[reali] = byteswap3 | byteswap2;
				vBuffer[j] = byteswap1 | byteswap4;
				break;
			}
			case 16:
			{
				byteswap1 = (inchar1 & 0xF0) >> 4;
				byteswap2 = (inchar1 & 0x0F) << 4;
				byteswap3 = inchar2 & 0xF0 >> 4;
				byteswap4 = inchar2 & 0x0F << 4;
				vBuffer[reali] = byteswap4 | byteswap3;
				vBuffer[j] = byteswap1 | byteswap2;
				break;
			}
			case 17:
			{
				byteswap1 = inchar1 & 0xF0;
				byteswap2 = inchar1 & 0x0F;
				byteswap3 = (inchar2 & 0xF0) >> 4;
				byteswap4 = (inchar2 & 0x0F) << 4;
				vBuffer[reali] = byteswap4 | byteswap2;
				vBuffer[j] = byteswap3 | byteswap1;
				break;
			}
			case 18:
			{
				byteswap1 = (inchar1 & 0xF0) >> 4;
				byteswap2 = (inchar1 & 0x0F) << 4;
				byteswap3 = (inchar2 & 0xF0) >> 4;
				byteswap4 = (inchar2 & 0x0F) << 4;
				vBuffer[reali] = byteswap1 | byteswap4;
				vBuffer[j] = byteswap2 | byteswap3;
				break;
			}
			case 19:
			{
				byteswap1 = inchar1 & 0xF0;
				byteswap2 = inchar1 & 0x0F;
				byteswap3 = (inchar2 & 0xF0) >> 4;
				byteswap4 = (inchar2 & 0x0F) << 4;
				vBuffer[reali] = byteswap3 | byteswap4;
				vBuffer[j] = byteswap2 | byteswap1;
				break;
			}
			case 20:
			{
				byteswap1 = (inchar1 & 0xF0) >> 4;
				byteswap2 = inchar1 & 0x0F;
				byteswap3 = inchar2 & 0xF0;
				byteswap4 = (inchar2 & 0x0F) << 4;
				vBuffer[reali] = byteswap3 | byteswap2;
				vBuffer[j] = byteswap1 | byteswap4;
				break;
			}
		}
	}

	for( i= (len * nbPasses) - 1; i >= 0; i--)
	{
		reali = ( i % len );
		key = seedstore[i];
		memcpy( keytool, &key, 4 );				
		if( reali <= len - 4)
		{
			vBuffer[reali] ^= keytool[0];			
			vBuffer[reali + 1] ^= keytool[1];				
			vBuffer[reali + 2] ^= keytool[2];				
			vBuffer[reali + 3] ^= keytool[3];				
		}
		else if( reali == len - 3)
		{
			vBuffer[reali] ^= keytool[0];			
			vBuffer[reali + 1] ^= keytool[1];				
			vBuffer[reali + 2] ^= keytool[2];					
		}
		else if( reali == len - 2)
		{
			vBuffer[reali] ^= keytool[0];			
			vBuffer[reali + 1] ^= keytool[1];				
		}
		else if( reali == len - 1)
			vBuffer[reali] ^= keytool[0];			
	}

	free( seedstore );
	free( istore);
	free( jstore);
	
	return true;
}
