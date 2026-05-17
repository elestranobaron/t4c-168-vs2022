#pragma once

#include <string>

/** Répertoire racine contenant sprites/, maps/, sons/ (sans slash final). */
std::string ResolveT4CDataRoot();

std::string T4CDataPath(const char *subpath);
