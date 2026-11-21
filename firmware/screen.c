/* NetTerm: the screen RAM's cells. */
#include "hw.h"
#include "nt.h"

void cell(int row, int col, uint8_t ch, uint8_t attr)
{
    SCR[row * COLS + col] = ((uint32_t)attr << 8) | ch;
}

uint32_t cell_get(int row, int col)
{
    return SCR[row * COLS + col];
}

void cell_set_raw(int row, int col, uint32_t v)
{
    SCR[row * COLS + col] = v;
}

void text(int row, int col, const char *s, uint8_t attr)
{
    while (*s && col < COLS)
        cell(row, col++, (uint8_t)*s++, attr);
}

void fill_row(int row, int col0, int col1, uint8_t ch, uint8_t attr)
{
    for (int c = col0; c < col1; c++)
        cell(row, c, ch, attr);
}
