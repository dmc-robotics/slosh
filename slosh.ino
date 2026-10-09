// Slosh: a liquid fuel gauge for robot LiPo power buses.
#include <Arduino_GFX_Library.h>
#include <Wire.h>

#include <atomic>

#include "src/hardware/BoardPins.h"
#include "src/hardware/FuelSensor.h"
#include "src/hardware/MotionSensor.h"
#include "src/slosh/FluidRenderer.h"
#include "src/slosh/FluidSimulation.h"
#include "src/slosh/PerformanceModel.h"
#include "src/slosh/TunedSettings.h"

static_assert(settingsValid(TUNED_SETTINGS), "TunedSettings.h holds settings that would crash the gauge");

// Each step simulates one frame at the tuned rate, so frames are held to that rate.
constexpr uint32_t FRAME_PERIOD = 1e6f / TUNED_SETTINGS.targetFrameRate + 0.5f;  // µs
constexpr uint8_t DISPLAY_BRIGHTNESS = 200;
constexpr uint32_t REPORT_INTERVAL = 1000000;  // µs
constexpr uint32_t I2C_FREQUENCY = 400000;     // Hz; fast mode, since the accelerometer is read every frame

// USB serial. Created here because the plain ESP32S3 Dev Module FQBN leaves "USB CDC On Boot" off.
HWCDC console;

Arduino_DataBus* displayBus = new Arduino_ESP32QSPI(DISPLAY_CHIP_SELECT_PIN, DISPLAY_CLOCK_PIN, DISPLAY_DATA0_PIN,
                                                    DISPLAY_DATA1_PIN, DISPLAY_DATA2_PIN, DISPLAY_DATA3_PIN);
Arduino_CO5300* display = new Arduino_CO5300(displayBus, DISPLAY_RESET_PIN, 0, DISPLAY_SIZE, DISPLAY_SIZE,
                                             DISPLAY_COLUMN_OFFSET, 0, 0, 0);

FluidSimulation simulation;
FluidRenderer renderer;
MotionSensor motionSensor;
FuelSensor fuelSensor;

struct Strip {
  alignas(4) uint16_t pixels[DISPLAY_SIZE * STRIP_HEIGHT];  // DMA reads it from here
  int row;
  int rows;
};
Strip strips[STRIP_COUNT];
QueueHandle_t freeStrips, renderedStrips;
std::atomic<uint32_t> transferTime{0};

uint32_t previousFrameTime = 0;

// Per-stage timings, reported as key:value lines that Gremlin can plot.
struct FrameTimings {
  uint32_t frames = 0;
  uint32_t simulation = 0;
  uint32_t render = 0;
  uint32_t displayWait = 0;  // core 1 waiting for a strip the display has finished with
  uint32_t windowStart = 0;
} timings;

void setup() {
  console.begin(115200);
  Wire.begin(I2C_DATA_PIN, I2C_CLOCK_PIN, I2C_FREQUENCY);

  if (!display->begin(DISPLAY_BUS_FREQUENCY)) console.println("ERROR:display failed to start");
  display->fillScreen(RGB565_BLACK);
  display->setBrightness(DISPLAY_BRIGHTNESS);

  if (!motionSensor.begin(Wire)) console.println("ERROR:QMI8658 not found; using fixed gravity");

  PerformanceEstimate estimate = estimatePerformance(TUNED_SETTINGS, DISPLAY_SIZE);
  if (!estimate.withinMemoryBudget) {
    // Allocating would abort and reboot in a loop; stay up so the message can be read.
    while (true) {
      console.printf("ERROR:settings need about %d bytes of heap, over the %d byte budget\n", estimate.memory, MEMORY_BUDGET);
      delay(1000);
    }
  }

  fuelSensor.begin();
  simulation.setFillLevel(fuelSensor.fillLevel());
  simulation.configure(TUNED_SETTINGS);
  renderer.configure(TUNED_SETTINGS, DISPLAY_SIZE);

  console.printf("INFO:%d particles at full capacity, %u bytes of heap free\n", simulation.particleCapacity(),
                 ESP.getFreeHeap());
  freeStrips = xQueueCreate(STRIP_COUNT, sizeof(Strip*));
  renderedStrips = xQueueCreate(STRIP_COUNT, sizeof(Strip*));
  for (Strip& strip : strips) {
    Strip* free = &strip;
    xQueueSend(freeStrips, &free, 0);
  }
  // From here on only this task touches the display.
  xTaskCreatePinnedToCore(sendStrips, "display", 4096, nullptr, 2, nullptr, 0);

  previousFrameTime = micros();
  timings.windowStart = previousFrameTime;
}

