#pragma once
#include <cstdint>
int64_t esp_timer_get_time();
#include "esp_err.h"
using esp_timer_cb_t = void (*)(void*);
using esp_timer_handle_t = void*;
enum esp_timer_dispatch_t { ESP_TIMER_TASK };
struct esp_timer_create_args_t { esp_timer_cb_t callback; void* arg; esp_timer_dispatch_t dispatch_method; const char* name; bool skip_unhandled_events; };
esp_err_t esp_timer_create(const esp_timer_create_args_t*, esp_timer_handle_t*);
esp_err_t esp_timer_start_periodic(esp_timer_handle_t, uint64_t);
esp_err_t esp_timer_delete(esp_timer_handle_t);
