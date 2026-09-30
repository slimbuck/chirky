#include "save_store.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
int main(void)
{
    char root[]="/tmp/chirky-saves-XXXXXX";assert(mkdtemp(root));assert(chdir(root)==0);
    char bytes[32]={0};
    assert(!save_store_read(NULL,"game","best",bytes,sizeof(bytes)));
    assert(save_store_write(NULL,"game","best","ABC",3));
    assert(save_store_read(NULL,"game","best",bytes,sizeof(bytes))==3 && !memcmp(bytes,"ABC",3));
    assert(!save_store_read(NULL,"game","best",bytes,2));
    assert(!save_store_read(NULL,"other-game","best",bytes,sizeof(bytes)));
    assert(!save_store_write(NULL,"../bad","best","BAD",3));
    assert(!save_store_write(NULL,"game","../../best","BAD",3));
    assert(!save_store_write(NULL,"game","best",bytes,65537));
    assert(save_store_write(NULL,"game","best","DEF",3));
    assert(save_store_read(NULL,"game","best",bytes,sizeof(bytes))==3 && !memcmp(bytes,"DEF",3));
    assert(unlink("saves/game/best")==0);assert(rmdir("saves/game")==0);assert(rmdir("saves")==0);
    assert(chdir("/")==0);assert(rmdir(root)==0);
    puts("Save store: persisted replacement, namespace isolation, bounds and invalid paths passed.");
}
