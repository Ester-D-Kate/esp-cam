#include "receiver.h"
#include "usb_cam/frame_slots.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "dma_receive.h"
#include "hardware/spi.h"
#include "pico/critical_section.h"
#include "pico/stdlib.h"

namespace {
// SPI0 alternate functions on exposed RP2040-Zero pins; GP8-GP14 stay unused.
spi_inst_t* const kSpi = spi0;
constexpr uint kDataPin = 0;
constexpr uint kSelectPin = 1;
constexpr uint kClockPin = 2;
constexpr uint kReadyPin = 3;
constexpr uint kReturnPin = 7; // SPI0 TX; GP3 remains the separate READY signal.
constexpr uint32_t kTransactionTimeoutUs = 250000;
constexpr size_t kDmaCapacity = usb_cam::kPacketCapacity + 1;
ReceivedFrame frames[usb_cam::FrameSlots::kCount];
usb_cam::FrameSlots slots;
critical_section_t mutex;
ReceiverStats stats;
usb_cam::Config desired;

class Lock {
 public:
  Lock() { critical_section_enter_blocking(&mutex); }
  ~Lock() { critical_section_exit(&mutex); }
};

void clear_spi() {
  // Reset also discards any partial byte left by an interrupted master.
  spi_deinit(kSpi);
  spi_init(kSpi, usb_cam::kSpiHz);
  spi_set_format(kSpi, 8, SPI_CPOL_0, SPI_CPHA_1, SPI_MSB_FIRST);
  spi_set_slave(kSpi, true);
  spi_get_hw(kSpi)->dmacr = SPI_SSPDMACR_RXDMAE_BITS | SPI_SSPDMACR_TXDMAE_BITS;
}

int reserve() {
  Lock lock;
  bool replaced = false;
  const int slot = slots.begin_receive(replaced);
  if (replaced) ++stats.replaced;
  return slot;
}

void validate(int slot) {
  ReceivedFrame& frame = frames[slot];
  {
    Lock lock;
    slots.received(slot);
    const int validating = slots.begin_validate();
    assert(validating == slot);  // This single producer validates each arrival.
    (void)validating;
  }
  const bool header_valid = usb_cam::decode_header(frame.packet, frame.length, frame.header);
  const bool length_valid = header_valid && frame.length == usb_cam::kHeaderSize + frame.header.payload_size;
  const bool valid = length_valid &&
      (frame.header.kind == usb_cam::PacketKind::Status ||
       usb_cam::validate_payload(frame.header, frame.packet + usb_cam::kHeaderSize, frame.header.payload_size));
  Lock lock;
  if (!valid) {
    ++stats.invalid;
    if (!header_valid) ++stats.header_errors;
    else if (!length_valid) ++stats.length_errors;
    else ++stats.payload_errors;
    slots.release(slot);
    return;
  }
  stats.sender = frame.header;
  if (frame.header.kind == usb_cam::PacketKind::Status) {
    ++stats.status_packets;
    slots.release(slot);
    return;
  }
  if (stats.frames) {
    const uint32_t delta = frame.header.sequence - stats.last_sequence;
    if (delta > 0x80000000u) ++stats.sender_resets;
    else if (delta > 1) stats.sequence_gaps += delta - 1;
  }
  stats.last_sequence = frame.header.sequence;
  ++stats.frames;
  if (frame.header.payload_size > stats.max_bytes) stats.max_bytes = frame.header.payload_size;
  slots.publish(slot);
}
}  // namespace

void receiver_init() { critical_section_init(&mutex); }

