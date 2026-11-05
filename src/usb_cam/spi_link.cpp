#include "spi_link.h"
#include <Arduino.h>
#include <driver/spi_master.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include "usb_cam/protocol.h"

namespace camera_link {
namespace {
constexpr int kClockPin = 14;
constexpr int kDataPin = 13;
constexpr int kSelectPin = 15;
constexpr int kReadyPin = 3;
constexpr int kReturnPin = 2;
constexpr size_t kChunkSize = 4092;
spi_device_handle_t device = nullptr;
uint8_t* staging = nullptr;
uint8_t* response = nullptr;

bool write_bytes(const uint8_t* bytes, size_t length) {
  while (length) {
    const size_t count = length < kChunkSize ? length : kChunkSize;
    memcpy(staging, bytes, count);
    spi_transaction_t transaction{};
    transaction.length = count * 8;
    transaction.tx_buffer = staging;
    if (spi_device_polling_transmit(device, &transaction) != ESP_OK) return false;
    bytes += count;
    length -= count;
  }
  return true;
}
}  // namespace

bool begin() {
  pinMode(kSelectPin, OUTPUT);
  digitalWrite(kSelectPin, HIGH);
  pinMode(kReadyPin, INPUT_PULLDOWN);
  staging = static_cast<uint8_t*>(heap_caps_malloc(kChunkSize, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
  if (!staging) return false;
  response = static_cast<uint8_t*>(heap_caps_malloc(usb_cam::kControlSize, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
  if (!response) { heap_caps_free(staging); staging = nullptr; return false; }
  spi_bus_config_t bus{};
  bus.mosi_io_num = kDataPin;
  bus.miso_io_num = kReturnPin;
  bus.sclk_io_num = kClockPin;
  bus.quadwp_io_num = -1;
  bus.quadhd_io_num = -1;
  bus.max_transfer_sz = kChunkSize;
  if (spi_bus_initialize(HSPI_HOST, &bus, 1) != ESP_OK) {
    heap_caps_free(staging);
    heap_caps_free(response); response = nullptr;
    staging = nullptr;
    return false;
  }
  spi_device_interface_config_t config{};
  config.mode = 1;  // RP2040 slave accepts continuous words with CPHA=1.
  config.clock_speed_hz = usb_cam::kSpiHz;
  config.spics_io_num = -1;  // One manual CS encloses header + all JPEG chunks.
  config.queue_size = 1;
  config.flags = SPI_DEVICE_NO_DUMMY;
  if (spi_bus_add_device(HSPI_HOST, &config, &device) != ESP_OK) {
    spi_bus_free(HSPI_HOST);
    heap_caps_free(staging);
    heap_caps_free(response); response = nullptr;
    staging = nullptr;
    return false;
  }
  return true;
}

bool ready() { return digitalRead(kReadyPin) == HIGH; }

bool exchange(const uint8_t* header, const uint8_t* jpeg, size_t length, uint8_t* control) {
  if (!device || !header || !control || length > usb_cam::kMaxJpeg ||
      (length && !jpeg) || !ready()) return false;
  digitalWrite(kSelectPin, LOW);
  // A falling READY acknowledges this CS edge, not just a previous buffer.
  const int64_t deadline = esp_timer_get_time() + 2000;
  while (ready() && esp_timer_get_time() < deadline) delayMicroseconds(2);
  bool success = !ready();
  if (success) {
    memcpy(staging, header, usb_cam::kHeaderSize);
    spi_transaction_t transaction{};
    transaction.length = usb_cam::kHeaderSize * 8;
    transaction.rxlength = usb_cam::kControlSize * 8;
    transaction.tx_buffer = staging; transaction.rx_buffer = response;
    success = spi_device_polling_transmit(device, &transaction) == ESP_OK;
    if (success) {
      memcpy(control, response, usb_cam::kControlSize);
      success = write_bytes(jpeg, length);
    }
  }
  delayMicroseconds(10);  // Let the slave drain the final byte from its RX FIFO.
  digitalWrite(kSelectPin, HIGH);
  delayMicroseconds(100);
  return success;
}
}  // namespace camera_link
