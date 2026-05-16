#pragma once

/**
 * Stub TFCCrypt pour Linux — EncryptC / DecryptC en no-op.
 *
 * IMPORTANT — ce que le dépôt contient vraiment :
 * - Le client Windows lie **TFCCrypt.lib** (voir `T4C Client.vcxproj`) : l’implémentation
 *   de **EncryptC / DecryptC / EncryptC2** n’est **pas** fournie en source ici, seulement
 *   `TFCCRYPTC/include/Crypt.h` (et doublons sous `CLIENT168_.../TFCCRYPTC`).
 * - **`CLIENT168_.../CryptMestoph/crypt.cpp`** implémente **EncryptS / DecryptS / DecryptS2**
 *   (XOR tables, autre usage serveur / ancien pipeline). Ce n’est **pas** le même algorithme
 *   que celui appelé par `CommCenter.cpp` sur le chemin client (**EncryptC** après
 *   `GetTickCount()+0xBBAA`, etc.). Le recopier tel quel ne ferait pas parler le serveur
 *   comme le vrai client.
 *
 * Pour un chiffrement filaire identique au client Windows, il faut soit les **sources**
 * TFCCryptC d’origine, soit une **rétro-ingénierie** de `TFCCrypt.lib` (PE), puis un port
 * en C++ avec arithmétique **uint32_t** explicite (éviter `unsigned long` 64 bits sous LP64).
 *
 * Tant que ce stub est actif, le trafic UDP client reste en clair — comportement attendu
 * du stub, pas du binaire Windows officiel.
 */
#if defined(LINUX_PORT) && !defined(_WIN32) && defined(USE_CLIENT_CONNECTION)

#include <cstdint>

#include "network/T4CLinuxCommPort.h"

class TFCCrypt {
   public:
    explicit TFCCrypt(std::uint32_t dwKey) : m_dwKEY(dwKey) {}

    int DecryptC(LPBYTE &pBuffer, int &pBufferSize, unsigned int dwKey = 0xFFFFFFFFu);
    int EncryptC(LPBYTE &pBuffer, int &pBufferSize, unsigned int dwKey = 0xFFFFFFFFu);

   private:
    DWORD EncryptC2(LPBYTE &pBuffer, int &pBufferSize, unsigned int dwKey = 0xFFFFFFFFu);

    std::uint32_t m_dwKEY;
};

#endif /* LINUX + USE_CLIENT_CONNECTION */
