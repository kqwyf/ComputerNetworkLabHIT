#include <stdio.h>
#include <io.h>
#include <string.h>
#include <stdlib.h>
#include "cache.h"

#define CACHE_DIR "cached\\"
#define CACHE_DIR_LEN sizeof(CACHE_DIR)

void hash(const char *str, char *result);
int readAll(char *buf, int size, FILE *f);

int getCachedData(const char *firstline, httpMessage *response, char *lastModified) {
    int err = 0;
    // find and read the cache
    char filename[PATH_LEN];
    char filepath[PATH_LEN] = CACHE_DIR;
    //hash(firstline, filename);
    strcat(filepath, filename);
    if(_access(filepath, 0) != 0) return CACHE_NOT_FOUND;
    FILE *f = fopen(filepath, "r");
    char *buf = (char*)malloc(sizeof(char)*BUFSIZE);
    int len = readAll(buf, BUFSIZE, f);
    err = parseHttpMessage(buf, len, response);
    free(buf);
    // check if the cache is valid
    if(err!=PARSE_OK && err!=PARSE_HOST_NOT_FOUND) return CACHE_NOT_FOUND;
    const char *srcLastModified = getValueHandle(response, "Last-Modified");
    if(srcLastModified) {
        if(lastModified)
            strcpy(lastModified, srcLastModified);
        // TODO: if it is long after last modified, return CACHE_DEAD
    } else {
        return CACHE_NOT_FOUND;
    }
    return CACHE_OK;
}

void cacheData(const httpMessage *message) {
}

int readAll(char *buf, int size, FILE *f) {
    buf[0] = '\0';
    int len = 0;
    while(!feof(f) && len<size) {
        fgets(buf+len, size-len, f);
        while(buf[len]) len++;
    }
    return len;
}
