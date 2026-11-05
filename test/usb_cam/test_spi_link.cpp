// Run the real transport against a deterministic fake of the hardware boundary.
#include <array>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "Arduino.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "spi_link.h"
#include "usb_cam/protocol.h"

struct TestSpiDevice { int hz; };
namespace {
TestSpiDevice devices[2];
unsigned added = 0, calls = 0, fail_call = 0;
bool selected = false, acknowledge = true;
int64_t now = 0;
std::vector<int> clocks;
std::vector<uint8_t> sent;
std::array<uint8_t, 128> reply;
}
void pinMode(int, int) {}
void digitalWrite(int pin, int value) { if (pin == 15) selected = !value; }
int digitalRead(int pin) { assert(pin == 3); return !(selected && acknowledge); }
void delayMicroseconds(unsigned delay) { now += delay; }
int64_t esp_timer_get_time() { return now++; }
void* heap_caps_malloc(size_t size, unsigned caps) {
  assert(caps == (MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
  return std::malloc(size);
}
void heap_caps_free(void* data) { std::free(data); }
esp_err_t spi_bus_initialize(int host, const spi_bus_config_t* bus, int dma) {
  assert(host == HSPI_HOST && dma == 1);
  assert(bus->mosi_io_num == 13 && bus->miso_io_num == 2 && bus->sclk_io_num == 14);
  return ESP_OK;
}
esp_err_t spi_bus_free(int) { return ESP_OK; }
esp_err_t spi_bus_remove_device(spi_device_handle_t) { return ESP_OK; }
esp_err_t spi_bus_add_device(int, const spi_device_interface_config_t* config, spi_device_handle_t* out) {
  assert(added < 2 && config->mode == 1 && config->spics_io_num == -1);
  assert(config->flags == SPI_DEVICE_NO_DUMMY);
  devices[added].hz = config->clock_speed_hz;
  *out = &devices[added++];
  return ESP_OK;
}
esp_err_t spi_device_polling_transmit(spi_device_handle_t device, spi_transaction_t* transaction) {
  assert(selected && transaction->length % 8 == 0);
  if (++calls == fail_call) return ESP_FAIL;
  const size_t length = transaction->length / 8;
  const auto* bytes = static_cast<const uint8_t*>(transaction->tx_buffer);
  sent.insert(sent.end(), bytes, bytes + length);
  clocks.push_back(device->hz);
  if (transaction->rx_buffer) {
    assert(length == 128 && transaction->rxlength == 1024);
    const auto* before = static_cast<const uint8_t*>(transaction->rx_buffer);
    for (size_t i = 0; i < reply.size(); ++i) assert(before[i] == 0xa5);
    std::memcpy(transaction->rx_buffer, reply.data(), reply.size());
  } else assert(length <= 4092);
  return ESP_OK;
}
int main() {
  assert(camera_link::begin());
  std::array<uint8_t, 128> header{}, received{};
  std::array<uint8_t, 5000> jpeg{};
  for (size_t i = 0; i < jpeg.size(); ++i) jpeg[i] = uint8_t(i);
  usb_cam::encode_config(usb_cam::Config{}, reply.data());
  assert(camera_link::exchange(header.data(), jpeg.data(), jpeg.size(), received.data()));
  assert(received == reply && !selected);
  assert(clocks == std::vector<int>({1000000, 8000000, 8000000}));
  assert(sent.size() == header.size() + jpeg.size());
  assert(std::memcmp(sent.data() + header.size(), jpeg.data(), jpeg.size()) == 0);
  // Failed transfers and absent READY acknowledgment must always release CS.
  fail_call = calls + 1;
  assert(!camera_link::exchange(header.data(), nullptr, 0, received.data()));
  assert(!selected);
  fail_call = calls + 2;
  assert(!camera_link::exchange(header.data(), jpeg.data(), jpeg.size(), received.data()));
  assert(calls == fail_call && !selected); // No later chunk after a failed write.
  acknowledge = false;
  const unsigned before_timeout = calls;
  assert(!camera_link::exchange(header.data(), nullptr, 0, received.data()));
  assert(calls == before_timeout && !selected);
  puts("SPI transport passed: slow control, fast chunked video, reply copy and failure cleanup.");
}
