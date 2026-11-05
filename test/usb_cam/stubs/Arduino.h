#pragma once
#include <cstddef>
#include <cstring>
#define OUTPUT 1
#define INPUT_PULLDOWN 2
#define HIGH 1
#define LOW 0
void pinMode(int pin, int mode);
void digitalWrite(int pin, int value);
int digitalRead(int pin);
void delayMicroseconds(unsigned delay);
bool psramFound();
struct TestSerial { template<typename... Args> void printf(const char*, Args...) {} };
extern TestSerial Serial;
