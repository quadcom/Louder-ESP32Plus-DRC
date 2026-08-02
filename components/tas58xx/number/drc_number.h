#pragma once

#include "esphome/components/number/number.h"
#include "esphome/core/component.h"
#include "esphome/core/preferences.h"
#include "../tas58xx.h"

namespace esphome::tas58xx {

// One class covers all fifteen DRC controls (three bands x five parameters),
// parameterised at codegen time. The EQ gains use one class per entity, but
// there are thirty of those with per-band coefficient tables behind them; the
// DRC parameters are uniform, so a single class avoids fifteen near-identical
// file pairs.
class DrcNumber : public number::Number, public Component, public Parented<Tas58xxComponent> {
 public:
  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_CONNECTION; }

  void set_drc_band(DrcBand band) { this->band_ = band; }
  void set_drc_parameter(DrcParameter parameter) { this->parameter_ = parameter; }

 protected:
  void control(float value) override;

  // Pushes the value to the component. Before 'loop' reaches DRC_SETUP the
  // component just caches it, which is what we want at boot.
  bool apply_(float value);

  float restore_default_();

  DrcBand band_{DRC_LOW};
  DrcParameter parameter_{DRC_PARAM_THRESHOLD};

  ESPPreferenceObject pref_;
};

}  // namespace esphome::tas58xx
