/* -*- indent-tabs-mode: nil; tab-width: 4; c-basic-offset: 4; -*-

   mask.c for the Openbox window manager
   Copyright (c) 2003-2007   Dana Jansens
   Copyright (c) 2003        Derek Foreman

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   See the COPYING file for a copy of the GNU General Public License.
*/

#include "render.h"
#include "color.h"
#include "mask.h"

#include <immintrin.h>

RrPixmapMask *RrPixmapMaskNew(const RrInstance *inst,
                              gint w, gint h, const gchar *data)
{
    RrPixmapMask *m = g_slice_new(RrPixmapMask);
    m->inst = inst;
    m->width = w;
    m->height = h;
    /* round up to nearest byte */
    m->data = g_memdup2(data, (w + 7) / 8 * h);
    m->mask = XCreateBitmapFromData(RrDisplay(inst), RrRootWindow(inst),
                                    data, w, h);
    return m;
}

void RrPixmapMaskFree(RrPixmapMask *m)
{
    if (m) {
        XFreePixmap(RrDisplay(m->inst), m->mask);
        g_free(m->data);
        g_slice_free(RrPixmapMask, m);
    }
}

void RrPixmapMaskDraw(Pixmap p, const RrTextureMask *m, const RrRect *area)
{
    gint x, y;
    if (m->mask == NULL) return; /* no mask given */

    /* set the clip region */
    x = area->x + (area->width - m->mask->width) / 2;
    y = area->y + (area->height - m->mask->height) / 2;

    if (x < 0) x = 0;
    if (y < 0) y = 0;

    XSetClipMask(RrDisplay(m->mask->inst), RrColorGC(m->color), m->mask->mask);
    XSetClipOrigin(RrDisplay(m->mask->inst), RrColorGC(m->color), x, y);

    /* fill in the clipped region */
    XFillRectangle(RrDisplay(m->mask->inst), p, RrColorGC(m->color), x, y,
                   x + m->mask->width, y + m->mask->height);

    /* unset the clip region */
    XSetClipMask(RrDisplay(m->mask->inst), RrColorGC(m->color), None);
    XSetClipOrigin(RrDisplay(m->mask->inst), RrColorGC(m->color), 0, 0);
}

RrPixmapMask *RrPixmapMaskCopy(const RrPixmapMask *src)
{
    RrPixmapMask *m = g_slice_new(RrPixmapMask);
    m->inst = src->inst;
    m->width = src->width;
    m->height = src->height;

    size_t data_size = (src->width + 7) / 8 * src->height;
#ifdef __SSE2__
    // Align data_size to 16 bytes for SSE2 optimization
    size_t aligned_data_size = (data_size + 15) & ~15;
    m->data = g_malloc(aligned_data_size);
    // Use SSE2 to copy data if available and data size is sufficient
    if (data_size >= 16) {
        __m128i *dest = (__m128i *)m->data;
        const __m128i *src_ptr = (const __m128i *)src->data;
        for (size_t i = 0; i < data_size / 16; ++i) {
            _mm_storeu_si128(dest + i, _mm_loadu_si128(src_ptr + i));
        }
    }
    memcpy((char *)m->data + (data_size / 16) * 16, (const char *)src->data + (data_size / 16) * 16, data_size % 16);
#else
    m->data = g_memdup2(src->data, data_size);
#endif
    m->mask = XCreateBitmapFromData(RrDisplay(m->inst), RrRootWindow(m->inst),
                                    m->data, m->width, m->height);
    return m;
}
