#include "drc_number.h"
#include "esphome/core/log.h"

namespace esphome::tas58xx {

static constexpr const char* TAG = "tas58xx.number";

float DrcNumber::restore_default_() {
  switch (this->parameter_) {
    case DRC_PARAM_THRESHOLD: return DRC_THRESHOLD_DEFAULT_DB;
    case DRC_PARAM_RATIO:     return DRC_RATIO_DEFAULT;
    case DRC_PARAM_ATTACK:    return DRC_ATTACK_DEFAULT_MS;
    case DRC_PARAM_RELEASE:   return DRC_RELEASE_DEFAULT_MS;
    case DRC_PARAM_MAKEUP:    return DRC_MAKEUP_DEFAULT_DB;
  }
  return 0.0f;
}

bool DrcNumber::apply_(float value) {
  switch (this->parameter_) {
    case DRC_PARAM_THRESHOLD: return this->parent_->set_drc_threshold(this->band_, value);
    case DRC_PARAM_RATIO:     return this->parent_->set_drc_ratio(this->band_, value);
    case DRC_PARAM_ATTACK:    return this->parent_->set_drc_attack(this->band_, value);
    case DRC_PARAM_RELEASE:   return this->parent_->set_drc_release(this->band_, value);
    case DRC_PARAM_MAKEUP:    return this->parent_->set_drc_makeup(this->band_, value);
  }
  return false;
}

void DrcNumber::setup() {
  float value;
  this->pref_ = this->make_entity_preference<float>();
  if (!this->pref_.load(&value)) value = this->restore_default_();

  // So the component can drive every configured control from one button. Only
  // the entities actually present in the YAML register, which is what we want:
  // a reset must not invent controls the user did not ask for.
  this->parent_->register_drc_number(this);

  this->publish_state(value);
  this->apply_(value);

  // Nothing reaches the DSP until it has seen a valid I2S clock, so the
  // component defers all coefficient writes until 'loop' runs. Ask for that
  // sequence to start. Idempotent, and it means DRC works in configs that
  // define no EQ entities at all.
  this->parent_->refresh_eq_settings();
}

void DrcNumber::dump_config() {
  ESP_LOGCONFIG(TAG, "Tas58xx DRC Number:");
  ESP_LOGCONFIG(TAG, "  %s band %s '%s'", DRC_BAND_TEXT[this->band_],
                DRC_PARAM_TEXT[this->parameter_], this->get_name().c_str());
}

void DrcNumber::control(float value) {
  this->publish_state(value);
  this->apply_(value);
  this->pref_.save(&value);
}

}  // namespace esphome::tas58xx
