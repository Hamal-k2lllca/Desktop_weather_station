#include "aht20.h"
#include <Wire.h>

// 定义 AHT20 I2C 地址
#define AHT20_T2C_ADDR

// 定义 AHT20 控制寄存器命令
#define AHT20_CMD_INIT 0xBE