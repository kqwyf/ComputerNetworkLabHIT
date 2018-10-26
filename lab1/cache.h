#ifndef CACHE_H
#define CACHE_H

#include "httpMessage.h"

#define CACHE_LIFETIME 60 // a cached record should be updated after 2 minutes since last updated
#define CACHE_TTL 3 // a cached record should be updated after used 3 times

#define CACHE_OK 0
#define CACHE_DEAD 1
#define CACHE_NOT_FOUND -1

#define BASE 151
#define MOD1 122420729
#define MOD2 69061

int getCachedData(const httpMessage *request, httpMessage *response, char *lastModified);
int cacheData(const httpMessage *request, const char *response, int len, BOOL overwrite);

#endif
