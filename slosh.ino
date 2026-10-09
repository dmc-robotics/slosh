// Slosh: a liquid fuel gauge for robot LiPo power buses.
#include <Arduino_GFX_Library.h>
#include <Wire.h>

#include <atomic>

#include "src/hardware/BoardPins.h"
#include "src/hardware/FuelSensor.h"
#include "src/hardware/MotionSensor.h"
#include "src/slosh/DisplayStrips.h"
#include "src/slosh/FluidRenderer.h"
#include "src/slosh/FluidSimulation.h"
#include "src/slosh/PerformanceModel.h"
#include "src/slosh/TunedSettings.h"

static_assert(settingsValid(TUNED_SETTINGS), "TunedSettings.h holds settings that would crash the gauge");

// Each step simulates one frame at the tuned rate, so frames are held to that rate: by the
// panel's refresh, or by this period if its TE signal is missing.
constexpr int REFRESHES_PER_FRAME = refreshesPerFrame(TUNED_SETTINGS.targetFrameRate);
constexpr uint32_t FRAME_PERIOD = 1e6f / TUNED_SETTINGS.targetFrameRate + 0.5f;  // µs
// TE falls as each refresh starts scanning from the top. A frame's strips go out fast at the top
// and slower through the middle, trailing the scan by -0.3 to +5.7 ms if started with it, so
// starting this much later keeps them between that refresh's scan and the next one's, with about
// 5 ms to spare either way. Then no refresh shows parts of two frames.
constexpr uint32_t FRAME_WRITE_DELAY = 5600;               // µs after a refresh starts
constexpr TickType_t REFRESH_TIMEOUT = pdMS_TO_TICKS(50);  // so a lost TE signal can't freeze the gauge
constexpr uint8_t CO5300_TEARING_EFFECT_ON = 0x35;
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
  int column;
  int columns;
};
Strip strips[STRIP_COUNT];
QueueHandle_t freeStrips, renderedStrips;
TaskHandle_t displayTask;
std::atomic<uint32_t> transferTime{0};

// The latest refresh, counted and timed as TE falls on core 1 and read by the display task on core 0.
portMUX_TYPE refreshLock = portMUX_INITIALIZER_UNLOCKED;
volatile uint32_t refreshCount = 0;
volatile uint32_t refreshStart = 0;  // micros()
bool refreshSync = false;  // frames follow the panel's TE signal rather than FRAME_PERIOD

// Defined before use: the Arduino builder doesn't declare IRAM_ATTR functions ahead.
void IRAM_ATTR onRefreshStart() {
  portENTER_CRITICAL_ISR(&refreshLock);
  refreshStart = micros();
  refreshCount++;
  portEXIT_CRITICAL_ISR(&refreshLock);
  BaseType_t woken = pdFALSE;
  vTaskNotifyGiveFromISR(displayTask, &woken);
  portYIELD_FROM_ISR(woken);
}

uint32_t previousFrameTime = 0;

// Per-stage timings, reported as key:value lines that Gremlin can plot.
struct FrameTimings {
  uint32_t frames = 0;
  uint32_t simulation = 0;
  uint32_t render = 0;
  uint32_t displayWait = 0;  // core 1 waiting for a strip the display has finished with
  uint32_t windowStart = 0;
  uint32_t refreshes = 0;  // refreshCount at windowStart
} timings;

void setup() {
  console.begin(115200);
  Wire.begin(I2C_DATA_PIN, I2C_CLOCK_PIN, I2C_FREQUENCY);

  if (!display->begin(DISPLAY_BUS_FREQUENCY)) console.println("ERROR:display failed to start");
  display->fillScreen(RGB565_BLACK);
  display->setBrightness(DISPLAY_BRIGHTNESS);
  displayBus->beginWrite();
  displayBus->writeC8D8(CO5300_TEARING_EFFECT_ON, 0x00);  // pulse TE during vertical blanking; Arduino_GFX leaves it off
  displayBus->endWrite();

  if (!motionSensor.begin(Wire)) console.println("ERROR:QMI8658 not found; using fixed gravity");

  PerformanceEstimate estimate = estimatePerformance(TUNED_SETTINGS, DISPLAY_SIZE);
  if (!estimate.withinMemoryBudget) {
    // Allocating would abort and reboot in a loop; stay up so the message can be read.
    while (true) {
      console.printf("ERROR:settings need about %d bytes of heap, over the %d byte budget\n", estimate.memory, MEMORY_BUDGET);
      delay(1000);
    }
  }

  fuelSensor.begin(Serial1);
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
  xTaskCreatePinnedToCore(sendStrips, "display", 4096, nullptr, 2, &displayTask, 0);

  pinMode(DISPLAY_TEARING_PIN, INPUT);
  attachInterrupt(DISPLAY_TEARING_PIN, onRefreshStart, FALLING);
  delay(100);
  refreshSync = refreshCount >= 3;
  if (!refreshSync) console.println("ERROR:no TE signal from the display; frames may tear");

  previousFrameTime = micros();
  timings.windowStart = previousFrameTime;
}

