#ifndef RECT_RENDERER_H
#define RECT_RENDERER_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define RECT_BATCH_CAPACITY 4096
#define TEXTURE_BATCH_CAPACITY 256
struct rect_vertex { float x,y; uint8_t r,g,b,a; };
struct texture_vertex { float x,y,u,v; uint8_t r,g,b,a; }; /* u/v are source texels */
/* Zero-initialize. Owned by the caller; delete before destroying the renderer.
   Do not copy a live texture or use it with a different GL context. */
struct rect_renderer_texture { unsigned int id; int width,height; };
struct rect_renderer {
    unsigned int program, buffer;
    struct rect_vertex *vertices;
    size_t count;
    unsigned int rectangles, batches;
    int width,height;
    unsigned int texture_program, texture_buffer, batch_texture;
    int texture_size_location, batch_texture_width, batch_texture_height;
    struct texture_vertex *texture_vertices;
    size_t texture_count;
    unsigned int sprites;
};
bool rect_renderer_init(struct rect_renderer *r,int width,int height);
void rect_renderer_destroy(struct rect_renderer *r);
void rect_renderer_begin(struct rect_renderer *r);
void rect_renderer_rect(struct rect_renderer *r,int x,int y,int w,int h,uint8_t red,uint8_t green,uint8_t blue);
void rect_renderer_flush(struct rect_renderer *r);
/* A current GLES2/WebGL1 context is required for create/delete/flush.
   Input is tightly packed, top-down, straight-alpha RGBA8. An empty t stays empty
   on failure; a live t is rejected unchanged. size may exceed width*height*4. */
bool rect_renderer_texture_create(struct rect_renderer *r,struct rect_renderer_texture *t,
                                 int width,int height,const void *rgba,size_t size);
/* Flushes queued uses of t before deleting it; safe on an empty texture. */
void rect_renderer_texture_delete(struct rect_renderer *r,struct rect_renderer_texture *t);
/* Destination uses bottom-left coordinates, source uses top-left pixels.
   Rejects out-of-bounds source regions. Destination is clipped to the viewport.
   Nearest sampling uses integer-edge scaling: source=floor(((p+1)*s-1)/d).
   Flip mirrors that sampled region. White/255 tint preserves the source.
   Straight alpha is composited in submission order, including rectangles. */
void rect_renderer_sprite(struct rect_renderer *r,const struct rect_renderer_texture *t,
                          int x,int y,int width,int height,int sx,int sy,int sw,int sh,
                          uint8_t red,uint8_t green,uint8_t blue,uint8_t alpha,bool flip_x);
/* Clip bounds are bottom-left physical pixels, intersected with the viewport.
   Pass the ORIGINAL destination plus host offset, not a pre-clipped destination.
   Clipping preserves its sampling/flip using geometry, not per-sprite scissor. */
void rect_renderer_sprite_clipped(struct rect_renderer *r,const struct rect_renderer_texture *t,
                                  int x,int y,int width,int height,int sx,int sy,int sw,int sh,
                                  uint8_t red,uint8_t green,uint8_t blue,uint8_t alpha,bool flip_x,
                                  int clip_x,int clip_y,int clip_w,int clip_h);
#endif
