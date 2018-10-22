#ifndef CACHE_H
#define CACHE_H

#include "httpMessage.h"

#define CACHE_LIFETIME 120 // a cached record should be updated after 2 minutes since last updated

#define CACHE_OK 0
#define CACHE_DEAD 1
#define CACHE_NOT_FOUND -1

int getCachedData(const char *firstline, httpMessage *response, char *lastModified);
void cacheData(const httpMessage *message);

#endif
