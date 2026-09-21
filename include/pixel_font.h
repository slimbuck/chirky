#ifndef CHIRKY_PIXEL_FONT_H
#define CHIRKY_PIXEL_FONT_H
#include <stdint.h>
static const uint8_t *glyph(char character)
{
    static const uint8_t alphabet[26][7] = {
        {14,17,17,31,17,17,17},{30,17,17,30,17,17,30},
        {14,17,16,16,16,17,14},{30,17,17,17,17,17,30},
        {31,16,16,30,16,16,31},{31,16,16,30,16,16,16},
        {14,17,16,23,17,17,15},{17,17,17,31,17,17,17},
        {14,4,4,4,4,4,14},{7,2,2,2,18,18,12},
        {17,18,20,24,20,18,17},{16,16,16,16,16,16,31},
        {17,27,21,21,17,17,17},{17,25,21,19,17,17,17},
        {14,17,17,17,17,17,14},{30,17,17,30,16,16,16},
        {14,17,17,17,21,18,13},{30,17,17,30,20,18,17},
        {15,16,16,14,1,1,30},{31,4,4,4,4,4,4},
        {17,17,17,17,17,17,14},{17,17,17,17,17,10,4},
        {17,17,17,21,21,21,10},{17,17,10,4,10,17,17},
        {17,17,10,4,4,4,4},{31,1,2,4,8,16,31}
    };
    static const uint8_t digits[10][7] = {
        {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},
        {14,17,1,2,4,8,31},{30,1,1,14,1,1,30},
        {2,6,10,18,31,2,2},{31,16,16,30,1,1,30},
        {14,16,16,30,17,17,14},{31,1,2,4,8,8,8},
        {14,17,17,14,17,17,14},{14,17,17,15,1,1,14}
    };
    static const uint8_t dash[7] = {0,0,0,31,0,0,0};
    static const uint8_t slash[7] = {1,2,2,4,8,8,16};
    static const uint8_t dot[7] = {0,0,0,0,0,0,4};
    static const uint8_t question[7] = {14,17,1,2,4,0,4};
    static const uint8_t blank[7] = {0,0,0,0,0,0,0};
    if (character >= 'a' && character <= 'z') character -= 32;
    if (character >= 'A' && character <= 'Z') return alphabet[character - 'A'];
    if (character >= '0' && character <= '9') return digits[character - '0'];
    if (character == '-') return dash;
    if (character == '/') return slash;
    if (character == '.') return dot;
    if (character == ' ') return blank;
    return question;
}

#endif
