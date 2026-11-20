
// #endif

/*******************************************************************************
* Title                 :   Compressor and BMS operations header
* Filename              :   bms_comp_daq.h
* Target                :   esp32 (esp idf)
* Notes                 :   UART0 shared by BMS (9600) and Compressor (2400)
*                           through CD4052B mux. SEL = GPIO32
*                           SEL HIGH -> BMS (X1/Y1), SEL LOW -> Compressor (X0/Y0)
*******************************************************************************/
#ifndef BMS_COMP_DAQ_H_
#define BMS_COMP_DAQ_H_


/************************************
 * MACROS AND DEFINES
 ************************************/
/* Fallbacks - remove if already defined in mrs.h */
#ifndef COMP_BMS_SEL
#define COMP_BMS_SEL        GPIO_NUM_32
#endif
#ifndef BMS_MODE
#define BMS_MODE            1           /* SEL high */
#endif
#ifndef COMP_MODE
#define COMP_MODE           0           /* SEL low  */
#endif
#ifndef UART0_RXBUFF_SIZE
#define UART0_RXBUFF_SIZE   128
#endif
#ifndef QUERY_RESPONSE_TIME
#define QUERY_RESPONSE_TIME 300         /* ms to wait for BMS reply */
#endif

#define BMS_BAUD            9600
#define COMP_BAUD           2400
#define COMP_PARITY         UART_PARITY_EVEN     /* compressor frame is 8E1 (8 data, EVEN parity, 1 stop) */
#define BMS_PARITY          UART_PARITY_DISABLE  /* BMS is 8N1 */

#define MAX_TRIES           3           /* attempts before switching channel */

/* Debug port: UART2 (ESP32 default pins TX=GPIO17, RX=GPIO16) */
#define DBG_UART            UART_NUM_2
#define DBG_BAUD            115200
#define DBG_TX_PIN          17
#define DBG_RX_PIN          16
#define DBG_ENABLE          1           /* set 0 to compile all debug prints out */

#if DBG_ENABLE
#define DBG_PRINT(fmt, ...) vDbg_Printf(fmt, ##__VA_ARGS__)
#define DBG_HEX(label, buf, len) vDbg_HexDump(label, buf, len)
#define DBG_HEXLINE(label, buf, len) vDbg_HexLine(label, buf, len)
#else
#define DBG_PRINT(fmt, ...) ((void)0)
#define DBG_HEX(label, buf, len) ((void)0)
#define DBG_HEXLINE(label, buf, len) ((void)0)
#endif

/* BMS: valid reply starts DD 03 00 2A  -> 0x2A data bytes, +4 header, +3 (chk 2B + 0x77) */
#define BMS_HDR_LEN         4
#define BMS_FRAME_LEN       64

/* Compressor: valid frame starts AA 13 00 (received only, nothing is sent) */
#define COMP_HDR_LEN        3
#define COMP_FRAME_LEN      38          /* compressor reply = 38 bytes total (AA 13 00 ...) */
// #define COMP_FRAME_LEN      43 
#define COMP_SEND_QUERY     0           /* 0 = receive only (no request sent), 1 = send aCompQuery first */
#define COMP_RX_WINDOW_MS   1500        /* per attempt: frame comes every 1 s and takes ~160 ms @2400 baud -> 1.5 s always holds a full frame */
/* Checksum: last byte = sum of bytes [1..36] (everything except 0xAA and the checksum itself), modulo 256 */
#define COMP_CHK_FIRST      1
#define COMP_CHK_LAST       36
#define COMP_CHK_IDX        37
#define COMP_IDLE_GAP_MS    200         /* silence inside a half-received frame before it is dropped (must be < 500 ms frame interval) */

/************************************
 * TYPEDEFS
 ************************************/
typedef union {
    struct {
        uint8_t bCompTime   : 1;  // Bit 0
        uint8_t bBMSTime    : 1;  // Bit 1
        uint8_t reserved2_3 : 2;  // Bits 2-3 (not used)
        uint8_t bCompUpdate : 1;  // Bit 4  frame received
        uint8_t bBMSUpdate  : 1;  // Bit 5  frame received
        uint8_t bBMSUpdate_success  : 1;
        uint8_t bCompUpdate_success : 1;
    } flags;
    uint8_t ubMode;
} utUART0Mux_t;

typedef struct {
    uint8_t  ubload_status;
    uint8_t  ubdischarge_switch;
    uint8_t  ubcharge_switch;
    uint16_t uhwcurrent_mA;
    uint16_t uhwvoltage_mV;
    uint16_t uhwfault_alert;
    uint8_t  ubsoh;
    uint8_t  ubsoc;
    uint16_t uhwcycles;
    uint8_t  ubcbc;
    uint32_t uwcbs;
    uint8_t  ubchemistry;
    uint16_t uhwcapacity_mAh;
    uint8_t  ubyear;   // +2000
    uint8_t  ubmonth;
    uint8_t  ubdate;
    uint8_t  ubhour;
    uint8_t  ubminute;
    uint8_t  ubsecond;
    uint8_t  ubsubsecond;
    uint8_t  ubnum_thermistors;
} stBMS_Data_t;

typedef struct {
    uint16_t uhwrpm;
    uint16_t uhwfault1;
    uint16_t uhwfault2;
    uint16_t uhwwarning1;
    uint16_t uhwwarning2;
    uint16_t uhwbus_voltage;
    float foutput_current;    // Scaled by 100
    float famb_temp;          // Scaled by 10
    float fmodule_temp;
} stComp_Data_t;

/************************************
 * EXPORTED VARIABLES
 ************************************/
extern uint8_t aCompBuf[UART0_RXBUFF_SIZE];
extern uint8_t aBMSBuf[UART0_RXBUFF_SIZE];
extern uint8_t aCompFrame[COMP_FRAME_LEN];
extern utUART0Mux_t utUART0Mux;
extern stBMS_Data_t stBMS_Data;
extern stComp_Data_t stComp_Data;
extern uint8_t ubCompCon_Cnt;
extern uint8_t ubBMSCon_Cnt;

/************************************
 * GLOBAL FUNCTION PROTOTYPES
 ************************************/
void vDbg_Init(void);                       /* UART2 debug port + ESP_LOG redirect */
void vDbg_Printf(const char *fmt, ...);     /* printf to UART2 with tick timestamp */
void vDbg_HexDump(const char *label, const uint8_t *buf, uint16_t len);  /* hex dump, 16 bytes per line */
void vDbg_HexLine(const char *label, const uint8_t *buf, uint16_t len);  /* whole array on ONE line */
void vInit_tskUART0(void);
void vdoCompTime(void);   /* call periodically (e.g. every 10 ms) */
void vdoBMSTime(void);    /* call periodically (e.g. every 10 ms) */
uint8_t utParse_Comp_Data(uint8_t *data, stComp_Data_t *parsed);
uint8_t utParse_BMS_Data(uint8_t *data, stBMS_Data_t *parsed);

#endif