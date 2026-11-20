

/*******************************************************************************
 * Title                 :   Compressor and BMS operations
 * Filename              :   bms_comp_daq.c
 * Target                :   esp32 (esp idf)
 * Notes                 :   Flow:
 *   SEL(GPIO32)=HIGH, 9600 : send BMS request, wait for a COMPLETE DD 03 xx LL frame.
 *        no valid frame in QUERY_RESPONSE_TIME -> retry, max 3 attempts -> go to compressor
 *   SEL(GPIO32)=LOW, 2400  : listen only for AA 13 00 frame (38 bytes), nothing is sent.
 *        no valid frame in window -> retry, max 3 attempts -> go to BMS
 *   A valid frame at any point also switches to the other device immediately.
 *
 *   BMS FIX: the BMS frame is now only accepted when ALL bytes have arrived
 *   (length taken from the length byte), the stop byte 0x77 is present and the
 *   checksum is correct. Before, the code switched to the compressor as soon as
 *   the header + 1 byte arrived, and uart_flush_input() dropped the rest of the
 *   frame (that is why only ~43 bytes were seen).
 *******************************************************************************/
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "bms_comp_daq.h"

/************************************
 * BMS FRAME SETTINGS
 ************************************/
/* JBD frame: DD | 03 | status | len | data[len] | chkH | chkL | 77
 * total bytes = 4 + len + 3  (len = 0x2A = 42  ->  49 bytes)                      */
#define BMS_FRAME_OVERHEAD 7 /* 4 header bytes + 2 checksum + 1 stop byte        */
#define BMS_STOP_BYTE 0x77
#define BMS_CHECK_STOP 1 /* 1 = require last byte == 0x77, 0 = ignore it   */
#define BMS_CHK_STRICT 1 /* 1 = reject frame on bad checksum, 0 = only log   */

/* RX buffer must hold a full BMS frame (49 bytes) - 64 or more is needed */
_Static_assert(UART0_RXBUFF_SIZE >= 64, "UART0_RXBUFF_SIZE must be >= 64");

/************************************
 * GLOBAL VARIABLES
 ************************************/
uint8_t ubCompCon_Cnt = 0;
uint8_t ubBMSCon_Cnt = 0;
uint8_t aCompBuf[UART0_RXBUFF_SIZE] = {0};
uint8_t aBMSBuf[UART0_RXBUFF_SIZE] = {0};
uint8_t aCompFrame[COMP_FRAME_LEN] = {0}; /* last valid 38-byte compressor frame */
utUART0Mux_t utUART0Mux;
stBMS_Data_t stBMS_Data;
stComp_Data_t stComp_Data;

uint8_t BMS_soc;
uint8_t BMS_lds;
uint16_t BMS_Cycles;
uint16_t BMS_volt;
uint16_t BMS_cur;
uint16_t BMS_faults;
uint8_t BMS_soh;
uint16_t Cds = 0;
uint16_t Bfl = 0;
char Stt[] = "NORMAL";
uint16_t Flt = 0;

int Tco = 0;
float Dcv = 0.0f;
 uint8_t compresser_spd = 0;

uint8_t compresser_voltage = 0;

uint8_t compresser_current = 0;

uint8_t compresser_temp = 0;

float Cur = 0.0f;

/************************************
 * STATIC VARIABLES
 ************************************/
static const uint8_t aBMSHdr[BMS_HDR_LEN] = {0xDD, 0x03, 0x00, 0x2A};
static const uint8_t aCompHdr[COMP_HDR_LEN] = {0xAA, 0x13, 0x01};
// static const uint8_t aCompHdr[COMP_HDR_LEN] = {0xAA, 0x4D, 0x03};

/* BMS request frame - JBD style "read basic info". CHANGE if your BMS uses another request */
static const uint8_t aBMSQuery[] = {0xDD, 0xA5, 0x03, 0x00, 0xFF, 0xFD, 0x77};

/* Compressor MASTER frame (ESP32 -> driver). *** REPLACE with the real frame from the driver datasheet,
 * including its CRC/checksum. The driver stays silent unless it receives a valid frame. *** */
#if COMP_SEND_QUERY
static const uint8_t aCompQuery[] = {0x00 /* TODO: real compressor request frame */};
#endif

static uint32_t ulCompFrameCnt = 0; /* frames received (header + 38 bytes) */
static uint32_t ulCompChkOk = 0;    /* checksum good */
static uint32_t ulCompChkFail = 0;  /* checksum bad  */

