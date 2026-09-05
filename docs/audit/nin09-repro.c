/* Reproduction minimale de NIN-09, extraite telle quelle de legacy/include/libWeb.c. */
#include <stdio.h>
#include <string.h>

#define MD5_SIZE 32
#define MD5_DIGEST_LENGTH 16

/* libWeb.c:125-135, à l'identique */
static void md5Hash_original(const unsigned char *md5, char *hash)
{
    for (int i = 0; i < MD5_DIGEST_LENGTH; i++) {
        sprintf(hash + 2 * i, "%02x", md5[i]);
    }
}

int main(void)
{
    unsigned char digest[MD5_DIGEST_LENGTH];
    for (int i = 0; i < MD5_DIGEST_LENGTH; i++) digest[i] = (unsigned char)(0xA0 + i);

    char secure[MD5_SIZE];          /* libWeb.c:260 — 32 octets */
    md5Hash_original(digest, secure);

    printf("jeton : %.32s\n", secure);
    return 0;
}