void receiver_run() {
  gpio_init(kReadyPin);
  gpio_put(kReadyPin, false);
  gpio_set_dir(kReadyPin, GPIO_OUT);
  gpio_set_function(kDataPin, GPIO_FUNC_SPI);
  gpio_set_function(kSelectPin, GPIO_FUNC_SPI);
  gpio_set_function(kClockPin, GPIO_FUNC_SPI);
  gpio_set_function(kReturnPin, GPIO_FUNC_SPI);
  gpio_pull_up(kSelectPin);
  gpio_pull_down(kClockPin);
  const uint32_t peripheral_hz = clock_get_hz(clk_peri);
  {
    Lock lock;
    stats.peripheral_hz = peripheral_hz;
  }
  // RP2040 datasheet: SPI slave clock <= clk_peri / 12.
  if (peripheral_hz < usb_cam::kSpiHz * 12) {
    for (;;) tight_loop_contents();  // READY remains low; CDC reports the clock.
  }
  const int dma = dma_claim_unused_channel(true);
  const int tx_dma = dma_claim_unused_channel(true);
  dma_channel_config tx_config = dma_channel_get_default_config(tx_dma);
  channel_config_set_transfer_data_size(&tx_config, DMA_SIZE_8);
  channel_config_set_read_increment(&tx_config, true);
  channel_config_set_write_increment(&tx_config, false);
  channel_config_set_dreq(&tx_config, spi_get_dreq(kSpi, true));
  alignas(4) uint8_t response[usb_cam::kControlSize];
  dma_channel_config config = dma_channel_get_default_config(dma);
  channel_config_set_transfer_data_size(&config, DMA_SIZE_8);
  channel_config_set_read_increment(&config, false);
  channel_config_set_write_increment(&config, true);
  channel_config_set_dreq(&config, spi_get_dreq(kSpi, false));
  for (;;) {
    // Never acknowledge an old/incomplete transaction, including at power-up.
    while (!gpio_get(kSelectPin)) tight_loop_contents();
    const int slot = reserve();
    if (slot < 0) { sleep_us(10); continue; }
    ReceivedFrame& frame = frames[slot];
    usb_cam::Config snapshot;
    { Lock lock; snapshot = desired; }
    usb_cam::encode_config(snapshot, response);
    clear_spi();
    dma_channel_configure(dma, &config, frame.packet, &spi_get_hw(kSpi)->dr, kDmaCapacity, true);
    dma_channel_configure(tx_dma, &tx_config, &spi_get_hw(kSpi)->dr, response, sizeof(response), true);
    gpio_put(kReadyPin, true);
    // Dedicated core 1 waits for the master; core 0 continues servicing USB.
    while (gpio_get(kSelectPin)) tight_loop_contents();
    gpio_put(kReadyPin, false);  // Acknowledge only after RX DMA has been armed.
    const uint32_t started = time_us_32();
    while (!gpio_get(kSelectPin) && uint32_t(time_us_32() - started) < kTransactionTimeoutUs) {
      tight_loop_contents();
    }
    const bool timed_out = !gpio_get(kSelectPin);
    spi_get_hw(kSpi)->dmacr = 0;
    dma_channel_abort(tx_dma); // Response storage stays owned until pending reads finish.
    // No DMA IRQ is enabled (including RP2040-E13). Abort clears TRANS_COUNT;
    // count bytes using the completed destination address instead.
    size_t count = usb_cam_finish_dma(dma, frame.packet, kDmaCapacity);
    bool overflow = (spi_get_hw(kSpi)->ris & SPI_SSPRIS_RORRIS_BITS) != 0;
    // With CS high no further clocks arrive. Account for any residual FIFO bytes
    // after stopping DMA, with bounded storage and explicit overflow detection.
    for (unsigned i = 0; i < 8 && spi_is_readable(kSpi); ++i) {
      const uint8_t byte = uint8_t(spi_get_hw(kSpi)->dr);
      if (count < kDmaCapacity) frame.packet[count++] = byte;
      else overflow = true;
    }
    if (spi_is_readable(kSpi) || count > usb_cam::kPacketCapacity) overflow = true;
    frame.length = count;
    frame.received_us = time_us_32();
    {
      Lock lock;
      stats.last_rx_bytes = count;
      for (size_t i = 0; i < sizeof(stats.last_prefix); ++i) {
        stats.last_prefix[i] = i < count ? frame.packet[i] : 0;
      }
    }
    if (timed_out || overflow) {
      Lock lock;
      if (timed_out) ++stats.timeouts;
      else {
        ++stats.invalid;
        ++stats.overflows;
      }
      slots.release(slot);
    } else {
      validate(slot);
    }
    // On timeout CS may remain low. The top-of-loop gate keeps READY low until
    // the sender releases it; clear_spi then resets the receive shift register.
  }
}

ReceivedFrame* receiver_take_latest() {
  Lock lock;
  const int slot = slots.take_latest(&stats.replaced);
  return slot < 0 ? nullptr : &frames[slot];
}

void receiver_release(ReceivedFrame* frame) {
  Lock lock;
  slots.release(int(frame - frames));
}

ReceiverStats receiver_stats() { Lock lock; return stats; }

void receiver_configure(const usb_cam::Config& config) {
  if (!usb_cam::valid_config(config)) return;
  Lock lock;
  desired = config;
}
