// Exposes the shared gauge core to the tuner page. Built by build.sh.
#include <emscripten/bind.h>

#include <vector>

#include "slosh/FluidRenderer.h"
#include "slosh/FluidSimulation.h"
#include "slosh/PerformanceModel.h"
#include "slosh/TunedSettings.h"

using namespace emscripten;

// Catches a GaugeSettings field added without a binding below, which the tuner would silently zero.
static_assert(sizeof(GaugeSettings) == 21 * 4, "GaugeSettings changed: update the bindings below and FIELDS in index.html");

class Gauge {
 public:
  Gauge() : _pixels(DISPLAY_SIZE * DISPLAY_SIZE), _rgba(DISPLAY_SIZE * DISPLAY_SIZE * 4) { setSettings(TUNED_SETTINGS); }

  GaugeSettings settings() const { return _settings; }

  void setSettings(const GaugeSettings& settings) {
    _settings = settings;
    _simulation.configure(settings);
    _renderer.configure(settings, DISPLAY_SIZE);
  }

  void setGravity(float x, float y) { _simulation.setGravity(x, y); }
  void setFillLevel(float level) { _simulation.setFillLevel(level); }
  void step() { _simulation.step(); }
  int particleCount() const { return _simulation.particleCount(); }
  PerformanceEstimate performance() const { return estimatePerformance(_settings, DISPLAY_SIZE); }

  // Renders a frame and returns it as RGBA bytes, valid until the next call.
  val render() {
    _renderer.prepare(_simulation);
    _renderer.renderRows(_pixels.data(), 0, DISPLAY_SIZE, 0, DISPLAY_SIZE);
    for (size_t i = 0; i < _pixels.size(); i++) {
      uint16_t pixel = static_cast<uint16_t>((_pixels[i] << 8) | (_pixels[i] >> 8));  // from big-endian
      uint8_t red = pixel >> 11;
      uint8_t green = (pixel >> 5) & 63;
      uint8_t blue = pixel & 31;
      _rgba[4 * i] = (red << 3) | (red >> 2);
      _rgba[4 * i + 1] = (green << 2) | (green >> 4);
      _rgba[4 * i + 2] = (blue << 3) | (blue >> 2);
      _rgba[4 * i + 3] = 255;
    }
    return val(typed_memory_view(_rgba.size(), _rgba.data()));
  }

 private:
  GaugeSettings _settings{};
  FluidSimulation _simulation;
  FluidRenderer _renderer;
  std::vector<uint16_t> _pixels;
  std::vector<uint8_t> _rgba;
};

GaugeSettings tunedSettings() { return TUNED_SETTINGS; }

EMSCRIPTEN_BINDINGS(slosh) {
  value_object<GaugeSettings>("GaugeSettings")
      .field("fullChargeFill", &GaugeSettings::fullChargeFill)
      .field("tankDiameter", &GaugeSettings::tankDiameter)
      .field("gravityScale", &GaugeSettings::gravityScale)
      .field("gridResolution", &GaugeSettings::gridResolution)
      .field("particleRadiusRatio", &GaugeSettings::particleRadiusRatio)
      .field("substeps", &GaugeSettings::substeps)
      .field("pressureIterations", &GaugeSettings::pressureIterations)
      .field("separationIterations", &GaugeSettings::separationIterations)
      .field("overRelaxation", &GaugeSettings::overRelaxation)
      .field("flipRatio", &GaugeSettings::flipRatio)
      .field("driftCompensation", &GaugeSettings::driftCompensation)
      .field("densityResolution", &GaugeSettings::densityResolution)
      .field("smoothingPasses", &GaugeSettings::smoothingPasses)
      .field("surfaceThreshold", &GaugeSettings::surfaceThreshold)
      .field("surfaceSoftness", &GaugeSettings::surfaceSoftness)
      .field("rimWidth", &GaugeSettings::rimWidth)
      .field("glowStrength", &GaugeSettings::glowStrength)
      .field("coreColor", &GaugeSettings::coreColor)
      .field("rimColor", &GaugeSettings::rimColor)
      .field("glowColor", &GaugeSettings::glowColor)
      .field("targetFrameRate", &GaugeSettings::targetFrameRate);

  value_object<PerformanceEstimate>("PerformanceEstimate")
      .field("simulationTime", &PerformanceEstimate::simulationTime)
      .field("renderTime", &PerformanceEstimate::renderTime)
      .field("transferTime", &PerformanceEstimate::transferTime)
      .field("frameTime", &PerformanceEstimate::frameTime)
      .field("frameRate", &PerformanceEstimate::frameRate)
      .field("withinFrameBudget", &PerformanceEstimate::withinFrameBudget)
      .field("memory", &PerformanceEstimate::memory)
      .field("withinMemoryBudget", &PerformanceEstimate::withinMemoryBudget);

  class_<Gauge>("Gauge")
      .constructor<>()
      .function("settings", &Gauge::settings)
      .function("setSettings", &Gauge::setSettings)
      .function("setGravity", &Gauge::setGravity)
      .function("setFillLevel", &Gauge::setFillLevel)
      .function("step", &Gauge::step)
      .function("particleCount", &Gauge::particleCount)
      .function("performance", &Gauge::performance)
      .function("render", &Gauge::render);

  function("tunedSettings", &tunedSettings);
  constant("DISPLAY_SIZE", DISPLAY_SIZE);
  constant("MEMORY_BUDGET", MEMORY_BUDGET);
}
