/*
 *******************************************************************************
 * task_uart.c
 * UART_task — command shell only. No metrics display.
 *
 * Responsibilities:
 *   - Receive characters from LPUART1 ISR into a ring buffer
 *   - Parse complete lines into UartCmd_t and forward to cmdQueue
 *   - Handle HELP and STATUS locally with a simple print
 *   - Print ACK/ERR responses from mode_manager_task (via ackQueue)
 *
 * What it does NOT do anymore:
 *   - Display voltage, waveform metrics, or any live data
 *   - Refresh a dashboard
 *   - Read from uartQueue (that queue is now unused and can be removed)
 *
 * Terminal behaviour:
 *   - Prints a prompt "> " after each response so the user knows it is ready
 *   - Echoes received characters back so the user can see what they typed
 *     (echo happens in the ISR push path)
 *******************************************************************************
 */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "app_types.h"
#include "lpuart.h"

/* --------------------------------------------------------------------------
 * RX ring buffer — filled one byte at a time from LPUART1_IRQHandler
 * --------------------------------------------------------------------------*/
#define RX_BUF_SIZE     64u
#define RX_LINE_SIZE    32u

static volatile char     rxRingBuf[RX_BUF_SIZE];
static volatile uint32_t rxHead    = 0;
static volatile uint32_t rxTail    = 0;
static volatile uint8_t  lineReady = 0;
static char     rxLine[RX_LINE_SIZE];
static uint32_t rxLineIdx = 0;

/*
 * Called from LPUART1_IRQHandler every time a byte arrives.
 * Stores into the ring buffer and flags lineReady on newline.
 * Also echoes the character back so the user sees their own typing.
 */
void rxRingBufPush(char c) {
    /* Echo back — lets the user see what they typed */
    char echo[2] = { c, '\0' };
    LPUART_print(echo);

    /* Newline: flag line ready, don't store the newline itself */
    if (c == '\r' || c == '\n') {
        lineReady = 1;
        LPUART_print("\r\n");   /* move terminal to next line */
        return;
    }

    /* Backspace support */
    if (c == '\b' || c == 0x7F) {
        if (rxLineIdx > 0) rxLineIdx--;
        return;
    }

    uint32_t nextHead = (rxHead + 1) % RX_BUF_SIZE;
    if (nextHead != rxTail) {
        rxRingBuf[rxHead] = c;
        rxHead = nextHead;
    }
}

static void drainRxToLine(void) {
    while (rxTail != rxHead) {
        char c = rxRingBuf[rxTail];
        rxTail = (rxTail + 1) % RX_BUF_SIZE;
        if (rxLineIdx < RX_LINE_SIZE - 1)
            rxLine[rxLineIdx++] = c;
    }
    rxLine[rxLineIdx] = '\0';
    rxLineIdx = 0;
}

/* --------------------------------------------------------------------------
 * Simple print helpers
 * --------------------------------------------------------------------------*/
static void println(const char *s) {
    LPUART_print(s);
    LPUART_print("\r\n");
}

static void prompt(void) {
    LPUART_print("> ");
}

/* --------------------------------------------------------------------------
 * printHelp()
 * --------------------------------------------------------------------------*/
static void printHelp(void) {
    println("");
    println("--- Commands ----------------------------");
    println("  MODE   <DC|AC|FOLLOW|FUNC>");
    println("  WAVE   <SINE|SQUARE|TRI|SAW>");
    println("  FREQ   <Hz>   0 < f <= 100 Hz");
    println("  AMP    <mV>   0 <= amp <= 3300 mV");

    println("  START  (FUNC or FOLLOW mode only)");
    println("  STOP");
    println("  STATUS");
    println("  HELP");
    println("-----------------------------------------");
    println("  ADC input:  0 V <= Vin <= 3.3 V");
    println("  DAC output: 0 V <= Vout <= 3.3 V");
    println("  AC input must be biased to 1.65 V");
    println("-----------------------------------------");
}

/* --------------------------------------------------------------------------
 * printStatus()
 * Reads current config directly from configQueue — no stale snapshot needed.
 * --------------------------------------------------------------------------*/
static void printStatus(void) {
    SystemConfig_t cfg;
    if (xQueuePeek(configQueue, &cfg, pdMS_TO_TICKS(10)) != pdTRUE) {
        println("ERR: config unavailable");
        return;
    }

    const char *modeStr[] = {"DC","AC","FOLLOW","FUNC"};
    const char *waveStr[] = {"SINE","SQUARE","TRI","SAW"};
    char buf[56];

    println("");
    println("=== STATUS ==============================");
    snprintf(buf, sizeof(buf), "  Mode   : %s",     modeStr[cfg.mode]);    println(buf);
    snprintf(buf, sizeof(buf), "  Wave   : %s",     waveStr[cfg.wave]);    println(buf);
    snprintf(buf, sizeof(buf), "  Freq   : %lu Hz", (unsigned long)cfg.freqHz);  println(buf);
    snprintf(buf, sizeof(buf), "  Amp    : %lu mV", (unsigned long)cfg.ampMv);   println(buf);
    snprintf(buf, sizeof(buf), "  Offset : %lu mV", (unsigned long)cfg.offsetMv);println(buf);
    snprintf(buf, sizeof(buf), "  Running: %s",     cfg.running ? "YES" : "NO"); println(buf);
    println("=========================================");
}

