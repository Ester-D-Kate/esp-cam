#pragma once
#include <cstddef>
bool psramFound();
struct TestSerial { template<typename... Args> void printf(const char*, Args...) {} };
extern TestSerial Serial;
