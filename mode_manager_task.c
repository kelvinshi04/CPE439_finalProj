/*
 *******************************************************************************
 * task_mode_manager.c
 * mode_manager_task — sole authority over system configuration.
 *
 * Receives UartCmd_t from cmdQueue (sent by UART_task after parsing).
 * Updates the local SystemConfig_t, sets event group mode bits, and
 * pushes the updated config to configQueue (overwrite — latest wins).
 *
 * This task is the ONLY writer to configQueue and the ONLY task that
 * calls xEventGroupSetBits / xEventGroupClearBits on mode bits.
 * That single-writer rule eliminates all races on those resources.
 *
 * phaseStep is pre-computed here whenever frequency changes, so
 * DAC_task can use it directly without division in its hot loop.
 *******************************************************************************
 */

#include <stdio.h>
#include <string.h>
#include "app_types.h"
#include "lpuart.h"

/* --------------------------------------------------------------------------
 * calcPhaseStep()
 * phaseStep = freqHz * LUT_SIZE / SAMPLE_RATE_HZ
 * Integer arithmetic; result is how many LUT entries to advance per sample.
 * --------------------------------------------------------------------------*/
static inline uint32_t calcPhaseStep(uint32_t freqHz) {
    return (freqHz * LUT_SIZE) / SAMPLE_RATE_HZ;
}

/* --------------------------------------------------------------------------
 * printAck()
 * Sends a short acknowledgement string over LPUART after each command.
 * Keeps the user informed without cluttering the dashboard.
 * --------------------------------------------------------------------------*/
static void printAck(const char *msg) {
    LPUART_print(msg);
    LPUART_print("\r\n");
}

/* --------------------------------------------------------------------------
 * mode_manager_task
 * --------------------------------------------------------------------------*/
void mode_manager_task(void *pvParams) {
    (void)pvParams;

    /* Local config — this task owns it. Everyone else reads via configQueue. */
    SystemConfig_t cfg = {
        .mode      = MODE_DC,
        .wave      = WAVE_SINE,
        .freqHz    = DEFAULT_FREQ_HZ,
        .ampMv     = DEFAULT_AMP_MV,
        .offsetMv  = DEFAULT_OFFSET_MV,
        .phaseStep = (DEFAULT_FREQ_HZ * LUT_SIZE) / SAMPLE_RATE_HZ,
        .running   = 0,
    };

    UartCmd_t cmd;
    char ackBuf[48];

    for (;;) {
        /* Block until UART_task delivers a parsed command */
        xQueueReceive(cmdQueue, &cmd, portMAX_DELAY);

        switch (cmd.type) {

            /* -------------------------------------------------------------- */
            case CMD_MODE:
                cfg.mode    = cmd.mode;
                cfg.running = 0;    /* always stop output on mode switch */

                /* Clear all mode bits then set the new one atomically */
                xEventGroupClearBits(eventGroup, EV_ALL_MODES | EV_RUNNING);
                switch (cfg.mode) {
                    case MODE_DC:     xEventGroupSetBits(eventGroup, EV_MODE_DC);     break;
                    case MODE_AC:     xEventGroupSetBits(eventGroup, EV_MODE_AC);     break;
                    case MODE_FOLLOW: xEventGroupSetBits(eventGroup, EV_MODE_FOLLOW); break;
                    case MODE_FUNC:   xEventGroupSetBits(eventGroup, EV_MODE_FUNC);   break;
                }

                snprintf(ackBuf, sizeof(ackBuf), "OK MODE %d",  (int)cfg.mode);
                printAck(ackBuf);
                break;

            /* -------------------------------------------------------------- */
            case CMD_WAVE:
                /* Guard against out-of-bounds LUT index */
                if (cmd.wave >= WAVE_COUNT) {
                    printAck("ERR invalid wave type");
                    continue;
                }
                cfg.wave = cmd.wave;
                {
                    const char *wNames[] = {"SINE","SQUARE","TRI","SAW"};
                    snprintf(ackBuf, sizeof(ackBuf), "OK WAVE %s", wNames[cfg.wave]);
                }
                printAck(ackBuf);
                break;

            /* -------------------------------------------------------------- */
            case CMD_FREQ:
                /*
                 * Lower bound: 1 Hz minimum.
                 * Upper bound: SAMPLE_RATE_HZ / 4 = 250 Hz at 1 kHz.
                 *   Above this phaseStep skips too many LUT entries and
                 *   waveform quality degrades severely. Hard cap at 100 Hz
                 *   for clean output (phaseStep=25, ~10 points per cycle).
                 * atoi() returns int — cmd.value is uint32_t so a negative
                 *   input like "FREQ -5" wraps to a huge number, caught here.
                 */
                if (cmd.value == 0 || cmd.value > 100) {
                    printAck("ERR FREQ must be 1-100 Hz");
                    continue;
                }
                cfg.freqHz    = cmd.value;
                cfg.phaseStep = calcPhaseStep(cfg.freqHz);
                snprintf(ackBuf, sizeof(ackBuf), "OK FREQ %lu Hz",
                         (unsigned long)cfg.freqHz);
                printAck(ackBuf);
                break;

            /* -------------------------------------------------------------- */
            case CMD_AMP:
                /*
                 * Offset is fixed at DEFAULT_OFFSET_MV (1650 mV).
                 * Clamp so neither peak exceeds the rail:
                 *   1650 + AMP/2 <= 3300  ->  AMP <= 3300 mV
                 *   1650 - AMP/2 >= 0     ->  AMP <= 3300 mV
                 * Both reduce to AMP <= 3300, but practically AMP <= 3000
                 * keeps 150 mV headroom on each side.
                 */
                if (cmd.value > 3000) {
                    printAck("ERR AMP max 3000 mV (150mV headroom each side)");
                    continue;
                }
                cfg.ampMv = cmd.value;
                snprintf(ackBuf, sizeof(ackBuf), "OK AMP %lu mV",
                         (unsigned long)cfg.ampMv);
                printAck(ackBuf);
                break;



            /* -------------------------------------------------------------- */
            case CMD_START:
                if (cfg.mode != MODE_FUNC && cfg.mode != MODE_FOLLOW) {
                    printAck("ERR START only valid in FUNC or FOLLOW mode");
                    continue;
                }
                cfg.running = 1;
                xEventGroupSetBits(eventGroup, EV_RUNNING);
                printAck("OK START");
                break;

            /* -------------------------------------------------------------- */
            case CMD_STOP:
                cfg.running = 0;
                xEventGroupClearBits(eventGroup, EV_RUNNING);
                printAck("OK STOP");
                break;

            /* -------------------------------------------------------------- */
            case CMD_STATUS:
            case CMD_HELP:
                /* STATUS and HELP are handled entirely in UART_task
                 * (they only need to print, not change config).
                 * They should not reach here, but guard defensively. */
                break;

            /* -------------------------------------------------------------- */
            case CMD_UNKNOWN:
            default:
                printAck("ERR unknown command — type HELP");
                break;
        }

        /*
         * Push updated config to DAC_task.
         * xQueueOverwrite: always succeeds, no blocking, replaces any
         * unread entry. DAC_task uses xQueuePeek so config is never consumed.
         *
         * Note: we call this on every command, even ones that don't affect
         * the DAC, to keep configQueue consistent with the local cfg struct.
         */
        xQueueOverwrite(configQueue, &cfg);
    }
}
