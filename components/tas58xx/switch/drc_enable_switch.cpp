#include "drc_enable_switch.h"
#include "esphome/core/log.h"

namespace esphome::tas58xx {

static constexpr const char* TAG = "tas58xx.switch";

void DrcEnableSwitch::setup() {
  optional<bool> initial_state = this->get_initial_state_with_restore_mode();
  bool setup_state = initial_state.has_value() ? initial_state.value() : false;
  this->write_state(setup_state);

  // See DrcNumber::setup - coefficient writes wait for 'loop'.
  this->parent_->refresh_eq_settings();
}

void DrcEnableSwitch::dump_config() {
  ESP_LOGCONFIG(TAG, "Tas58xx Switch:");
  LOG_SWITCH("  ", "Enable DRC", this);
}

void DrcEnableSwitch::write_state(bool state) {
  this->publish_state(state);
  this->parent_->set_drc_enable(state);
}

}  // namespace esphome::tas58xx
