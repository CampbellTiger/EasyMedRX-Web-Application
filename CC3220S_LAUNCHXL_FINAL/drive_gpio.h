#ifndef DRIVE_GPIO_H
#define DRIVE_GPIO_H

/*===========================================================================*/
/*  Includes                                                                 */
/*===========================================================================*/
#include <stdint.h>
#include <stdbool.h>

#include <ti/drivers/ADC.h>
#include <ti/drivers/SPI.h>
#include <ti/drivers/PWM.h>
#include <ti/drivers/GPIO.h>
#include <ti/display/Display.h>
#include <semaphore.h>
#include <pthread.h>

/*===========================================================================*/
/*  RC522 Register Map                                                       */
/*===========================================================================*/
#define CommandReg      0x01
#define ComIEnReg       0x02
#define ComIrqReg       0x04
#define ErrorReg        0x06
#define Status2Reg      0x08
#define FIFODataReg     0x09
#define FIFOLevelReg    0x0A
#define ControlReg      0x0C
#define BitFramingReg   0x0D
#define CollReg         0x0E
#define ModeReg         0x11
#define TxModeReg       0x12
#define RxModeReg       0x13
#define TxControlReg    0x14
#define TxASKReg        0x15
#define TModeReg        0x2A
#define TPrescalerReg   0x2B
#define TReloadRegH     0x2C
#define TReloadRegL     0x2D
#define RFCfgReg        0x26
#define VersionReg      0x37

/*===========================================================================*/
/*  RC522 Command Codes                                                      */
/*===========================================================================*/
#define PCD_Idle        0x00
#define PCD_Transceive  0x0C
#define PCD_SoftReset   0x0F

/*===========================================================================*/
/*  RC522 Status Codes                                                       */
/*===========================================================================*/
typedef enum
{
    RC522_OK        = 0,    /*!< Operation completed successfully  */
    RC522_ERR,              /*!< General error                     */
    RC522_TIMEOUT,          /*!< No response within timeout period */
    RC522_COLLISION         /*!< Bit collision detected            */
} RC522_Status;

/*===========================================================================*/
/*  Extern Globals (defined in hw_drivers.c)                                 */
/*===========================================================================*/

/** SPI driver handle used by the RC522 RFID module. */
extern SPI_Handle  spi;

/* Protects all SPI transactions */
extern pthread_mutex_t g_spiMutex;

/** SPI parameter structure used during SPI initialisation. */
extern SPI_Params  spiParams;

/** ADC driver handle. */
extern ADC_Handle  adc;

/** ADC parameter structure used during ADC initialisation. */
extern ADC_Params  adcParams;

/* Shared peripherals / OS objects provided by other translation units */
extern Display_Handle   display;
extern sem_t            sem;
extern PWM_Handle     pwm;
extern PWM_Params     params;

/*===========================================================================*/
/*  Interrupt                                                                */
/*===========================================================================*/

/**
 * @brief   GPIO interrupt handler for the IR sensor feedback line.
 *
 *          Resets the dispense tracker and posts the global semaphore
 *          to unblock a waiting dispense operation.
 *
 * @param   index   GPIO pin index passed by the driver callback mechanism.
 */
void IRQHandler(uint_least8_t index);

/**
 * @brief   Register IRQHandler on CONFIG_GPIO_11 and enable the interrupt.
 */
void configInterrupt(void);

/*===========================================================================*/
/*  SPI                                                                      */
/*===========================================================================*/

/**
 * @brief   Initialise the SPI controller at 4 MHz, mode 0, 8-bit frames.
 *          Opens CONFIG_SPI_0 and stores the handle in the global spi.
 */
void configSPI(void);

/**
 * @brief   SPI loopback / connectivity test thread.
 *
 *          Continuously transmits {0xFF, 0xAA} and prints both the
 *          transmitted and received bytes to the display.
 *
 * @param   pvParameters    Unused thread argument.
 * @return  NULL.
 */
void* testSPI(void* pvParameters);

/*===========================================================================*/
/*  RC522 RFID                                                               */
/*===========================================================================*/

/**
 * @brief   Write a single byte to an RC522 register over SPI.
 *
 * @param   reg     RC522 register address.
 * @param   value   Byte value to write.
 */
