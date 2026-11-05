# TinyUSB RP2040 endpoint abort adaptation

`src/iso_restart.h` adapts the endpoint abort sequence in TinyUSB's
[`dcd_rp2040.c`](https://github.com/hathach/tinyusb/blob/91cdc7bbb4d85a3ecb4c8c640297d9c401ff4390/src/portable/raspberrypi/rp2040/dcd_rp2040.c),
adding a bounded wait, explicit pending-state cleanup and controller-reset
fallback for older silicon. The installed SDK's TinyUSB driver and video class
are included by the adapter translation units and retain their original notices.

The MIT License (MIT)

Copyright (c) 2020 Raspberry Pi (Trading) Ltd.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
