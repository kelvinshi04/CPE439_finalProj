/*
 *******************************************************************************
 * app_types.h
 * Shared FreeRTOS primitive handles, struct definitions, and constants
 * for the multimeter / function generator project.
 *
 * Include this file in every task .c file. Do NOT define globals here —
 * they live in app_init.c and are declared extern below.
 *******************************************************************************
 */

#ifndef INC_APP_TYPES_H_
#define INC_APP_TYPES_H_

#include "cmsis_os.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "event_groups.h"

/* ===========================================================================
 * SYSTEM MODES
 * Stored in system_config_t.mode and mirrored as event group bits.
 * =========================================================================*/
typedef enum {
    MODE_DC     = 0,
    MODE_AC     = 1,
    MODE_FOLLOW = 2,
    MODE_FUNC   = 3,
} SystemMode_t;

/* Event group bit positions — one bit per mode */
#define EV_MODE_DC      (1 << 0)
#define EV_MODE_AC      (1 << 1)
#define EV_MODE_FOLLOW  (1 << 2)
#define EV_MODE_FUNC    (1 << 3)
#define EV_RUNNING      (1 << 4)   /* DAC output active */
#define EV_ALL_MODES    (EV_MODE_DC | EV_MODE_AC | EV_MODE_FOLLOW | EV_MODE_FUNC)

/* ===========================================================================
 * WAVEFORM TYPES
 * =========================================================================*/
typedef enum {
    WAVE_SINE     = 0,
    WAVE_SQUARE   = 1,
    WAVE_TRIANGLE = 2,
    WAVE_SAWTOOTH = 3,
    WAVE_COUNT    = 4,
} WaveType_t;

/* ===========================================================================
 * UART COMMAND TYPES
 * Parsed by UART_task, sent over cmdQueue to mode_manager_task.
 * =========================================================================*/
typedef enum {
    CMD_MODE,
    CMD_WAVE,
    CMD_FREQ,
    CMD_AMP,
    CMD_START,
    CMD_STOP,
    CMD_STATUS,
    CMD_HELP,
    CMD_UNKNOWN,
} CmdType_t;

typedef struct {
    CmdType_t   type;
    SystemMode_t mode;      /* valid when type == CMD_MODE */
    WaveType_t  wave;       /* valid when type == CMD_WAVE */
    uint32_t    value;      /* valid for FREQ (Hz), AMP (mV), OFFSET (mV) */
} UartCmd_t;

/* ===========================================================================
 * SYSTEM CONFIGURATION
 * Single authoritative struct owned by mode_manager_task.
 * Sent over configQueue (depth 1, overwrite) to DAC_task.
 * =========================================================================*/

/* Waveform LUT size — power of 2 for fast modulo via bitmask */
#define LUT_SIZE        256u
#define LUT_MASK        (LUT_SIZE - 1u)

/* Default waveform parameters */
#define DEFAULT_FREQ_HZ     100u
#define DEFAULT_AMP_MV      2000u
#define DEFAULT_OFFSET_MV   1650u

/*
 * DAC sample rate in Hz. DAC_task runs at this rate via vTaskDelayUntil.
 * Max reliable output frequency = SAMPLE_RATE_HZ / LUT_SIZE.
 * At 10000 Hz sample rate: max output = 39 Hz clean, ~100 Hz acceptable.
 * Raise SAMPLE_RATE_HZ for higher frequencies (adjust stack/priority too).
 */
#define SAMPLE_RATE_HZ      1000u
#define SAMPLE_PERIOD_MS    (1000u / SAMPLE_RATE_HZ)   /* 0 if > 1kHz — use ticks directly */
#define SAMPLE_PERIOD_TICKS (configTICK_RATE_HZ / SAMPLE_RATE_HZ)

typedef struct {
    SystemMode_t    mode;
    WaveType_t      wave;
    uint32_t        freqHz;
    uint32_t        ampMv;       /* peak-to-peak amplitude in mV */
    uint32_t        offsetMv;    /* DC offset in mV */
    uint32_t        phaseStep;   /* = freqHz * LUT_SIZE / SAMPLE_RATE_HZ */
    uint8_t         running;     /* pdTRUE when START issued */
} SystemConfig_t;

/* ===========================================================================
 * MEASUREMENT DATA
 * Written by metrics_task under dataMutex.
 * Read by UART_task and LCD_task (also under dataMutex).
 * =========================================================================*/

/* AC sample buffer size — must be >= one full waveform period at SAMPLE_RATE
 * for lowest expected frequency. At 100 Hz: 100 samples minimum. */
#define AC_BUF_SIZE     256u

typedef struct {
    /* DC metrics */
    float   voltage;
    float   vMin;
    float   vMax;
    float   vAvg;       /* exponential moving average */

    /* AC metrics */
    float   vPP;        /* peak-to-peak voltage */
    float   vRMS;       /* true RMS */
    float   vBias;      /* measured DC bias (rolling mean) */

    /* DAC follower */
    float   dacOutV;    /* current DAC output in volts */

    /* Mode snapshot for display */
    SystemMode_t mode;
    WaveType_t   wave;
    uint32_t     freqHz;
    uint32_t     ampMv;
    uint8_t      running;
} Measurement_t;

/* DAC follow-mode sample — depth-1 overwrite queue */
typedef struct {
    uint16_t dacWord;   /* raw 12-bit DAC value */
} DacSample_t;

/* ===========================================================================
 * DISPLAY MESSAGES
 * Sent from metrics_task to LCD_task over lcdQueue.
 * UART_task is command-only and does not receive metrics.
 * =========================================================================*/
#define LCD_LINE_LEN    16u

typedef struct {
    char line1[LCD_LINE_LEN + 1];
    char line2[LCD_LINE_LEN + 1];
} LcdMsg_t;

/* ===========================================================================
 * TASK HANDLES — defined in app_init.c
 * =========================================================================*/
extern TaskHandle_t adcTaskHandle;
extern TaskHandle_t metricsTaskHandle;
extern TaskHandle_t dacTaskHandle;
extern TaskHandle_t uartTaskHandle;
extern TaskHandle_t lcdTaskHandle;
extern TaskHandle_t modeManagerTaskHandle;

/* NOTE: adc.h also declares 'extern TaskHandle_t adcTask;'
 * adcTaskHandle here IS that handle — assign adcTask = adcTaskHandle
 * after xTaskCreate in app_init.c so the ADC ISR can find it. */

/* ===========================================================================
 * QUEUE HANDLES — defined in app_init.c
 * =========================================================================*/
extern QueueHandle_t adcQueue;      /* uint16_t raw samples,   depth 10  */
extern QueueHandle_t followQueue;   /* DacSample_t,            depth 1   */
extern QueueHandle_t cmdQueue;      /* UartCmd_t,              depth 8   */
extern QueueHandle_t configQueue;   /* SystemConfig_t,         depth 1   */
extern QueueHandle_t lcdQueue;      /* LcdMsg_t,               depth 4   */

/* ===========================================================================
 * SYNCHRONISATION PRIMITIVES — defined in app_init.c
 * =========================================================================*/
extern SemaphoreHandle_t dataMutex;
extern SemaphoreHandle_t configMutex;
extern EventGroupHandle_t eventGroup;

/* ===========================================================================
 * SHARED MEASUREMENT — written by metrics_task, read by display tasks
 * Protected by dataMutex.
 * =========================================================================*/
extern volatile Measurement_t sharedMeas;

#endif /* INC_APP_TYPES_H_ */
