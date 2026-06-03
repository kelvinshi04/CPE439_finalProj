/*
 *******************************************************************************
 * task_metrics.c
 * metrics_task — signal processing hub.
 *
 * Receives raw ADC samples from adcQueue, computes measurements, then:
 *   - Writes results to sharedMeas under dataMutex
 *   - Sends DacSample_t to followQueue (FOLLOW mode)
 *   - Sends LcdMsg_t to lcdQueue
 *
 * UART no longer receives metrics. LCD is the sole live display.
 *******************************************************************************
 */

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "app_types.h"
#include "dac.h"

static inline float adcToMv(uint16_t raw) {
    return (float)raw * (3300.0f / 4095.0f);
}

/* --------------------------------------------------------------------------
 * computeAC() — processes AC_BUF_SIZE samples, writes vBias/vPP/vRMS
 * --------------------------------------------------------------------------*/
static void computeAC(const uint16_t *buf, Measurement_t *m) {
    float sum = 0.0f;
    for (uint32_t i = 0; i < AC_BUF_SIZE; i++)
        sum += adcToMv(buf[i]);
    float bias = sum / (float)AC_BUF_SIZE;

    float acMin =  1e9f, acMax = -1e9f, sumSq = 0.0f;
    for (uint32_t i = 0; i < AC_BUF_SIZE; i++) {
        float ac = adcToMv(buf[i]) - bias;
        if (ac < acMin) acMin = ac;
        if (ac > acMax) acMax = ac;
        sumSq += ac * ac;
    }
    m->vBias = bias;
    m->vPP   = acMax - acMin;
    m->vRMS  = sqrtf(sumSq / (float)AC_BUF_SIZE);
}

/* --------------------------------------------------------------------------
 * buildLcdMsg()
 * LCD is now the primary display so each mode gets two packed info lines.
 *
 * DC mode:
 *   Line 1:  DC  +1.234V
 *   Line 2:  L1.200 H1.800A     (min, max, avg)
 *
 * AC mode:
 *   Line 1:  AC Vpp 1.234V
 *   Line 2:  RMS 0.872V
 *
 * FOLLOW mode:
 *   Line 1:  FOL IN  1.234V
 *   Line 2:      OUT 1.234V
 *
 * FUNC mode:
 *   Line 1:  SINE  100Hz
 *   Line 2:  2000mV  RUN        (amplitude + state)
 * --------------------------------------------------------------------------*/
/*
 * fmtV() — converts millivolts to "X.XX" string without using float printf.
 * Avoids any nano-libc float formatting issues on Cortex-M.
 * dst must be at least 5 bytes: sign + digit + '.' + 2 digits + null.
 * Returns number of characters written (not including null).
 */
static uint8_t fmtV(char *dst, int32_t mv) {
    uint8_t n = 0;
    if (mv < 0) { dst[n++] = '-'; mv = -mv; }
    else          { dst[n++] = ' '; }
    /* integer part: 0–3 V → always single digit in range */
    dst[n++] = '0' + (mv / 1000) % 10;
    dst[n++] = '.';
    dst[n++] = '0' + (mv / 100)  % 10;
    dst[n++] = '0' + (mv / 10)   % 10;
    dst[n]   = '\0';
    return n;
}

/*
 * lcdLine() — builds exactly LCD_LINE_LEN characters into dst.
 * Uses snprintf into a zeroed temp buffer, then pads with spaces.
 * No float format specifiers used here — voltages go through fmtV().
 */
static void lcdLine(char *dst, const char *fmt, ...) {
    char tmp[32];
    memset(tmp, 0, sizeof(tmp));    /* zero so unwritten positions are safe */
    va_list args;
    va_start(args, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, args);
    va_end(args);

    for (uint8_t i = 0; i < LCD_LINE_LEN; i++)
        dst[i] = (tmp[i] != '\0') ? tmp[i] : ' ';
    dst[LCD_LINE_LEN] = '\0';
}

/*
 * lcdVoltLine() — builds a voltage line without float printf.
 * label: up to 6 chars prefix, e.g. "DC   " or "Vpp  "
 * mv: millivolts as int32_t
 * Result: "DC    1.64V     " padded to 16 chars
 */
static void lcdVoltLine(char *dst, const char *label, int32_t mv) {
    char tmp[32];
    memset(tmp, 0, sizeof(tmp));
    uint8_t n = 0;
    /* copy label */
    while (label[n] && n < 6) { tmp[n] = label[n]; n++; }
    /* format voltage */
    n += fmtV(tmp + n, mv);
    tmp[n++] = 'V';
    /* pad to LCD_LINE_LEN */
    for (uint8_t i = 0; i < LCD_LINE_LEN; i++)
        dst[i] = (tmp[i] != '\0') ? tmp[i] : ' ';
    dst[LCD_LINE_LEN] = '\0';
}

