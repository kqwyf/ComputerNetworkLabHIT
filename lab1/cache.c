#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "cache.h"

#define CACHE_DIR "cached\\"
#define CACHE_DIR_LEN sizeof(CACHE_DIR)

/*
 * Cache strategy:
 * A cached record is deprecated if either its time-to-live reaches 0 or (nowTime-lastUpdated) > CACHE_LIFETIME.
 */

typedef struct cacheInfo {
    time_t lastUpdated;
    int ttl;
} cacheInfo;

BOOL cacheFilter(const httpMessage *request);
void hash(const char *str, char *result);
unsigned int _hash(const char *str, unsigned int mod);
int getFilepath(const httpMessage *request, char *filepath);
int readAll(cacheInfo *info, char *buf, int size, FILE *f);
void writeCacheFile(FILE *f, const cacheInfo *info, const char *response, int len);

/*
 * Get the cached data as an http response for a request.
 */
int getCachedData(const httpMessage *request, httpMessage *response, char *lastModified) {
    if(!cacheFilter(request)) return CACHE_NOT_FOUND;
    int err = 0;

    // find the cache file
    char filepath[PATH_LEN];
    getFilepath(request, filepath);
    FILE *f = fopen(filepath, "rb");
    if(f == NULL) return CACHE_NOT_FOUND;

    // read the cache file
    cacheInfo info;
    char *buf = (char*)malloc(sizeof(char)*BUFSIZE);
    int len = readAll(&info, buf, BUFSIZE, f);
    fclose(f);
    if(len == -1) {
        free(buf);
        return CACHE_NOT_FOUND;
    }

    // check if the cache is valid
    if(time(0) - info.lastUpdated > CACHE_LIFETIME || info.ttl <= 0) {
        free(buf);
        const char *lastModifiedValue = getValueHandle(response, "Last-Modified");
        if(lastModifiedValue == NULL) return CACHE_NOT_FOUND;
        strcpy(lastModified, lastModifiedValue);
        return CACHE_DEAD;
    }
    else {
        err = parseHttpMessage(buf, len, response);
        if(err!=PARSE_OK && err!=PARSE_HOST_NOT_FOUND) {
            free(buf);
            return CACHE_NOT_FOUND;
        }
    }

    // decrement ttl and update cache file
    info.ttl--;
    f = fopen(filepath, "wb");
    writeCacheFile(f, &info, buf, len);
    fclose(f);
    free(buf);
    return CACHE_OK;
}

/*
 * Store the response as a cache file.
 */
int cacheData(const httpMessage *request, const char *response, int len, BOOL overwrite) {
    if(!cacheFilter(request)) return 0;
    // calculate the lastUpdated and ttl
    cacheInfo info;
    info.lastUpdated = time(0);
    info.ttl = CACHE_TTL;
    // write to the cache file
    char filepath[PATH_LEN];
    getFilepath(request, filepath);
    CreateDirectoryA(CACHE_DIR, NULL);
    FILE *f = fopen(filepath, overwrite ? "wb" : "ab");
    if(f == NULL) return -1;
    writeCacheFile(f, overwrite ? &info : NULL, response, len);
    fclose(f);
    return 0;
}

void writeCacheFile(FILE *f, const cacheInfo *info, const char *response, int len) {
    char s[35]; // 35 = 32(long) + 2("\r\n") + 1('\0')
    // write the info
    if(info != NULL) {
        _ltoa(info->lastUpdated, s, 10);
        strcat(s, "\r\n");
        fwrite(s, sizeof(char), strlen(s), f);
        _ltoa(info->ttl, s, 10);
        strcat(s, "\r\n");
        fwrite(s, sizeof(char), strlen(s), f);
    }
    // write the response
    fwrite(response, sizeof(char), len, f);
}

int readAll(cacheInfo *info, char *buf, int size, FILE *f) {
    // read lastUpdate value
    info->lastUpdated = 0;
    while(1) {
        int c = fgetc(f);
        if(c < '0' || c > '9') {
            while(c != EOF && c != '\n') c = fgetc(f);
            break;
        }
        info->lastUpdated = (info->lastUpdated * 10) + c-'0';
    }
    // read time-to-live value
    info->ttl = 0;
    while(1) {
        int c = fgetc(f);
        if(c < '0' || c > '9') {
            while(c != EOF && c != '\n') c = fgetc(f);
            break;
        }
        info->ttl = (info->ttl * 10) + c-'0';
    }
    // read cached data
    int len = 0;
    for(int c = fgetc(f); c != EOF; c = fgetc(f))
        buf[len++] = c;
    return len;
}

/*
 * Get the file path of the cached response of a request.
 */
int getFilepath(const httpMessage *request, char *filepath) {
    char filename[PATH_LEN];
    hash(REQUEST_URL(request), filename);
    strcpy(filepath, CACHE_DIR);
    strcat(filepath, filename);
    return strlen(filepath);
}

/*
 * Judge if the response of a request should be cached.
 */
BOOL cacheFilter(const httpMessage *request) {
    return strcmp(REQUEST_METHOD(request), "GET") == 0;
}

/*
 * Hash a string to a 16-digit hex number as a string.
 */
void hash(const char *str, char *result) {
    const char HEX[17] = "0123456789ABCDEF";
    unsigned int h[2];
    h[0] = _hash(str, MOD1);
    h[1] = _hash(str, MOD2);
    int len = 0;
    for(int i = 0; i < 2; i++) {
        for(unsigned int j = 0; j < 2 * sizeof(unsigned int); j++) {
            result[len++] = HEX[h[i] & 0xF];
            h[i] >>= 4;
        }
    }
    result[len] = '\0';
}

unsigned int _hash(const char *str, unsigned int mod) {
    unsigned int result = 0;
    for(int i = 0; str[i]; i++)
        result = (result * BASE + str[i]) % mod;
    return result;
}