static uint32_t ulBMSFrameCnt = 0; /* complete BMS frames received */
static uint32_t ulBMSChkOk = 0;    /* BMS frames accepted           */
static uint32_t ulBMSChkFail = 0;  /* BMS frames rejected           */

static uint16_t uhwRxLen = 0;
static TickType_t last_query_tick = 0;
static TickType_t last_rx_tick = 0;

/************************************
 * STATIC FUNCTION PROTOTYPES
 ************************************/
static void vSwitchChannel(uint8_t sel, int baud, uart_parity_t parity);
static void vRxPoll(const char *label, uint8_t *buf, const uint8_t *hdr, uint8_t hdrLen);
static void vClearRx(uint8_t *buf);
static void vSendCompQuery(void);
static void vSendBMSQuery(void);
static void vcfg_uart0_Comp(void);
static void vcfg_uart0_BMS(void);
static uint16_t uhBMS_FrameLen(const uint8_t *b, uint16_t have);

static void vBMS_ZeroData(void);
static void vComp_ZeroData(void);
/* Wire disconnected / no valid frame -> all BMS values = 0 */
static void vBMS_ZeroData(void)
{
    memset(aBMSBuf, 0, sizeof(aBMSBuf));
    memset(&stBMS_Data, 0, sizeof(stBMS_Data));

    BMS_volt = 0;
    BMS_cur = 0;
    BMS_Cycles = 0;
    BMS_soh = 0;
    BMS_faults = 0;
    BMS_soc = 0;
    BMS_lds = 0;

    utUART0Mux.flags.bBMSUpdate_success = 0;
    uhwRxLen = 0;
}

/* Wire disconnected / no valid frame -> all compressor values = 0 */
static void vComp_ZeroData(void)
{
    memset(aCompBuf, 0, sizeof(aCompBuf));
    memset(aCompFrame, 0, sizeof(aCompFrame));
    memset(&stComp_Data, 0, sizeof(stComp_Data));

    compresser_spd = 0;
    /* add any other values you fill from the compressor frame, e.g.: */
    // Tco = 0;  Dcv = 0.0f;  Cur = 0.0f;

    utUART0Mux.flags.bCompUpdate_success = 0;
    uhwRxLen = 0;
}

/************************************
 * DEBUG PORT (UART2)
 ************************************/
static bool bDbgReady = false;

/* Sends ESP_LOGx / printf-style output to UART2 instead of UART0 */
// static int vDbg_vprintf(const char *fmt, va_list args)
// {
//     char buf[160];
//     int n = vsnprintf(buf, sizeof(buf), fmt, args);
//     if (n <= 0 || !bDbgReady)
//         return n;
//     if (n >= (int)sizeof(buf))
//         n = sizeof(buf) - 1;
//     uart_write_bytes(DBG_UART, buf, n);
//     return n;
// }

void vDbg_Printf(const char *fmt, ...)
{
    if (!bDbgReady)
        return;
    char buf[256];
    int len = snprintf(buf, sizeof(buf), "[%lu] ", (unsigned long)(xTaskGetTickCount() * portTICK_PERIOD_MS));
    va_list ap;
    va_start(ap, fmt);
    len += vsnprintf(buf + len, sizeof(buf) - len, fmt, ap);
    va_end(ap);
    if (len >= (int)sizeof(buf))
        len = sizeof(buf) - 1;
    uart_write_bytes(DBG_UART, buf, len);
}

/* Whole array on one line. Writes straight to UART2 with its own big buffer,
 * so the line is NOT truncated by the 160-byte buffer in vDbg_Printf(). */
void vDbg_HexDump(const char *label, const uint8_t *buf, uint16_t len)
{
    static char line[(UART0_RXBUFF_SIZE * 3) + 64];

    if (!bDbgReady)
        return;

    if (len > UART0_RXBUFF_SIZE)
        len = UART0_RXBUFF_SIZE;

    int pos = snprintf(line, sizeof(line), "[%lu] %s RX[%u]: ",
                       (unsigned long)(xTaskGetTickCount() * portTICK_PERIOD_MS), label, len);
    for (uint16_t i = 0; i < len && pos < (int)sizeof(line) - 4; i++)
    {
        pos += snprintf(line + pos, sizeof(line) - pos, "%02X ", buf[i]);
    }
    pos += snprintf(line + pos, sizeof(line) - pos, "\r\n");
    if (pos >= (int)sizeof(line))
        pos = sizeof(line) - 1;
    uart_write_bytes(DBG_UART, line, pos);
}

