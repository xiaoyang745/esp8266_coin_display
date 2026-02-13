// ===================================================================
// User_Setup.h for 1.54" 240x240 ST7789 + ESP8266-12F
// ===================================================================

// ==================== 驱动选择 ====================
#define ST7789_2_DRIVER   // 1.54" 240x240 ST7789

// ==================== 屏幕尺寸 ====================
#define TFT_WIDTH  240
#define TFT_HEIGHT 240

// ==================== 颜色顺序 ====================
#define TFT_RGB_ORDER TFT_RGB  // 如果颜色不对（红蓝互换），改成 TFT_BGR

// ==================== 引脚定义 (ESP8266-12F) ====================
#define TFT_MOSI 13  // SPI MOSI (D7)
#define TFT_SCLK 14  // SPI CLK  (D5)
#define TFT_CS   15  // Chip Select (PIN_D8 = GPIO15)
#define TFT_DC   0   // Data/Command (PIN_D3 = GPIO0)
#define TFT_RST  2   // Reset (PIN_D4 = GPIO2)
#define TFT_BL   5   // Backlight (PIN_D1 = GPIO5)

// ==================== SPI 频率 ====================
#define SPI_FREQUENCY  40000000  // 40MHz，如果显示有问题可以降到 27000000

// ==================== 字体启用（关键！）====================
#define LOAD_GLCD   // 基础字体，必须启用
#define LOAD_FONT2  // 小字体
#define LOAD_FONT4  // 中等字体
#define LOAD_FONT6  // 大字体
#define LOAD_FONT7  // 7段数码管字体
#define LOAD_FONT8  // 超大字体
#define LOAD_GFXFF  // FreeFonts 自由字体（推荐，支持更好看的字体）

// ==================== 平滑字体（可选，占用更多内存）====================
#define SMOOTH_FONT  // 如果 ESP8266 内存不够可以注释掉这行

// ==================== 其他设置 ====================
#define SPI_READ_FREQUENCY  20000000  // 读取频率