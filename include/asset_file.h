#ifndef CHIRKY_ASSET_FILE_H
#define CHIRKY_ASSET_FILE_H
#include "chirky.h"
#include <stdio.h>

/* Existing small text/binary parsers consume immutable preloaded bytes without
   disk I/O. Production hosts preload the game before invoking its init hook. */
struct chirky_file { FILE *stream; const struct chirky_host_api *api; chirky_asset asset; };
static inline struct chirky_file chirky_file_open(const struct chirky_host_api *api,const char *path)
{
    struct chirky_file file={.api=api};
    if(!api || !api->asset_request){file.stream=fopen(path,"rb");return file;}
    file.asset=api->asset_request(api->context,path,CHIRKY_ASSET_BLOB);
    if(api->asset_status(api->context,file.asset)==CHIRKY_ASSET_READY) {
        struct chirky_asset_view view=api->asset_data(api->context,file.asset);
        if(view.data)file.stream=fmemopen((void *)view.data,view.size,"rb");
    }
    if(!file.stream){api->asset_release(api->context,file.asset);file.asset=0;}
    return file;
}
static inline void chirky_file_close(struct chirky_file *file)
{
    if(file->stream)fclose(file->stream);
    if(file->asset)file->api->asset_release(file->api->context,file->asset);
    *file=(struct chirky_file){0};
}
#endif