void RC522_Write(uint8_t reg, uint8_t value);

/**
 * @brief   Read a single byte from an RC522 register over SPI.
 *
 * @param   reg     RC522 register address.
 * @return  Register value, or 0xFF on SPI transfer failure.
 */
uint8_t RC522_Read(uint8_t reg);

/**
 * @brief   Initialise the RC522 module on the CC3220S.
 *
 *          Performs a soft reset, configures the timer, ASK modulation,
 *          RF gain, and turns the antenna on.
 *
 * @return  0 on success.
 */
int RC522_Init_CC3220(void);

/**
 * @brief   Send a REQA command and receive the ATQA response.
 *
 * @param   atqa    2-byte buffer to receive the Answer To reQuest Type A.
 * @return  RC522_OK on success; RC522_ERR or RC522_TIMEOUT on failure.
 */
RC522_Status RC522_RequestA(uint8_t *atqa);

/**
 * @brief   Perform Cascade Level 1 anti-collision to read the card UID.
 *
 * @param   uid     4-byte buffer to receive the UID bytes.
 * @param   bcc     Optional pointer to receive the Block Check Character;
 *                  pass NULL if not needed.
 * @return  RC522_OK on success; RC522_ERR on failure or BCC mismatch.
 */
RC522_Status RC522_Anticoll_CL1(uint8_t uid[4], uint8_t *bcc);

/**
 * @brief   Continuously poll for RFID cards and print the UID on detection.
 *
 *          Initialises the RC522 then blocks on the global semaphore between
 *          each poll cycle. Runs indefinitely.
 */
void *RC522_senseRFID(void *pvParameters);


/*===========================================================================*/
/*  ADC                                                                      */
/*===========================================================================*/

/**
 * @brief   Open CONFIG_ADC_0, perform a single conversion, close the handle,
 *          and return the raw ADC value.
 *
 * @return  Raw 16-bit ADC conversion result.
 */
uint16_t getADC(void);

/*===========================================================================*/
/*  PWM / Servo                                                              */
/*===========================================================================*/

/**
 * @brief   (Re)configure and start the PWM output at the requested pulse width.
 *
 *          Stops any running PWM, opens CONFIG_PWM_0 with a 20 ms period and
 *          the specified duty cycle, then starts the output.
 *
 * @param   uS  Desired pulse width in microseconds (e.g. 10002000 for a
 *              standard servo).
 */
void setPWM(int uS);

/* ============================================================
 *  PWM test thread
 * ============================================================ */

/**
 * @brief ADC-driven PWM control loop.
 *
 * Reads getADC() once per second and adjusts the PWM duty cycle based on
 * the returned value:
 *
 *   adcVal < 50          dead zone   hold current position
 *   50  <= adcVal < 130  increment   increase duty by 100 s (1%), clamped at 20 000
 *   130 <= adcVal < 250  decrement   decrease duty by 100 s (1%), clamped at 0
 *
 * dutyCycle is recalculated AFTER each i change so the logged value always
 * matches the value actually sent to setPWM().
 *
 * @param pvParameters  Unused (FreeRTOS / POSIX thread argument).
 * @return NULL.
 */
void* testPWM(void *pvParameters);
/**
 * @brief   Demultiplexer / servo cycling test thread.
 *
 *          Waits on the global semaphore then cycles through all four
 *          container select lines and servo positions with 10-second dwell
 *          times.
 *
 * @param   pvParameters    Unused thread argument.
 * @return  NULL.
 */
void* testDemux(void* pvParameters);

/*===========================================================================*/
/*  Dispense                                                                 */
/*===========================================================================*/

/**
 * @brief   Select the IR sensor demux channel for the given container and
 *          wait for the IR interrupt to confirm a pill has passed.
 *
 * @param   container   Container number (14).
 * @return  0 if a pill was detected; -1 on invalid container or timeout.
 */
int readIR(int container, const char *pActiveFile);

/**
 * @brief   Dispense one pill from the specified container.
 *
 *          Starts the servo, calls readIR() to confirm dispensing, then
 *          stops the servo. Displays an error message on the LCD if the
 *          dispense fails.
 *
 * @param   container   Container number (14).
 * @return  0 on success; -1 on failure.
 */
int dispense(int container);

#endif /* DRIVE_GPIO_H */