// Runs on core 0, so each strip's transfer overlaps rendering the next one on core 1.
void sendStrips(void*) {
  uint32_t shownRefresh = refreshCount;
  while (true) {
    Strip* strip;
    xQueueReceive(renderedStrips, &strip, portMAX_DELAY);
    if (strip->row == 0 && refreshSync) shownRefresh = waitForRefresh(shownRefresh + REFRESHES_PER_FRAME);
    uint32_t start = micros();
    display->draw16bitBeRGBBitmap(strip->column, strip->row, strip->pixels, strip->columns, strip->rows);
    transferTime += micros() - start;
    xQueueSend(freeStrips, &strip, portMAX_DELAY);
  }
}

// Starts the frame FRAME_WRITE_DELAY after a refresh at or after the target one starts. A frame
// ready after its refresh started, but before its writes would begin, still makes that refresh.
// Returns the refresh it started on.
uint32_t waitForRefresh(uint32_t target) {
  while (true) {
    ulTaskNotifyTake(pdTRUE, 0);  // refreshes so far are covered by reading the latest below
    portENTER_CRITICAL(&refreshLock);
    uint32_t refresh = refreshCount;
    uint32_t started = refreshStart;
    portEXIT_CRITICAL(&refreshLock);
    if (static_cast<int32_t>(refresh - target) >= 0) {
      uint32_t sinceStart = micros() - started;
      if (sinceStart < FRAME_WRITE_DELAY) {
        delayMicroseconds(FRAME_WRITE_DELAY - sinceStart);
        return refresh;
      }
      target = refresh + 1;  // too late for this one
    }
    if (ulTaskNotifyTake(pdTRUE, REFRESH_TIMEOUT) == 0) return refresh;  // TE lost: send now
  }
}

void loop() {
  // Synced to the panel, core 1 is paced by waiting for strips the display has finished with.
  uint32_t sinceLastFrame = micros() - previousFrameTime;
  if (!refreshSync && sinceLastFrame < FRAME_PERIOD) {
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
    int rows = min(STRIP_HEIGHT, DISPLAY_SIZE - row);
    ColumnSpan columns = stripColumns(row, rows, DISPLAY_SIZE);
    if (columns.first == columns.end) continue;
    uint32_t waitStart = micros();
    Strip* strip;
    xQueueReceive(freeStrips, &strip, portMAX_DELAY);
    uint32_t renderStart = micros();
    strip->row = row;
    strip->rows = rows;
    strip->column = columns.first;
    strip->columns = columns.end - columns.first;
    renderer.renderRows(strip->pixels, row, rows, strip->column, strip->columns);
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
  uint32_t refreshes = refreshCount;
  const FluidSimulation::Profile& phases = simulation.profile();
  float phaseScale = 1000.0f / frames;  // s in total to ms per frame
  // transfer_ms runs on core 0 alongside the rest. Synced to the panel, refreshes_per_frame reads
  // REFRESHES_PER_FRAME (2 at 30 fps) while frames keep up; more means some overran a refresh.
  console.printf(
      "fps:%.1f,refreshes_per_frame:%.2f,simulation_ms:%.2f,render_ms:%.2f,transfer_ms:%.2f,display_wait_ms:%.2f,"
      "particles:%d,free_heap:%u,gravity_x:%.2f,gravity_y:%.2f,bus_voltage:%.2f,fill:%.2f\n",
      frames * 1e6f / (now - timings.windowStart), (refreshes - timings.refreshes) / frames,
      timings.simulation / frames / 1000.0f, timings.render / frames / 1000.0f, transferTime.exchange(0) / frames / 1000.0f,
      timings.displayWait / frames / 1000.0f, simulation.particleCount(), ESP.getFreeHeap(), simulation.gravityX(),
      simulation.gravityY(), fuelSensor.busVoltage(), fuelSensor.fillLevel());
  // Where the simulation step's time goes, for calibrating the cost model.
  console.printf("integrate_ms:%.2f,separate_ms:%.2f,walls_ms:%.2f,to_grid_ms:%.2f,density_ms:%.2f,pressure_ms:%.2f,from_grid_ms:%.2f\n",
                 phases.integrate * phaseScale, phases.separate * phaseScale, phases.walls * phaseScale,
                 phases.toGrid * phaseScale, phases.density * phaseScale, phases.pressure * phaseScale,
                 phases.fromGrid * phaseScale);
  simulation.resetProfile();
  timings = FrameTimings{};
  timings.windowStart = now;
  timings.refreshes = refreshes;
}