static void buildLcdMsg(const Measurement_t *m, LcdMsg_t *lcd) {
    char va[6], vb[6];   /* voltage string scratch — fmtV needs 5 + null */

    switch (m->mode) {

        case MODE_DC:
            /* Line 1: "DC    1.64V     " */
            lcdVoltLine(lcd->line1, "DC   ", (int32_t)m->voltage);
            /* Line 2: "Lo1.64 Hi1.80   " */
            fmtV(va, (int32_t)m->vMin);
            fmtV(vb, (int32_t)m->vMax);
            lcdLine(lcd->line2, "Lo%s Hi%s", va, vb);
            break;

        case MODE_AC:
            /* Line 1: "Vpp   1.64V     " */
            lcdVoltLine(lcd->line1, "Vpp  ", (int32_t)m->vPP);
            /* Line 2: "RMS   1.16V     " */
            lcdVoltLine(lcd->line2, "RMS  ", (int32_t)m->vRMS);
            break;

        case MODE_FOLLOW:
            lcdVoltLine(lcd->line1, "IN   ", (int32_t)m->voltage);
            lcdVoltLine(lcd->line2, "OUT  ", (int32_t)m->dacOutV);
            break;

        case MODE_FUNC: {
            const char *wNames[] = {"SINE","SQR ","TRI ","SAW "};
            /* Line 1: "SINE  100Hz     " */
            lcdLine(lcd->line1, "%s  %luHz",
                    wNames[m->wave], (unsigned long)m->freqHz);
            /* Line 2: "2000mV  RUN     " */
            lcdLine(lcd->line2, "%lumV  %s",
                    (unsigned long)m->ampMv,
                    m->running ? "RUN" : "STP");
            break;
        }
    }
}

/* --------------------------------------------------------------------------
 * metrics_task
 * --------------------------------------------------------------------------*/
void metrics_task(void *pvParams) {
    (void)pvParams;

    uint16_t      acBuf[AC_BUF_SIZE];
    uint32_t      acIdx = 0;
    Measurement_t m     = {0};
    m.vMin = 3300.0f;

    const float followerGain = 1.0f;

    for (;;) {
        uint16_t raw;
        xQueueReceive(adcQueue, &raw, portMAX_DELAY);

        EventBits_t bits = xEventGroupGetBits(eventGroup);
        float vMv = adcToMv(raw);

        /* ---- DC -------------------------------------------------------- */
        if (bits & EV_MODE_DC) {
            m.voltage = vMv;
            if (vMv < m.vMin) m.vMin = vMv;
            if (vMv > m.vMax) m.vMax = vMv;
            m.vAvg += (vMv - m.vAvg) * 0.05f;
            m.mode = MODE_DC;
        }

        /* ---- AC -------------------------------------------------------- */
        else if (bits & EV_MODE_AC) {
            acBuf[acIdx % AC_BUF_SIZE] = raw;
            acIdx++;
            if (acIdx % AC_BUF_SIZE == 0)
                computeAC(acBuf, &m);
            m.mode = MODE_AC;
        }

        /* ---- FOLLOW ---------------------------------------------------- */
        else if (bits & EV_MODE_FOLLOW) {
            m.voltage = vMv;
            m.mode    = MODE_FOLLOW;
            float outMv = vMv * followerGain;
            if (outMv > VOLTAGE_RAIL) outMv = VOLTAGE_RAIL;
            m.dacOutV = outMv;
            DacSample_t s = { DAC_volt_conv((uint32_t)outMv) };
            xQueueOverwrite(followQueue, &s);
        }

        /* ---- FUNC (display only) --------------------------------------- */
        else if (bits & EV_MODE_FUNC) {
            SystemConfig_t cfg;
            if (xQueuePeek(configQueue, &cfg, 0) == pdTRUE) {
                m.mode    = MODE_FUNC;
                m.wave    = cfg.wave;
                m.freqHz  = cfg.freqHz;
                m.ampMv   = cfg.ampMv;
                m.running = cfg.running;
            }
        }

        /* ---- Publish under mutex --------------------------------------- */
        if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
            sharedMeas = m;
            xSemaphoreGive(dataMutex);
        }

        /* ---- Send to LCD at 10 Hz max ---------------------------------- */
        /*
         * ADC samples arrive at 1000 Hz but the LCD takes ~2 ms per full
         * refresh. Sending every sample floods lcdQueue and causes the LCD
         * to be writing continuously, which corrupts cursor positioning.
         * Rate-limit to every 100 samples (100 ms = 10 Hz) — fast enough
         * for readable live values, slow enough for the LCD to keep up.
         */
        static uint32_t lcdSampleCount = 0;
        if (++lcdSampleCount >= 20) {
            lcdSampleCount = 0;
            LcdMsg_t lcd;
            buildLcdMsg(&m, &lcd);
            /* Overwrite any unread frame — always show latest value */
            xQueueOverwrite(lcdQueue, &lcd);
        }

        /* uartQueue is no longer used for metrics — UART_task is command-only */
    }
}
