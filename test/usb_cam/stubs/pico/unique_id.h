#pragma once
#include <stddef.h>
#include <string.h>
#define PICO_UNIQUE_BOARD_ID_SIZE_BYTES 8
static inline void pico_get_unique_board_id_string(char* out, size_t length) {
  const char id[] = "0123456789abcdef";
  if (length >= sizeof(id)) memcpy(out, id, sizeof(id));
}
