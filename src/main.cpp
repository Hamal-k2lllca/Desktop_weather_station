#include <Arduino.h>
#include <Wire.h>

// I2C 硬件引脚定义 (SDA: GPIO 8, SCL: GPIO 9)
#define I2C_SDA_PIN 8
#define I2C_SCL_PIN 9

// AHT20 芯片 I2C 设备地址 (7位)
#define AHT20_ADDR 0x38

// 板载 RGB LED 引脚 (ESP32-S3 DevKitC-1 通常为 GPIO 48)
#ifndef RGB_BUILTIN
#define RGB_BUILTIN 48
#endif

// ============================================================================
// AHT20 底层驱动函数
// ============================================================================

/**
 * @brief 初始化 AHT20 传感器
 * @return true 初始化成功 / false 失败
 */
bool aht20_init() {
    delay(100); // 芯片上电等待至少 100ms

    // 1. 读取状态字，检查是否已校准
    Wire.requestFrom(AHT20_ADDR, 1);
    if (Wire.available()) {
        uint8_t status = Wire.read();
        // 若 Bit[3] 校准使能位不是 1，则下发初始化校准命令
        if ((status & 0x08) == 0) {
            Wire.beginTransmission(AHT20_ADDR);
            Wire.write(0xBE); // 初始化命令寄存器
            Wire.write(0x08); // 参数 1
            Wire.write(0x00); // 参数 2
            if (Wire.endTransmission() != 0) {
                return false;
            }
            delay(10); // 等待校准生效
        }
        return true;
    }
    return false;
}

/**
 * @brief 触发测量并读取 AHT20 温湿度原始数据
 * @param temperature 摄氏度 (℃)
 * @param humidity 相对湿度 (%RH)
 * @return true 采集成功 / false 采集失败
 */
bool aht20_read(float *temperature, float *humidity) {
    // 步骤 1: 下发测量触发命令 (0xAC, 0x33, 0x00)
    Wire.beginTransmission(AHT20_ADDR);
    Wire.write(0xAC);
    Wire.write(0x33);
    Wire.write(0x00);
    if (Wire.endTransmission() != 0) {
        return false;
    }

    // 步骤 2: 等待传感器完成内部 AD 模数转换 (数据手册要求典型值 75ms)
    delay(80);

    // 步骤 3: 连续请求 6 个字节的数据包
    // Byte[0]: 状态字
    // Byte[1]: 湿度高 8 位
    // Byte[2]: 湿度中 8 位
    // Byte[3]: [高4位]湿度低4位 | [低4位]温度高4位 (核心跨字节混拼区)
    // Byte[4]: 温度中 8 位
    // Byte[5]: 温度低 8 位
    uint8_t raw[6];
    Wire.requestFrom(AHT20_ADDR, 6);
    if (Wire.available() < 6) {
        return false;
    }

    for (int i = 0; i < 6; i++) {
        raw[i] = Wire.read();
    }

    // 检查状态字 Bit[7] (忙标志位，0 表示转换完成空闲，1 表示正忙)
    if ((raw[0] & 0x80) != 0) {
        return false; // 芯片仍处于忙状态
    }

    // ========================================================================
    // 💡 核心第一性原理：20 位跨字节位移拼装算法 (Bit-Shifting Algorithm)
    // ========================================================================
    
    // 1. 湿度 20 位拼装：
    // raw[1] (高8位) 左移 12 位 -> 占 [19~12]
    // raw[2] (中8位) 左移 4 位  -> 占 [11~4]
    // raw[3] (低4位位于其高半字节) 右移 4 位 -> 占 [3~0]
    uint32_t raw_humidity = ((uint32_t)raw[1] << 12) | ((uint32_t)raw[2] << 4) | (raw[3] >> 4);

    // 2. 温度 20 位拼装：
    // raw[3] 掩码过滤低 4 位 (& 0x0F) 并左移 16 位 -> 占 [19~16]
    // raw[4] (中8位) 左移 8 位                     -> 占 [15~8]
    // raw[5] (低8位)                               -> 占 [7~0]
    uint32_t raw_temp = (((uint32_t)(raw[3] & 0x0F)) << 16) | ((uint32_t)raw[4] << 8) | raw[5];

    // 步骤 4: 依据官方数据手册标定物理公式换算为浮点数 (总刻度为 2^20 = 1048576)
    *humidity = ((float)raw_humidity * 100.0f) / 1048576.0f;
    *temperature = ((float)raw_temp * 200.0f) / 1048576.0f - 50.0f;

    return true;
}

// ============================================================================
// 主程序 Setup 与 Loop
// ============================================================================

void setup() {
    Serial.begin(115200);
    delay(2000);

    Serial.println("\r\n==================================================");
    Serial.println("  🚀 ESP32-S3 智能桌面气象站 · AHT20 温湿度采集系统");
    Serial.println("==================================================");

    // 初始化 I2C 总线
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);

    // 初始化 AHT20
    Serial.print("正在初始化 AHT20 传感器...");
    if (aht20_init()) {
        Serial.println(" [OK] 校验与校准就绪！");
        neopixelWrite(RGB_BUILTIN, 0, 30, 0); // 绿色常青指示初始化成功
    } else {
        Serial.println(" [FAIL] 初始化失败，请检查接线！");
        neopixelWrite(RGB_BUILTIN, 30, 0, 0); // 红色报警
    }
}

void loop() {
    float temp = 0.0f;
    float hum = 0.0f;

    if (aht20_read(&temp, &hum)) {
        // 采集成功，蓝色微光闪烁一次作为数据采样指示
        neopixelWrite(RGB_BUILTIN, 0, 0, 40);

        Serial.println("\r\n┌──────────────────────────────────────────────┐");
        Serial.printf("│  🌡️ 室内实时温度:  %6.2f ℃                    │\r\n", temp);
        Serial.printf("│  💧 室内相对湿度:  %6.2f %%RH                  │\r\n", hum);
        Serial.println("└──────────────────────────────────────────────┘");

        delay(100);
        neopixelWrite(RGB_BUILTIN, 0, 20, 0); // 恢复健康微绿常亮
    } else {
        Serial.println("⚠️ 读取温湿度失败: 传感器总线未应答或正忙...");
        neopixelWrite(RGB_BUILTIN, 30, 15, 0); // 橙色警告
    }

    // 每 2 秒刷新一次环境数据
    delay(2000);
}


