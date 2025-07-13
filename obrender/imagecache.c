/* -*- indent-tabs-mode: nil; tab-width: 4; c-basic-offset: 4; -*-

   imagecache.c for the Openbox window manager
   Copyright (c) 2008        Dana Jansens

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
#include "imagecache.h"
#include "image.h"

#include <immintrin.h>

static gboolean RrImagePicEqual(const RrImagePic *p1,
                                const RrImagePic *p2);

RrImageCache* RrImageCacheNew(gint max_resized_saved)
{
    RrImageCache *self;

    g_assert(max_resized_saved >= 0);

    self = g_slice_new(RrImageCache);
    self->ref = 1;
    self->max_resized_saved = max_resized_saved;
    self->pic_table = g_hash_table_new((GHashFunc)RrImagePicHash,
                                       (GEqualFunc)RrImagePicEqual);
    self->name_table = g_hash_table_new(g_str_hash, g_str_equal);
    return self;
}

void RrImageCacheRef(RrImageCache *self)
{
    ++self->ref;
}

void RrImageCacheUnref(RrImageCache *self)
{
    if (self && --self->ref == 0) {
        g_assert(g_hash_table_size(self->pic_table) == 0);
        g_hash_table_unref(self->pic_table);
        self->pic_table = NULL;

        g_assert(g_hash_table_size(self->name_table) == 0);
        g_hash_table_destroy(self->name_table);
        self->name_table = NULL;

        g_slice_free(RrImageCache, self);
    }
}

#define hashsize(n) ((RrPixel32)1<<(n))
#define hashmask(n) (hashsize(n)-1)
#define rot(x,k) (((x)<<(k)) | ((x)>>(32-(k))))
/* mix -- mix 3 32-bit values reversibly. */
#define mix(a,b,c) \
{ \
  a -= c;  a ^= rot(c, 4);  c += b; \
  b -= a;  b ^= rot(a, 6);  a += c; \
  c -= b;  c ^= rot(b, 8);  b += a; \
  a -= c;  a ^= rot(c,16);  c += b; \
  b -= a;  b ^= rot(a,19);  a += c; \
  c -= b;  c ^= rot(b, 4);  b += a; \
}
/* final -- final mixing of 3 32-bit values (a,b,c) into c */
#define final(a,b,c) \
{ \
  c ^= b; c -= rot(b,14); \
  a ^= c; a -= rot(c,11); \
  b ^= a; b -= rot(a,25); \
  c ^= b; c -= rot(b,16); \
  a ^= c; a -= rot(c,4);  \
  b ^= a; b -= rot(a,14); \
  c ^= b; c -= rot(b,24); \
}

/* This is a fast hash function called "CityHash", found here:
   https://github.com/google/cityhash, by Google

   We use a 64-bit hash for better distribution and fewer collisions.
*/

static G_GNUC_UNUSED guint64
UNALIGNED_LOAD64 (const gchar *p)
{
  guint64 result;
  memcpy (&result, p, sizeof (result));
  return result;
}

static G_GNUC_UNUSED guint32
UNALIGNED_LOAD32 (const gchar *p)
{
  guint32 result;
  memcpy (&result, p, sizeof (result));
  return result;
}

static G_GNUC_UNUSED guint64
Rotate (guint64 val, int shift)
{
  return shift == 0 ? val : (val >> shift) | (val << (64 - shift));
}

static G_GNUC_UNUSED guint64
ShiftMix (guint64 val)
{
  return val ^ (val >> 47);
}

static G_GNUC_UNUSED guint64
Hash128to64 (const guint64 u, const guint64 v)
{
  /* Murmur-inspired hashing. */
  const guint64 kMul = 0x9ddfea08eb382d69ULL;
  guint64 a = (u ^ v) * kMul;
  a ^= (a >> 47);
  guint64 b = (v ^ a) * kMul;
  b ^= (b >> 47);
  b *= kMul;
  return b;
}

static G_GNUC_UNUSED guint64
CityHash64 (const gchar *s, gsize len)
{
  const guint64 k0 = 0xc3a5c85c97cb3127ULL;
  const guint64 k1 = 0xb492b66fbe98f273ULL;
  const guint64 k2 = 0x9ae16a3b2f90404fULL;
#if defined(__SSE4_1__) || defined(__SSE4_2__) // POPCNT
  const guint64 seed = k2 + len;
  return _mm_crc32_u64 (_mm_crc32_u64 (seed, UNALIGNED_LOAD64 (s)), UNALIGNED_LOAD64 (s + 8));
#else
  const gchar *p = s;
  const gchar *end = s + len;
  guint64 h = k2 + len;

  if (len >= 16)
    {
      guint64 v = UNALIGNED_LOAD64 (p + 8) + k1;
      guint64 w = UNALIGNED_LOAD64 (p) + k0;
      h ^= v;
      h = Rotate (h, 19);
      h = h * k0 + k2;
      h ^= w;
      h = Rotate (h, 18);
      h = h * k1 + k2;

      while (p + 16 <= end)
        {
          guint64 x = UNALIGNED_LOAD64 (p);
          guint64 y = UNALIGNED_LOAD64 (p + 8);
          h += x;
          v += y + x;
          w += x;
          v = Rotate (v, 32);
          h = Rotate (h, 19);
          h = h * k0 + k2;
          p += 16;
        }
    }

  if (len > 0)
    {
      h ^= UNALIGNED_LOAD64 (p);
      h = Rotate (h, 47) * k0;
    }

  h = ShiftMix (h);
  h *= k0;
  h = ShiftMix (h);
  return h;
#endif
}

/*! This is some arbitrary initial value for the hashing function.  It's
  constant so that you get the same result from the same data each time.
*/

guint RrImagePicHash(const RrImagePic *p)
{
    return (guint) CityHash64 ((const gchar *) p->data,
                               p->width * p->height * sizeof (RrPixel32));
}

static gboolean RrImagePicEqual(const RrImagePic *p1,
                                const RrImagePic *p2)
{
    return p1->width == p2->width && p1->height == p2->height &&
        p1->sum == p2->sum;
}
