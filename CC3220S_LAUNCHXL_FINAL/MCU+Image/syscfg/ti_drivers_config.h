/*
 *  ======== ti_drivers_config.h ========
 *  Configured TI-Drivers module declarations
 *
 *  The macros defines herein are intended for use by applications which
 *  directly include this header. These macros should NOT be hard coded or
 *  copied into library source code.
 *
 *  Symbols declared as const are intended for use with libraries.
 *  Library source code must extern the correct symbol--which is resolved
 *  when the application is linked.
 *
 *  DO NOT EDIT - This file is generated for the CC3220S_LAUNCHXL
 *  by the SysConfig tool.
 */
#ifndef ti_drivers_config_h
#define ti_drivers_config_h

#define CONFIG_SYSCONFIG_PREVIEW

#define CONFIG_CC3220S_LAUNCHXL
#ifndef DeviceFamily_CC3220
#define DeviceFamily_CC3220
#endif

#include <ti/devices/DeviceFamily.h>

#include <stdint.h>

/* support C++ sources */
#ifdef __cplusplus
extern "C" {
#endif


/*
 *  ======== ADC ========
 */

/* P59, Analog Input 3 */
extern const uint_least8_t              CONFIG_ADC_0_CONST;
#define CONFIG_ADC_0                    0
#define CONFIG_TI_DRIVERS_ADC_COUNT     1


/*
 *  ======== Crypto ========
 */

extern const uint_least8_t                  CONFIG_Crypto_0_CONST;
#define CONFIG_Crypto_0                     0
#define CONFIG_TI_DRIVERS_CRYPTO_COUNT      1

/*
 *  ======== GPIO ========
 */
/* Owned by CONFIG_ADC_0 as  */
extern const uint_least8_t CONFIG_GPIO_ADC_0_AIN_CONST;
#define CONFIG_GPIO_ADC_0_AIN 4

extern const uint_least8_t CONFIG_GPIO_12_CONST;
#define CONFIG_GPIO_12 12

extern const uint_least8_t CONFIG_GPIO_06_CONST;
#define CONFIG_GPIO_06 6

extern const uint_least8_t CONFIG_GPIO_07_CONST;
#define CONFIG_GPIO_07 7

extern const uint_least8_t CONFIG_GPIO_10_CONST;
#define CONFIG_GPIO_10 10

extern const uint_least8_t CONFIG_GPIO_11_CONST;
#define CONFIG_GPIO_11 11

extern const uint_least8_t CONFIG_GPIO_08_CONST;
#define CONFIG_GPIO_08 8

extern const uint_least8_t CONFIG_GPIO_30_CONST;
#define CONFIG_GPIO_30 30

extern const uint_least8_t CONFIG_GPIO_09_CONST;
#define CONFIG_GPIO_09 9

extern const uint_least8_t CONFIG_GPIO_00_CONST;
#define CONFIG_GPIO_00 0

extern const uint_least8_t CONFIG_GPIO_28_CONST;
#define CONFIG_GPIO_28 28

/* The range of pins available on this device */
extern const uint_least8_t GPIO_pinLowerBound;
extern const uint_least8_t GPIO_pinUpperBound;

/* LEDs are active high */
#define CONFIG_GPIO_LED_ON  (1)
#define CONFIG_GPIO_LED_OFF (0)

#define CONFIG_LED_ON  (CONFIG_GPIO_LED_ON)
#define CONFIG_LED_OFF (CONFIG_GPIO_LED_OFF)


/*
 *  ======== PWM ========
 */

/* P21 */
extern const uint_least8_t              CONFIG_PWM_0_CONST;
#define CONFIG_PWM_0                    0
#define CONFIG_TI_DRIVERS_PWM_COUNT     1


/*
 *  ======== SPI ========
 */

extern const uint_least8_t              CONFIG_NWP_SPI_CONST;
#define CONFIG_NWP_SPI                  0
/*
 *  PICO: P07
 *  POCI: P06
 *  SCLK: P05
 *  CSN: P08
 */
extern const uint_least8_t              CONFIG_SPI_0_CONST;
#define CONFIG_SPI_0                    1
#define CONFIG_TI_DRIVERS_SPI_COUNT     2


/*
 *  ======== UART2 ========
 */

/*
 *  TX: P55
 *  RX: P57
 *  XDS110 UART
 */
extern const uint_least8_t                  CONFIG_UART2_0_CONST;
#define CONFIG_UART2_0                      0
#define CONFIG_TI_DRIVERS_UART2_COUNT       1


/*
 *  ======== Watchdog ========
 */

extern const uint_least8_t                  CONFIG_WATCHDOG_0_CONST;
#define CONFIG_WATCHDOG_0                   0
#define CONFIG_TI_DRIVERS_WATCHDOG_COUNT    1


/*
 *  ======== Board_init ========
 *  Perform all required TI-Drivers initialization
 *
 *  This function should be called once at a point before any use of
 *  TI-Drivers.
 */
extern void Board_init(void);

/*
 *  ======== Board_initGeneral ========
 *  (deprecated)
 *
 *  Board_initGeneral() is defined purely for backward compatibility.
 *
 *  All new code should use Board_init() to do any required TI-Drivers
 *  initialization _and_ use <Driver>_init() for only where specific drivers
 *  are explicitly referenced by the application.  <Driver>_init() functions
 *  are idempotent.
 */
#define Board_initGeneral Board_init

#ifdef __cplusplus
}
#endif

#endif /* include guard */
