#ifndef RECT_RENDERER_H
#define RECT_RENDERER_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define RECT_BATCH_CAPACITY 4096
struct rect_vertex { float x,y; uint8_t r,g,b,a; };
struct rect_renderer {
    unsigned int program, buffer;
    struct rect_vertex *vertices;
    size_t count;
    unsigned int rectangles, batches;
    int width,height;
};
bool rect_renderer_init(struct rect_renderer *r,int width,int height);
void rect_renderer_destroy(struct rect_renderer *r);
void rect_renderer_begin(struct rect_renderer *r);
void rect_renderer_rect(struct rect_renderer *r,int x,int y,int w,int h,uint8_t red,uint8_t green,uint8_t blue);
void rect_renderer_flush(struct rect_renderer *r);
#endif
