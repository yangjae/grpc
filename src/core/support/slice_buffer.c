/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <grpc/support/slice_buffer.h>

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

/* initial allocation size (# of slices) */
#define INITIAL_CAPACITY 4
/* grow a buffer; requires INITIAL_CAPACITY > 1 */
#define GROW(x) (3 * (x) / 2)

void gpr_slice_buffer_init(gpr_slice_buffer *sb) {
  sb->count = 0;
  sb->length = 0;
  sb->capacity = INITIAL_CAPACITY;
  sb->slices = gpr_malloc(sizeof(gpr_slice) * INITIAL_CAPACITY);
}

void gpr_slice_buffer_destroy(gpr_slice_buffer *sb) {
  gpr_slice_buffer_reset_and_unref(sb);
  gpr_free(sb->slices);
}

gpr_uint8 *gpr_slice_buffer_tiny_add(gpr_slice_buffer *sb, unsigned n) {
  gpr_slice *back;
  gpr_uint8 *out;

  sb->length += n;

  if (sb->count == 0) goto add_new;
  back = &sb->slices[sb->count - 1];
  if (back->refcount) goto add_new;
  if ((back->data.inlined.length + n) > sizeof(back->data.inlined.bytes))
    goto add_new;
  out = back->data.inlined.bytes + back->data.inlined.length;
  back->data.inlined.length += n;
  return out;

add_new:
  if (sb->count == sb->capacity) {
    sb->capacity = GROW(sb->capacity);
    GPR_ASSERT(sb->capacity > sb->count);
    sb->slices = gpr_realloc(sb->slices, sb->capacity * sizeof(gpr_slice));
  }
  back = &sb->slices[sb->count];
  sb->count++;
  back->refcount = NULL;
  back->data.inlined.length = n;
  return back->data.inlined.bytes;
}

size_t gpr_slice_buffer_add_indexed(gpr_slice_buffer *sb, gpr_slice s) {
  size_t out = sb->count;
  if (out == sb->capacity) {
    sb->capacity = GROW(sb->capacity);
    GPR_ASSERT(sb->capacity > sb->count);
    sb->slices = gpr_realloc(sb->slices, sb->capacity * sizeof(gpr_slice));
  }
  sb->slices[out] = s;
  sb->length += GPR_SLICE_LENGTH(s);
  sb->count = out + 1;
  return out;
}

void gpr_slice_buffer_add(gpr_slice_buffer *sb, gpr_slice s) {
  size_t n = sb->count;
  /* if both the last slice in the slice buffer and the slice being added
     are inlined (that is, that they carry their data inside the slice data
     structure), and the back slice is not full, then concatenate directly
     into the back slice, preventing many small slices being passed into
     writes */
  if (!s.refcount && n) {
    gpr_slice *back = &sb->slices[n - 1];
    if (!back->refcount && back->data.inlined.length < GPR_SLICE_INLINED_SIZE) {
      if (s.data.inlined.length + back->data.inlined.length <=
          GPR_SLICE_INLINED_SIZE) {
        memcpy(back->data.inlined.bytes + back->data.inlined.length,
               s.data.inlined.bytes, s.data.inlined.length);
        back->data.inlined.length += s.data.inlined.length;
      } else {
        size_t cp1 = GPR_SLICE_INLINED_SIZE - back->data.inlined.length;
        memcpy(back->data.inlined.bytes + back->data.inlined.length,
               s.data.inlined.bytes, cp1);
        back->data.inlined.length = GPR_SLICE_INLINED_SIZE;
        if (n == sb->capacity) {
          sb->capacity = GROW(sb->capacity);
          GPR_ASSERT(sb->capacity > sb->count);
          sb->slices =
              gpr_realloc(sb->slices, sb->capacity * sizeof(gpr_slice));
        }
        back = &sb->slices[n];
        sb->count = n + 1;
        back->refcount = NULL;
        back->data.inlined.length = s.data.inlined.length - cp1;
        memcpy(back->data.inlined.bytes, s.data.inlined.bytes + cp1,
               s.data.inlined.length - cp1);
      }
      sb->length += s.data.inlined.length;
      return; /* early out */
    }
  }
  gpr_slice_buffer_add_indexed(sb, s);
}

void gpr_slice_buffer_addn(gpr_slice_buffer *sb, gpr_slice *s, size_t n) {
  size_t i;
  for (i = 0; i < n; i++) {
    gpr_slice_buffer_add(sb, s[i]);
  }
}

void gpr_slice_buffer_pop(gpr_slice_buffer *sb) {
  if (sb->count != 0) {
    size_t count = --sb->count;
    sb->length -= GPR_SLICE_LENGTH(sb->slices[count]);
  }
}

void gpr_slice_buffer_reset_and_unref(gpr_slice_buffer *sb) {
  size_t i;

  for (i = 0; i < sb->count; i++) {
    gpr_slice_unref(sb->slices[i]);
  }

  sb->count = 0;
  sb->length = 0;
}