// Runs on core 0, so each strip's transfer overlaps rendering the next one on core 1.
void sendStrips(void*) {
  while (true) {
    Strip* strip;
    xQueueReceive(renderedStrips, &strip, portMAX_DELAY);
    uint32_t start = micros();
    display->draw16bitBeRGBBitmap(0, strip->row, strip->pixels, DISPLAY_SIZE, strip->rows);
    transferTime += micros() - start;
    xQueueSend(freeStrips, &strip, portMAX_DELAY);
  }
}

void loop() {
  uint32_t sinceLastFrame = micros() - previousFrameTime;
  if (sinceLastFrame < FRAME_PERIOD) {
    uint32_t wait = FRAME_PERIOD - sinceLastFrame;
    delay(wait / 1000);
    delayMicroseconds(wait % 1000);
  }
  uint32_t frameStart = micros();
  float elapsedTime = (frameStart - previousFrameTime) * 1e-6f;
  previousFrameTime = frameStart;

  float accelerationX, accelerationY;
  if (motionSensor.readLiquidAcceleration(accelerationX, accelerationY)) {
    simulation.setGravity(accelerationX, accelerationY);
  }
  fuelSensor.update(elapsedTime);
  simulation.setFillLevel(fuelSensor.fillLevel());

  uint32_t simulationStart = micros();
  simulation.step();
  uint32_t simulationDone = micros();
  renderer.prepare(simulation);
  timings.render += micros() - simulationDone;

  for (int row = 0; row < DISPLAY_SIZE; row += STRIP_HEIGHT) {
    uint32_t waitStart = micros();
    Strip* strip;
    xQueueReceive(freeStrips, &strip, portMAX_DELAY);
    uint32_t renderStart = micros();
    strip->row = row;
    strip->rows = min(STRIP_HEIGHT, DISPLAY_SIZE - row);
    renderer.renderRows(strip->pixels, row, strip->rows);
    timings.displayWait += renderStart - waitStart;
    timings.render += micros() - renderStart;
    xQueueSend(renderedStrips, &strip, portMAX_DELAY);
  }

  timings.simulation += simulationDone - simulationStart;
  timings.frames++;
  reportTimings();
}

void reportTimings() {
  uint32_t now = micros();
  if (now - timings.windowStart < REPORT_INTERVAL) return;
  float frames = timings.frames;
  // transfer_ms runs on core 0 alongside the rest, so the frame takes roughly the larger of
  // transfer_ms and simulation_ms + render_ms + display_wait_ms.
  console.printf(
      "fps:%.1f,simulation_ms:%.2f,render_ms:%.2f,transfer_ms:%.2f,display_wait_ms:%.2f,particles:%d,free_heap:%u,"
      "gravity_x:%.2f,gravity_y:%.2f,bus_voltage:%.2f,fill:%.2f\n",
      frames * 1e6f / (now - timings.windowStart), timings.simulation / frames / 1000.0f, timings.render / frames / 1000.0f,
      transferTime.exchange(0) / frames / 1000.0f, timings.displayWait / frames / 1000.0f, simulation.particleCount(),
      ESP.getFreeHeap(), simulation.gravityX(), simulation.gravityY(), fuelSensor.busVoltage(), fuelSensor.fillLevel());
  timings = FrameTimings{};
  timings.windowStart = now;
}
