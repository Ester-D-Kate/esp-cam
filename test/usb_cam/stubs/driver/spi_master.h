#pragma once
#include <cstddef>
#include <cstdint>
#include "esp_err.h"
constexpr int HSPI_HOST = 1;
constexpr unsigned SPI_DEVICE_NO_DUMMY = 1u << 6;
struct TestSpiDevice;
using spi_device_handle_t = TestSpiDevice*;
struct spi_bus_config_t {
  int mosi_io_num, miso_io_num, sclk_io_num, quadwp_io_num, quadhd_io_num;
  int max_transfer_sz;
};
struct spi_device_interface_config_t {
  unsigned mode;
  int clock_speed_hz, spics_io_num, queue_size;
  unsigned flags;
};
struct spi_transaction_t {
  size_t length, rxlength;
  const void* tx_buffer;
  void* rx_buffer;
};
esp_err_t spi_bus_initialize(int host, const spi_bus_config_t* config, int dma);
esp_err_t spi_bus_free(int host);
esp_err_t spi_bus_add_device(int host, const spi_device_interface_config_t* config, spi_device_handle_t* out);
esp_err_t spi_bus_remove_device(spi_device_handle_t device);
esp_err_t spi_device_polling_transmit(spi_device_handle_t device, spi_transaction_t* transaction);
