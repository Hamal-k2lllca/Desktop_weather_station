// aht20头文件（include）定义
#ifndef AHT20_H
#define AHT20_H

#include <Arduino.h>

// 结构体（struct）封装
typedef struct {
  float temperature;
  float humidity;
} aht20_data_t;

// 声明接口（API）
bool aht20_init(uint8_t sda_pin, uint8_t scl_pin);
bool aht20_getdata(aht20_data_t *data);

#endif