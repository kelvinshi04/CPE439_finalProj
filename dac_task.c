/*
 *******************************************************************************
 * task_dac.c
 * DAC_task — highest priority waveform / follower output driver.
 *
 * Uses your DAC driver:
 *   DAC_write(uint16_t data)         — raw 12-bit SPI write to MCP4921
 *   DAC_volt_conv(uint32_t voltage)  — millivolts → DAC word
 *   VOLTAGE_RAIL  3300               — 3.3 V reference
 *   CONTROL_BITS  0x1000             — MCP4921 config bits (already OR'd in
 *                                      DAC_volt_conv / DAC_write as needed)
 *
 * Waveform generation:
 *   256-entry lookup tables are built once at startup (sine computed,
 *   others computed analytically). The phase accumulator advances by
 *   phaseStep each tick, wrapping via LUT_MASK.
 *
 *   phaseStep = freqHz * LUT_SIZE / SAMPLE_RATE_HZ
 *   (set by mode_manager_task whenever FREQ changes)
 *
 * Output scaling:
 *   LUT entries are normalised 0–4095 (full 12-bit range).
 *   scaleSample() maps them to the requested amplitude + offset using
 *   DAC_volt_conv() to stay in millivolt units throughout.
 *
 * Timing:
 *   vTaskDelayUntil gives a stable, drift-free sample period.
 *   SAMPLE_PERIOD_TICKS = configTICK_RATE_HZ / SAMPLE_RATE_HZ.
 *   At 1 kHz FreeRTOS tick and 10 kHz sample rate this is 0 — meaning
 *   the task yields but can be scheduled every tick. For lower sample
 *   rates (e.g. 1 kHz output) set SAMPLE_RATE_HZ accordingly in app_types.h.
 *******************************************************************************
 */

#include <math.h>
#include "app_types.h"
#include "dac.h"

/* --------------------------------------------------------------------------
 * Lookup table storage
 * --------------------------------------------------------------------------*/
static uint16_t lut[WAVE_COUNT][LUT_SIZE];

/* --------------------------------------------------------------------------
 * buildLUTs()
 * Pre-compute all four waveform tables once at startup.
 * All entries are raw 12-bit DAC words (0–4095, full-scale normalised).
 * --------------------------------------------------------------------------*/
static void buildLUTs(void) {
    for (uint32_t i = 0; i < LUT_SIZE; i++) {

        /* Sine: sin maps −1..1 → scale to 0..4095 */
        float s = sinf((2.0f * 3.14159265f * (float)i) / (float)LUT_SIZE);
        lut[WAVE_SINE][i] = (uint16_t)((s * 0.5f + 0.5f) * 4095.0f);

        /* Square: high for first half, low for second half */
        lut[WAVE_SQUARE][i] = (i < LUT_SIZE / 2) ? 4095u : 0u;

        /* Triangle: ramp up then ramp down */
        if (i < LUT_SIZE / 2)
            lut[WAVE_TRIANGLE][i] = (uint16_t)((float)i / (LUT_SIZE / 2.0f) * 4095.0f);
        else
            lut[WAVE_TRIANGLE][i] = (uint16_t)((2.0f - (float)i / (LUT_SIZE / 2.0f)) * 4095.0f);

        /* Sawtooth: ramp from 0 to 4095 */
        lut[WAVE_SAWTOOTH][i] = (uint16_t)((float)i / (float)(LUT_SIZE - 1) * 4095.0f);
    }
}

/* --------------------------------------------------------------------------
 * scaleSample()
 * Maps a normalised LUT value (0–4095) to a DAC word.
 * Offset is fixed at DEFAULT_OFFSET_MV (1650 mV) — centred on the rail.
 *
 *   Vout = (lutVal / 4095.0) * ampMv + (DEFAULT_OFFSET_MV - ampMv/2)
 *
 * Clamps to [0, VOLTAGE_RAIL] before converting to DAC word.
 * --------------------------------------------------------------------------*/
static uint16_t scaleSample(uint16_t lutVal, uint32_t ampMv) {
    float vCentre = ((float)lutVal / 4095.0f - 0.5f) * (float)ampMv;
    float vOut    = vCentre + (float)DEFAULT_OFFSET_MV;

    if (vOut < 0.0f)         vOut = 0.0f;
    if (vOut > VOLTAGE_RAIL) vOut = (float)VOLTAGE_RAIL;

    return DAC_volt_conv((uint32_t)vOut);
}

/* --------------------------------------------------------------------------
 * DAC_task
 * --------------------------------------------------------------------------*/
void DAC_task(void *pvParams) {
    (void)pvParams;

    /* Build all waveform lookup tables */
    buildLUTs();

    /* Output 0 V (silence) on startup */
    DAC_write(DAC_volt_conv(0));

    SystemConfig_t cfg = {
        .mode      = MODE_DC,
        .wave      = WAVE_SINE,
        .freqHz    = DEFAULT_FREQ_HZ,
        .ampMv     = DEFAULT_AMP_MV,
        .offsetMv  = DEFAULT_OFFSET_MV,
        .phaseStep = (DEFAULT_FREQ_HZ * LUT_SIZE) / SAMPLE_RATE_HZ,
        .running   = 0,
    };

    uint32_t phase = 0;
    TickType_t xLastWakeTime = xTaskGetTickCount();

    for (;;) {
        /*
         * Pull latest config — non-blocking peek.
         * configQueue depth is 1 with overwrite semantics, so this always
         * gets the most recent command without consuming it (DAC_task is
         * the only reader so xQueueReceive would also work).
         */
        xQueuePeek(configQueue, &cfg, 0);

        uint16_t dacWord = DAC_volt_conv(0);    /* default: output 0 */

        if (cfg.mode == MODE_FUNC && cfg.running) {
            /* Advance phase accumulator */
            phase = (phase + cfg.phaseStep) & LUT_MASK;
            dacWord = scaleSample(lut[cfg.wave][phase], cfg.ampMv);

        } else if (cfg.mode == MODE_FOLLOW) {
            DacSample_t s;
            if (xQueuePeek(followQueue, &s, 0) == pdTRUE)
                dacWord = s.dacWord;
            /* If followQueue is empty, hold last output — dacWord stays 0
             * only on the very first iteration, which is safe. */

        } else {
            /* DC / AC meter modes: DAC output silent */
            dacWord = DAC_volt_conv(0);
            phase   = 0;    /* reset phase so FUNC mode starts cleanly */
        }

        DAC_write(dacWord);

        /*
         * vTaskDelayUntil provides a stable, drift-corrected period.
         * If the task body takes longer than SAMPLE_PERIOD_TICKS, the next
         * wake will be scheduled immediately (no accumulating lag).
         *
         * If SAMPLE_PERIOD_TICKS == 0 (sample rate >= tick rate), the task
         * still yields via taskYIELD() to prevent starving lower-priority
         * tasks on every iteration.
         */
#if SAMPLE_PERIOD_TICKS > 0
        vTaskDelayUntil(&xLastWakeTime, SAMPLE_PERIOD_TICKS);
#else
        taskYIELD();
        xLastWakeTime = xTaskGetTickCount();
#endif
    }
}