/* --------------------------------------------------------------------------
 * Command parser
 * --------------------------------------------------------------------------*/
static void toUpperInPlace(char *s) {
    while (*s) { *s = (char)toupper((unsigned char)*s); s++; }
}

static int parseLine(const char *line, UartCmd_t *cmd) {
    char buf[RX_LINE_SIZE];
    strncpy(buf, line, RX_LINE_SIZE - 1);
    buf[RX_LINE_SIZE - 1] = '\0';
    toUpperInPlace(buf);

    char *verb = strtok(buf, " \t");
    char *arg  = strtok(NULL, " \t");

    if (!verb || verb[0] == '\0') return 0;

    cmd->value = 0;
    cmd->mode  = MODE_DC;
    cmd->wave  = WAVE_SINE;

    if (strcmp(verb, "MODE") == 0) {
        cmd->type = CMD_MODE;
        if (!arg) return 0;
        if      (strcmp(arg, "DC")     == 0) cmd->mode = MODE_DC;
        else if (strcmp(arg, "AC")     == 0) cmd->mode = MODE_AC;
        else if (strcmp(arg, "FOLLOW") == 0) cmd->mode = MODE_FOLLOW;
        else if (strcmp(arg, "FUNC")   == 0) cmd->mode = MODE_FUNC;
        else return 0;

    } else if (strcmp(verb, "WAVE") == 0) {
        cmd->type = CMD_WAVE;
        if (!arg) return 0;
        if      (strcmp(arg, "SINE")   == 0) cmd->wave = WAVE_SINE;
        else if (strcmp(arg, "SQUARE") == 0) cmd->wave = WAVE_SQUARE;
        else if (strcmp(arg, "TRI")    == 0) cmd->wave = WAVE_TRIANGLE;
        else if (strcmp(arg, "SAW")    == 0) cmd->wave = WAVE_SAWTOOTH;
        else return 0;

    } else if (strcmp(verb, "FREQ")   == 0) {
        cmd->type = CMD_FREQ;
        if (!arg) return 0;
        int fv = atoi(arg); if (fv <= 0) { cmd->type = CMD_UNKNOWN; return 1; }
        cmd->value = (uint32_t)fv;
    } else if (strcmp(verb, "AMP")    == 0) {
        cmd->type = CMD_AMP;
        if (!arg) return 0;
        int av = atoi(arg); if (av < 0) { cmd->type = CMD_UNKNOWN; return 1; }
        cmd->value = (uint32_t)av;
    }

    else if   (strcmp(verb, "START")  == 0) { cmd->type = CMD_START;  }
    else if   (strcmp(verb, "STOP")   == 0) { cmd->type = CMD_STOP;   }
    else if   (strcmp(verb, "STATUS") == 0) { cmd->type = CMD_STATUS; }
    else if   (strcmp(verb, "HELP")   == 0) { cmd->type = CMD_HELP;   }
    else { cmd->type = CMD_UNKNOWN; }

    return 1;
}

/* --------------------------------------------------------------------------
 * UART_task
 * --------------------------------------------------------------------------*/
void UART_task(void *pvParams) {
    (void)pvParams;

    println("");
    println("FreeRTOS Multimeter ready.");
    println("Type HELP for commands.");
    prompt();

    for (;;) {
        /*
         * Block until a complete line is ready.
         * Poll every 20 ms — fast enough to feel responsive,
         * slow enough to not burn CPU.
         */
        if (!lineReady) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        lineReady = 0;
        drainRxToLine();

        if (rxLine[0] == '\0') {
            /* Empty line — just reprint prompt */
            prompt();
            continue;
        }

        UartCmd_t cmd;
        if (!parseLine(rxLine, &cmd)) {
            println("ERR: unknown command -- type HELP");
            prompt();
            memset(rxLine, 0, sizeof(rxLine));
            continue;
        }

        switch (cmd.type) {
            case CMD_HELP:
                printHelp();
                break;

            case CMD_STATUS:
                printStatus();
                break;

            case CMD_UNKNOWN:
                println("ERR: unknown command -- type HELP");
                break;

            default:
                /* Forward to mode_manager_task */
                if (xQueueSend(cmdQueue, &cmd, pdMS_TO_TICKS(50)) != pdTRUE)
                    println("ERR: system busy, try again");
                break;
        }

        memset(rxLine, 0, sizeof(rxLine));
        prompt();
    }
}
