#include "vevor_bus.h"
#include "driver/rmt.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <functional>

static const char *TAG = "vevor";

#define TX_PIN GPIO_NUM_17
#define RX_PIN GPIO_NUM_16

// ---------------- TX ----------------
#define RMT_TX_CHANNEL RMT_CHANNEL_0
#define RMT_CLK_DIV    80   // 1us per tick
#define BIT_ONE_LOW_US 4000
#define BIT_ZERO_LOW_US 8000
#define BIT_PERIOD_US 12100

// ---------------- RX ----------------
#define RMT_RX_CHANNEL RMT_CHANNEL_1
#define RMT_RX_BUF_SIZE 64

#define PRE_LOW_1MS_MIN       800
#define PRE_LOW_1MS_MAX      3000

#define START_HIGH_30MS_MIN 28000
#define START_HIGH_30MS_MAX 32000

static std::function<void(uint8_t, uint16_t)> vevor_on_byte_received = nullptr;

void vevor_set_receive_callback(std::function<void(uint8_t, uint16_t)> cb) {
  vevor_on_byte_received = cb;
}

// ------------------------------------------------ TX
void vevor_send_byte(uint8_t data) {
  rmt_item32_t items[10];

  items[0].level0 = 1; items[0].duration0 = 10000;
  items[0].level1 = 1; items[0].duration1 = 10000;

  items[1].level0 = 1; items[1].duration0 = 5000;
  items[1].level1 = 1; items[1].duration1 = 5000;

  for (int i = 0; i < 8; i++) {
    bool bit = data & (1 << (7 - i));
    uint32_t low = bit ? BIT_ONE_LOW_US : BIT_ZERO_LOW_US;
    uint32_t high = BIT_PERIOD_US - low;

    items[i + 2].level0 = 0;
    items[i + 2].duration0 = low;
    items[i + 2].level1 = 1;
    items[i + 2].duration1 = high;
  }

  rmt_write_items(RMT_TX_CHANNEL, items, 10, true);
}

// ------------------------------------------------ RX
void IRAM_ATTR vevor_rx_task(void* arg) {
  RingbufHandle_t rb = (RingbufHandle_t)arg;
  size_t rx_size;

  bool got_8bit = false;
  bool got_16bit = false;
  int64_t last_activity_us = 0;

  bool waiting_for_start = true;
  bool saw_1ms_low = false;

  int bit_count = 0;
  int expected_bits = 8;
  uint16_t value = 0;

  while (true) {
    uint32_t* rx_items =
      (uint32_t*)xRingbufferReceive(rb, &rx_size, portMAX_DELAY);
    if (!rx_items) continue;

    int num_items = rx_size / sizeof(rmt_item32_t);

    for (int i = 0; i < num_items; i++) {
      int64_t now_us = esp_timer_get_time();

      // Reset transaction after long idle
      if (now_us - last_activity_us > 80000) { // 80 ms silence
        got_8bit = false;
        got_16bit = false;
      }

      rmt_item32_t item = ((rmt_item32_t*)rx_items)[i];

      bool level0 = item.level0;
      uint32_t dur0 = item.duration0;
      bool level1 = item.level1;
      uint32_t dur1 = item.duration1;

      // -------- 1ms LOW pre-pulse (ONLY while waiting for start) --------
      if (waiting_for_start &&
          ((level0 == 0 && dur0 > PRE_LOW_1MS_MIN && dur0 < PRE_LOW_1MS_MAX) ||  (level1 == 0 && dur1 > PRE_LOW_1MS_MIN && dur1 < PRE_LOW_1MS_MAX))) {
        saw_1ms_low = true;
        ESP_LOGD(TAG, "Got 1-3ms LOW pre-pulse");
        continue;
      }

      // -------- 30ms HIGH start pulse --------
      if ((level0 == 1 && dur0 > START_HIGH_30MS_MIN && dur0 < START_HIGH_30MS_MAX) ||  (level1 == 1 && dur1 > START_HIGH_30MS_MIN && dur1 < START_HIGH_30MS_MAX)) {
        expected_bits = saw_1ms_low ? 16 : 8;
        saw_1ms_low = false;

        ESP_LOGD(TAG, "30ms start → %d-bit frame", expected_bits);

        waiting_for_start = false;
        bit_count = 0;
        value = 0;
      }

      // -------- Ignore until frame start --------
      if (waiting_for_start) {
        continue;
      }

      // -------- Bit decoding --------
      uint32_t low_us = 0;
      if (level0 == 0) low_us = dur0;
      else if (level1 == 0) low_us = dur1;
      else continue;

      uint32_t threshold = (BIT_ONE_LOW_US + BIT_ZERO_LOW_US) / 2;
      bool bit = (low_us < threshold);

      value = (value << 1) | (bit ? 1 : 0);
      bit_count++;

      if (bit_count == expected_bits) {
        last_activity_us = esp_timer_get_time();

        if (expected_bits == 8 && !got_8bit) {
          uint8_t b = value & 0xFF;
          ESP_LOGI(TAG, "Received byte: 0x%02X", b);

          if (vevor_on_byte_received) {
            vevor_on_byte_received(b, 0);
          }

          got_8bit = true;
        }
        else if (expected_bits == 16 && got_8bit && !got_16bit) {
          ESP_LOGI(TAG, "Received 16-bit value: 0x%04X", value);
          if (vevor_on_byte_received) {
            vevor_on_byte_received(0, value);
          }
          got_16bit = true;
        }

        waiting_for_start = true;
        bit_count = 0;
        value = 0;
      }
    }

    vRingbufferReturnItem(rb, (void*)rx_items);
  }
}

