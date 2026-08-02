#pragma once

// Minimal stand-in for esphome/core/log.h. Log level set below VERBOSE so the
// ESP_LOGV calls compile out, exactly as they do in a normal ESPHome build.
#define ESPHOME_LOG_LEVEL_NONE 0
#define ESPHOME_LOG_LEVEL_ERROR 1
#define ESPHOME_LOG_LEVEL_WARN 2
#define ESPHOME_LOG_LEVEL_INFO 3
#define ESPHOME_LOG_LEVEL_CONFIG 4
#define ESPHOME_LOG_LEVEL_DEBUG 5
#define ESPHOME_LOG_LEVEL_VERBOSE 6
#define ESPHOME_LOG_LEVEL ESPHOME_LOG_LEVEL_DEBUG

#define ESP_LOGV(tag, ...) ((void) 0)
#define ESP_LOGD(tag, ...) ((void) 0)
#define ESP_LOGI(tag, ...) ((void) 0)
#define ESP_LOGW(tag, ...) ((void) 0)
#define ESP_LOGE(tag, ...) ((void) 0)
