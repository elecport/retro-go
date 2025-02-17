// REF: https://wiki.odroid.com/odroid_go/odroid_go

// Target definition
#define RG_TARGET_NAME             "ODROID-GO"

// Storage and Settings
#define RG_STORAGE_DRIVER           1   // 1 = SDSPI, 2 = SDMMC, 3 = USB
#define RG_SETTINGS_USE_NVS         0

// Audio
#define RG_AUDIO_USE_SPEAKER        1
#define RG_AUDIO_USE_EXT_DAC        1

// Video
#define RG_SCREEN_TYPE              0
#define RG_SCREEN_WIDTH             320
#define RG_SCREEN_HEIGHT            240
#define RG_SCREEN_ROTATE            0
#define RG_SCREEN_MARGIN_TOP        0
#define RG_SCREEN_MARGIN_BOTTOM     0
#define RG_SCREEN_MARGIN_LEFT       0
#define RG_SCREEN_MARGIN_RIGHT      0

// Input
#define RG_GAMEPAD_DRIVER           1   // 1 = ODROID-GO, 2 = Serial, 3 = MRGC-IO
#define RG_GAMEPAD_MENU_BTN         1
#define RG_GAMEPAD_OPTION_BTN       1
#define RG_BATT_ADC_CHANNEL         ADC1_CHANNEL_0
#define RG_BATT_MULTIPLIER          2.0f
#define RG_BATT_VOLT_MIN            3.5f
#define RG_BATT_VOLT_MAX            4.2f

// Status LED
#define RG_GPIO_LED                 GPIO_NUM_2

// I2C BUS
#define RG_GPIO_I2C_SDA             GPIO_NUM_15
#define RG_GPIO_I2C_SCL             GPIO_NUM_4

// Built-in gamepad
#define RG_GPIO_GAMEPAD_X           ADC1_CHANNEL_6
#define RG_GPIO_GAMEPAD_Y           ADC1_CHANNEL_7
#define RG_GPIO_GAMEPAD_SELECT      GPIO_NUM_27
#define RG_GPIO_GAMEPAD_START       GPIO_NUM_39
#define RG_GPIO_GAMEPAD_A           GPIO_NUM_32
#define RG_GPIO_GAMEPAD_B           GPIO_NUM_33
#define RG_GPIO_GAMEPAD_MENU        GPIO_NUM_13
#define RG_GPIO_GAMEPAD_OPTION      GPIO_NUM_0

// SNES-style gamepad
#define RG_GPIO_GAMEPAD_LATCH       GPIO_NUM_NC
#define RG_GPIO_GAMEPAD_CLOCK       GPIO_NUM_NC
#define RG_GPIO_GAMEPAD_DATA        GPIO_NUM_NC

// SPI Display
#define RG_GPIO_LCD_MISO            GPIO_NUM_19
#define RG_GPIO_LCD_MOSI            GPIO_NUM_23
#define RG_GPIO_LCD_CLK             GPIO_NUM_18
#define RG_GPIO_LCD_CS              GPIO_NUM_5
#define RG_GPIO_LCD_DC              GPIO_NUM_21
#define RG_GPIO_LCD_BCKL            GPIO_NUM_14

// SPI SD Card
#define RG_GPIO_SD_MISO             GPIO_NUM_19
#define RG_GPIO_SD_MOSI             GPIO_NUM_23
#define RG_GPIO_SD_CLK              GPIO_NUM_18
#define RG_GPIO_SD_CS               GPIO_NUM_22

// External I2S DAC
#define RG_GPIO_SND_I2S_BCK         GPIO_NUM_4
#define RG_GPIO_SND_I2S_WS          GPIO_NUM_12
#define RG_GPIO_SND_I2S_DATA        GPIO_NUM_15
#define RG_GPIO_SND_AMP_ENABLE      GPIO_NUM_NC