// ------------------------------------------------ INIT
void vevor_init() {
  ESP_LOGI(TAG, "Initializing VevorBus RMT...");

  gpio_reset_pin(TX_PIN);
  gpio_set_direction(TX_PIN, GPIO_MODE_OUTPUT);
  gpio_set_level(TX_PIN, 0);

  rmt_driver_uninstall(RMT_TX_CHANNEL);
  rmt_driver_uninstall(RMT_RX_CHANNEL);

  rmt_config_t tx_config = {};
  tx_config.rmt_mode = RMT_MODE_TX;
  tx_config.channel = RMT_TX_CHANNEL;
  tx_config.gpio_num = TX_PIN;
  tx_config.mem_block_num = 1;
  tx_config.clk_div = RMT_CLK_DIV;
  tx_config.tx_config.idle_output_en = true;
  tx_config.tx_config.idle_level = RMT_IDLE_LEVEL_LOW;
  rmt_config(&tx_config);
  rmt_driver_install(RMT_TX_CHANNEL, 0, 0);

  rmt_config_t rx_config = {};
  rx_config.rmt_mode = RMT_MODE_RX;
  rx_config.channel = RMT_RX_CHANNEL;
  rx_config.gpio_num = RX_PIN;
  rx_config.mem_block_num = 1;
  rx_config.clk_div = RMT_CLK_DIV;
  rx_config.rx_config.filter_en = true;
  rx_config.rx_config.filter_ticks_thresh = 100;
  rx_config.rx_config.idle_threshold = 60000; // MUST be > 30ms
  rmt_config(&rx_config);
  rmt_driver_install(RMT_RX_CHANNEL,
                     RMT_RX_BUF_SIZE * sizeof(rmt_item32_t), 0);

  RingbufHandle_t rb = nullptr;
  rmt_get_ringbuf_handle(RMT_RX_CHANNEL, &rb);
  rmt_rx_start(RMT_RX_CHANNEL, true);

  xTaskCreatePinnedToCore(
    vevor_rx_task, "vevor_rx_task", 4096, rb, 10, nullptr, 0);

  ESP_LOGI(TAG, "VevorBus initialized");
}

// ------------------------------------------------ ESPHome wrapper
class VevorBusComponent : public esphome::Component {
 public:
  void setup() override {
    vevor_init();
  }
};

static VevorBusComponent *vevor_bus_component =
  new VevorBusComponent();