/* Whole array on one line, e.g.  [5230] COMP[38]: AA 13 00 29 ... F2  (max 80 bytes) */
void vDbg_HexLine(const char *label, const uint8_t *buf, uint16_t len)
{
    static char line[3 * 80 + 48];
    if (!bDbgReady)
        return;
    if (len > 80)
        len = 80;
    int pos = snprintf(line, sizeof(line), "[%lu] %s[%u]: ",
                       (unsigned long)(xTaskGetTickCount() * portTICK_PERIOD_MS), label, len);
    for (uint16_t i = 0; i < len; i++)
    {
        pos += snprintf(line + pos, sizeof(line) - pos, "%02X ", buf[i]);
    }
    pos += snprintf(line + pos, sizeof(line) - pos, "\r\n");
    uart_write_bytes(DBG_UART, line, pos);
}

void vDbg_Init(void)
{
    if (bDbgReady)
        return;
    uart_config_t cfg = {
        .baud_rate = DBG_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };
    uart_param_config(DBG_UART, &cfg);
    uart_set_pin(DBG_UART, DBG_TX_PIN, DBG_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(DBG_UART, 256, 1024, 0, NULL, 0); /* TX buffer so prints don't block */
    bDbgReady = true;
    // esp_log_set_vprintf(vDbg_vprintf); /* ESP_LOGx now goes to UART2 too */
    //  DBG_PRINT("Debug UART2 ready\r\n");
}

/************************************
 * STATIC FUNCTIONS
 ************************************/
/* Wait for TX to finish, flip the mux, change baud, drop stale RX bytes */
static void vSwitchChannel(uint8_t sel, int baud, uart_parity_t parity)
{
    uart_wait_tx_done(UART_NUM_0, pdMS_TO_TICKS(50));
    gpio_set_level(COMP_BMS_SEL, sel);
    uart_set_baudrate(UART_NUM_0, baud);
    uart_set_parity(UART_NUM_0, parity); /* compressor = 8E1, BMS = 8N1 */
    vTaskDelay(pdMS_TO_TICKS(5));        /* let the mux/line settle */
    uart_flush_input(UART_NUM_0);
}

static void vClearRx(uint8_t *buf)
{
    memset(buf, 0, UART0_RXBUFF_SIZE);
    uhwRxLen = 0;
    last_rx_tick = xTaskGetTickCount();
}

/* Read whatever is in the driver FIFO, then align buffer so it starts at the header.
 * Garbage before the header is discarded; a partial header at the tail is kept. */
static void vRxPoll(const char *label, uint8_t *buf, const uint8_t *hdr, uint8_t hdrLen)
{
    int n = uart_read_bytes(UART_NUM_0, buf + uhwRxLen, UART0_RXBUFF_SIZE - uhwRxLen, 0);
    if (n > 0)
    {
        uhwRxLen += n;
        last_rx_tick = xTaskGetTickCount();
        // DBG_HEX(label, buf + uhwRxLen - n, n); /* raw bytes exactly as received, before header sync */
    }

    if (uhwRxLen == 0)
        return;

    for (int off = 0; (off + hdrLen) <= uhwRxLen; off++)
    {
        if (memcmp(buf + off, hdr, hdrLen) == 0)
        {
            if (off)
            {
                memmove(buf, buf + off, uhwRxLen - off);
                uhwRxLen -= off;
            }
            return; /* header at buf[0] */
        }
    }
    /* header not found: keep only a possible partial header at the tail */
    uint16_t keep = (uhwRxLen >= hdrLen) ? (hdrLen - 1) : uhwRxLen;
    memmove(buf, buf + uhwRxLen - keep, keep);
    uhwRxLen = keep;
}

static void vcfg_uart0_Comp(void)
{
    vSwitchChannel(COMP_MODE, COMP_BAUD, COMP_PARITY);
    utUART0Mux.ubMode = 0;
    utUART0Mux.flags.bCompTime = 1;
    ubCompCon_Cnt = 0;
    vClearRx(aCompBuf);
    vSendCompQuery();
    // DBG_PRINT("SEL=LOW  -> COMP, %d baud, listening (window 1/%d)\r\n", COMP_BAUD, MAX_TRIES);
}

static void vcfg_uart0_BMS(void)
{
    vSwitchChannel(BMS_MODE, BMS_BAUD, BMS_PARITY);
    utUART0Mux.ubMode = 0;
    utUART0Mux.flags.bBMSTime = 1;
    ubBMSCon_Cnt = 0;
    vClearRx(aBMSBuf);
    last_query_tick = xTaskGetTickCount();
    uart_write_bytes(UART_NUM_0, (const char *)aBMSQuery, sizeof(aBMSQuery));
    // DBG_PRINT("SEL=HIGH -> BMS, %d baud, query sent (attempt 1/%d)\r\n", BMS_BAUD, MAX_TRIES);
}

static void vSendBMSQuery(void)
{
    vClearRx(aBMSBuf);
    uart_flush_input(UART_NUM_0);
    last_query_tick = xTaskGetTickCount();
    uart_write_bytes(UART_NUM_0, (const char *)aBMSQuery, sizeof(aBMSQuery));
    // DBG_PRINT("BMS query resent (attempt %d/%d)\r\n", ubBMSCon_Cnt + 1, MAX_TRIES);
}

static void vSendCompQuery(void)
{
    vClearRx(aCompBuf);
    last_query_tick = xTaskGetTickCount(); /* start of a listen window */
#if COMP_SEND_QUERY
    uart_flush_input(UART_NUM_0);
    uart_write_bytes(UART_NUM_0, (const char *)aCompQuery, sizeof(aCompQuery));
#endif
}

/* Expected total BMS frame size, taken from the length byte (data[3]).
 * Returns 0 if not enough bytes yet to know, or if the length cannot fit the buffer. */
static uint16_t uhBMS_FrameLen(const uint8_t *b, uint16_t have)
{
    if (have < BMS_HDR_LEN)
        return 0;
    uint16_t total = (uint16_t)(BMS_FRAME_OVERHEAD + b[3]);
    if (total > UART0_RXBUFF_SIZE)
        return 0;
    return total;
}

/************************************
 * PARSERS (header already validated by caller, but re-checked here)
 ************************************/
/* Needs a COMPLETE frame in data[] (caller checks uhBMS_FrameLen) */
uint8_t utParse_BMS_Data(uint8_t *data, stBMS_Data_t *parsed)
{
    if (data == NULL || parsed == NULL)
        return 0;

    /* header DD 03 00 2A */
    if (memcmp(data, aBMSHdr, BMS_HDR_LEN) != 0)
        return 0;

    uint8_t len = data[3];
    uint16_t total = (uint16_t)(BMS_FRAME_OVERHEAD + len);
    if (total > UART0_RXBUFF_SIZE)
        return 0;

    /* stop byte */
#if BMS_CHECK_STOP
    if (data[total - 1] != BMS_STOP_BYTE)
    {
        // DBG_PRINT("BMS stop byte FAIL rx=%02X\r\n", data[total - 1]);
        return 0;
    }
#endif

    /* JBD checksum = 0x10000 - (status + length + data bytes) */
    uint16_t sum = 0;
    for (int i = 2; i < 4 + len; i++)
        sum += data[i];
    uint16_t calc = (uint16_t)(0x10000 - sum);
    uint16_t rx = ((uint16_t)data[4 + len] << 8) | data[5 + len];
    if (calc != rx)
    {
        // DBG_PRINT("BMS checksum %s calc=%04X rx=%04X\r\n",
        //   BMS_CHK_STRICT ? "FAIL" : "mismatch (ignored)", calc, rx);
#if BMS_CHK_STRICT
        return 0;
#endif
    }

    /* Store BMS data directly in global variables */
    BMS_volt = ((uint16_t)data[4] << 8) | data[5];
    BMS_cur = ((uint16_t)data[6] << 8) | data[7];
    BMS_Cycles = ((uint16_t)data[12] << 8) | data[13];
    BMS_soh = data[17];
    BMS_faults = ((uint16_t)data[20] << 8) | data[21];
    BMS_soc = data[23];
    BMS_lds = data[24];

    return 1;
}

/* sum of bytes [1..36] modulo 256 */
static uint8_t ubComp_Checksum(const uint8_t *data)
{
    uint8_t sum = 0;
    for (int i = COMP_CHK_FIRST; i <= COMP_CHK_LAST; i++)
        sum += data[i];
    return sum;
}

uint8_t utParse_Comp_Data(uint8_t *data, stComp_Data_t *parsed)
{
    if (!data || !parsed)
        return 0;
    if (memcmp(data, aCompHdr, COMP_HDR_LEN) != 0)
        return 0; /* header AA 13 00 */

    uint8_t calc = ubComp_Checksum(data);
    if (calc != data[COMP_CHK_IDX])
    {
        // DBG_PRINT("COMP checksum FAIL calc=%02X rx=%02X\r\n", calc, data[COMP_CHK_IDX]);
        return 0;
    }

    // compresser_spd =
    //     ((uint16_t)data[3] << 8) | data[4];

    compresser_voltage =
        ((uint16_t)data[20] << 8) | data[21];

    compresser_current =
        ((uint16_t)data[22] << 8) | data[23];

    compresser_temp = data[26];

       compresser_spd = data[3];
    /* TODO: map compressor payload to stComp_Data_t fields here. */
    return 1;
}

/************************************
 * GLOBAL FUNCTIONS
 ************************************/
void vInit_tskUART0(void)
{
    // vDbg_Init();
    uart_config_t uart_config = {
        .baud_rate = BMS_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };
    uart_param_config(UART_NUM_0, &uart_config);
    uart_set_pin(UART_NUM_0, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE,
                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    esp_err_t err = uart_driver_install(UART_NUM_0, 512, 0, 0, NULL, 0); /* RX ring buffer must be > 128 */
    if (err != ESP_OK)
        // DBG_PRINT("UART0 driver install FAILED: %d\r\n", err);

        /* SEL line -> CD4052B pin 10 (A) */
        gpio_reset_pin(COMP_BMS_SEL);
    gpio_set_direction(COMP_BMS_SEL, GPIO_MODE_OUTPUT);

    memset(aCompBuf, 0, sizeof(aCompBuf));
    memset(aBMSBuf, 0, sizeof(aBMSBuf));
    memset(&stBMS_Data, 0, sizeof(stBMS_Data));
    memset(&stComp_Data, 0, sizeof(stComp_Data));

    vcfg_uart0_BMS(); /* start with BMS (SEL high) */
}

#if DBG_ENABLE
/* Prints the received checksum next to several common checksum formulas, for EVERY frame.
 * Use it to confirm (or find) the real rule if frames start failing. */
static void vComp_ChkDebug(const uint8_t *d)
{
    uint8_t s0 = 0, s1 = 0, s2 = 0, x0 = 0, x1 = 0;
    for (int i = 0; i <= COMP_CHK_LAST; i++)
    {
        s0 += d[i];
        x0 ^= d[i];
        if (i >= 1)
        {
            s1 += d[i];
            x1 ^= d[i];
        }
        if (i >= 2)
        {
            s2 += d[i];
        }
    }
    // DBG_PRINT("COMP chk rx=%02X | sum[1..36]=%02X sum[0..36]=%02X sum[2..36]=%02X xor[1..36]=%02X xor[0..36]=%02X neg[1..36]=%02X\r\n",
    //   d[COMP_CHK_IDX], s1, s0, s2, x1, x0, (uint8_t)(0x100 - s1));
}
#endif

/* Call periodically while SEL is LOW (compressor) */
void vdoCompTime(void)
{
    TickType_t now = xTaskGetTickCount();

    /* 1. Collect bytes as they arrive; garbage before AA 13 00 is dropped so the header sits at [0] */
    vRxPoll("COMP raw", aCompBuf, aCompHdr, COMP_HDR_LEN);

    /* 2. Frame is complete only when all 38 bytes (index 0..37) are in. Gaps inside the frame are tolerated. */
    if (uhwRxLen >= COMP_FRAME_LEN && memcmp(aCompBuf, aCompHdr, COMP_HDR_LEN) == 0)
    {
        utUART0Mux.flags.bCompUpdate = 1;
        ulCompFrameCnt++;
#if DBG_ENABLE
        vComp_ChkDebug(aCompBuf); /* checksum details for every frame */
#endif
        utUART0Mux.flags.bCompUpdate_success = utParse_Comp_Data(aCompBuf, &stComp_Data); /* header + checksum */
        if (utUART0Mux.flags.bCompUpdate_success)
        {
            memcpy(aCompFrame, aCompBuf, COMP_FRAME_LEN); /* store ONLY frames with good checksum */
            ulCompChkOk++;
        }
        else
        {
            ulCompChkFail++;
        }
        // DBG_PRINT("COMP frame #%lu %s  (good=%lu bad=%lu)\r\n", (unsigned long)ulCompFrameCnt,
        //           utUART0Mux.flags.bCompUpdate_success ? "CHECKSUM OK" : "CHECKSUM BAD",
        //           (unsigned long)ulCompChkOk, (unsigned long)ulCompChkFail);
        // DBG_HEXLINE("COMP", aCompBuf, COMP_FRAME_LEN); /* whole received array on UART2 */
        if (utUART0Mux.flags.bCompUpdate_success)
        {
            vcfg_uart0_BMS(); /* got comp data -> back to BMS */
            return;
        }
        vClearRx(aCompBuf);
    }
    /* 3. Partial frame left over and the line has been silent for a long time (> COMP_IDLE_GAP_MS):
     *    drop it so it can't get glued to the next frame. */
    else if (uhwRxLen > 0 && (now - last_rx_tick) >= pdMS_TO_TICKS(COMP_IDLE_GAP_MS))
    {
        // DBG_PRINT("COMP partial frame len=%u dropped (silence)\r\n", uhwRxLen);
        // DBG_HEX("COMP", aCompBuf, uhwRxLen);
        vClearRx(aCompBuf);
    }

    if ((now - last_query_tick) >= pdMS_TO_TICKS(COMP_RX_WINDOW_MS))
    {
        // DBG_PRINT("COMP no valid frame, window %d/%d (rx bytes=%u)\r\n", ubCompCon_Cnt + 1, MAX_TRIES, uhwRxLen);
        if (uhwRxLen)
        {
        }
        // DBG_HEX("COMP", aCompBuf, uhwRxLen);
        if (++ubCompCon_Cnt < MAX_TRIES)
        {
            vSendCompQuery(); /* new listen window (sends request only if COMP_SEND_QUERY) */
            // DBG_PRINT("COMP listening (window %d/%d)\r\n", ubCompCon_Cnt + 1, MAX_TRIES);
        }
        else
        {
            vComp_ZeroData();
            // DBG_PRINT("COMP failed %d times, switching to BMS\r\n", MAX_TRIES);
            vcfg_uart0_BMS(); /* 3 windows without a valid frame */
        }
    }
}

/* Call periodically while SEL is HIGH (BMS) - call every 5..10 ms */
void vdoBMSTime(void)
{
    TickType_t now = xTaskGetTickCount();

    vRxPoll("BMS raw", aBMSBuf, aBMSHdr, BMS_HDR_LEN);

    /* Work only with a COMPLETE frame: length comes from byte [3] of the frame */
    uint16_t need = uhBMS_FrameLen(aBMSBuf, uhwRxLen);
    if (need && uhwRxLen >= need)
    {
        utUART0Mux.flags.bBMSUpdate = 1;
        ulBMSFrameCnt++;
        utUART0Mux.flags.bBMSUpdate_success = utParse_BMS_Data(aBMSBuf, &stBMS_Data);
        if (utUART0Mux.flags.bBMSUpdate_success)
            ulBMSChkOk++;
        else
            ulBMSChkFail++;
        // DBG_PRINT("BMS frame #%lu len=%u (expected %u) %s (good=%lu bad=%lu)\r\n",
        //           (unsigned long)ulBMSFrameCnt, uhwRxLen, need,
        //           utUART0Mux.flags.bBMSUpdate_success ? "OK" : "BAD",
        //           (unsigned long)ulBMSChkOk, (unsigned long)ulBMSChkFail);
        // DBG_HEX("BMS", aBMSBuf, need);
        if (utUART0Mux.flags.bBMSUpdate_success)
        {
            vcfg_uart0_Comp(); /* got full, valid BMS frame -> go to compressor */
            return;
        }
        /* bad frame: drop it, a new query is sent by the timeout below */
        vClearRx(aBMSBuf);
    }

    if ((now - last_query_tick) >= pdMS_TO_TICKS(QUERY_RESPONSE_TIME))
    {
        // DBG_PRINT("BMS timeout, attempt %d/%d failed (rx bytes=%u, expected=%u)\r\n",
        //   ubBMSCon_Cnt + 1, MAX_TRIES, uhwRxLen, need);
        // if (uhwRxLen)
        // {

        // }
        // DBG_HEX("BMS", aBMSBuf, uhwRxLen);
        if (++ubBMSCon_Cnt < MAX_TRIES)
        {
            vSendBMSQuery(); /* attempt 2 and 3 */
            /* no valid frame -> all BMS values = 0 */
        }
        else
        {
            vBMS_ZeroData();
            // DBG_PRINT("BMS failed %d times, switching to COMP\r\n", MAX_TRIES);
            vcfg_uart0_Comp(); /* 3 attempts failed -> compressor */
        }
    }
}