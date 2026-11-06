#pragma once
#include <cstdint>
extern uint64_t test_time_us;
inline uint64_t get_absolute_time(){return test_time_us;}
inline uint64_t to_ms_since_boot(uint64_t t){return t/1000;}
