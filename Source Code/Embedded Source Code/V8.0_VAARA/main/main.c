
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

#include "driver/gpio.h"
#include "esp_timer.h"

#include "esp_err.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "st7565.h"
#include "logo_bitmap.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_heap_caps.h"

#include "bms_comp_daq.h"
#include "ds18b20.h"

#include "driver/uart.h"

#include <time.h>
#include <sys/time.h>

#include <errno.h>
#include "freertos/semphr.h"
#include <ctype.h>
#include "driver/ledc.h"

#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_bt_defs.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gatt_common_api.h"
#include "esp_gap_bt_api.h"

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_spp_api.h"

#include "I2C_HAL.h"
#include "SHT2x.h"

static uint32_t last_sht_read_ms = 0;

#define SHT_READ_INTERVAL_MS 1000

unsigned char error = 0;
float sht20_temperature = 0.0f;
float sht20_humidity = 0.0f;

uint8_t sht20_error = 0;
// for bt configurations

#define TAG "VAARA_BT"

/*
 * Change these according to your application.
 */
#define BT_DEVICE_NAME "VAARA_V8.0"
#define SPP_SERVER_NAME "VAARA_SPP"

/*
 * Your PIN.
 */
#define BT_PIN "1234"

/*
 * Maximum simultaneous SPP clients.
 */
#define MAX_BT_CONNECTIONS 3

/*
 * Maximum command line length.
 */
#define RX_BUFFER_SIZE 128

/*
 * Maximum bytes copied into one queue item.
 */
#define RX_PACKET_SIZE 256

/*
 * Queue depth.
 */
#define BT_RX_QUEUE_LENGTH 10

/*
 * Bluetooth processing task stack.
 */
#define BT_TASK_STACK_SIZE 4096

/*
 * Bluetooth task priority.
 */
#define BT_TASK_PRIORITY 2

#define vp bt_temp
/* ============================================================
 * BLUETOOTH APPLICATION VALUES
 * ==========================================================*/

#define NVS_NAMESPACE "bt_data"

#define NVS_KEY_TEMP "bt_temp"
#define NVS_KEY_DUTY "bt_duty"

int bt_pwm_auto = 30;
int bt_pwm_eco = 30;
int bt_pwm_power = 20;
int bt_pwm_normal = 30;

/* NVS PWM keys */
#define NVS_KEY_PWM_AUTO "PWM_AUTO"
#define NVS_KEY_PWM_ECO "PWM_ECO"
#define NVS_KEY_PWM_POWER "PWM_POWER"
#define NVS_KEY_PWM_NORMAL "PWM_NORMAL"

// int bt_temp_min= 0;
// int bt_temp_max= 0;
float bt_temp_min = 0.0f;
float bt_temp_max = 0.0f;

// #define SET_TEMP_MIN bt_temp_min
// #define SET_TEMP_MAX bt_temp_max

#define NVS_KEY_TEMP_MIN "TEMP_MIN"
#define NVS_KEY_TEMP_MAX "TEMP_MAX"

static int bt_temp = 0;
static int bt_duty = 0;

static int64_t boot_time_us = 0;
static bool bt_started = false;

#define Set 1
#define Clear 0

/* =========================================================
 * TEMPERATURE / CONTROL VALUES
 * All temperatures are in DEGREES C (float).
 * ========================================================= */
int main_glob_Humidity = Clear;

/* ---- compressor relay polarity: flip if your driver is active-low ---- */
#define COMP_RELAY_ON_LEVEL 1
#define COMP_RELAY_OFF_LEVEL 0

/* ---- control timing ---- */
#define CTRL_PERIOD_MS 1000

/* ---- ORIGINAL control constants (unit = 0.1 C, so 200 = 20.0 C) ---- */
#define defn_lim_autoLimH 50 // If value above limit
#define defn_val_autoH 80    // Assign the value for high

#define defn_lim_autoLimM 50 // If value above limit
#define defn_val_autoM 65    // Assign the value for mid

#define defn_lim_autoLimL 10 // If value above limit
#define defn_val_autoL 50    // Assign the value for low

#define defn_lim_autoOff 3 // If value above limit
#define defn_val_autoOff 0 // Assign the value for off

#define defn_val_runPwrMod 30 // Time to run in power mode
#define defn_val_returnHys 20 // PWM control Hysterisis value (2.0 C)

#define defn_val_sysModeEco 0    // System operation mode as eco
#define defn_val_sysModePower 1  // System operation mode as power
#define defn_val_sysModeNormal 2 // System operation mode as normal
#define defn_val_sysModeAuto 3   // System operation mode as Auto

/* ---- ORIGINAL control variables ----
 * main_intr_snsTemp        sensor temperature x10 (fed from the DS18B20 float)
 * main_glob_limitTemp      set temperature x10   (fed from SET_TEMP)
 * main_prog_divPrevStatus  hysteresis flag (Set = stopped at set point)
 * main_intr_divPwmIndx     PWM index / duty %, 0 = compressor stopped
 * vc_main_prog_systemStatus Set = ON key active
 * vc_main_prog_systemMode  0 Eco, 1 Power, 2 Normal, 3 Auto
 */
int main_intr_snsTemp = Clear;
int main_glob_limitTemp = 200;
int main_prog_tempDifference = Clear;
unsigned char main_prog_divPrevStatus = Clear;
unsigned int main_intr_divPwmIndx = Clear;
unsigned int main_intr_pwrModeTimer = Clear;
unsigned int vc_main_prog_systemMode = defn_val_sysModeNormal;
unsigned int vc_main_prog_systemStatus = Clear;

/* ---- other control flags ----
 * onOffState    : user wants the compressor running (ON/OFF key, saved in NVS)
 * temp_fault    : DS18B20 failed TEMP_FAIL_LIMIT times in a row
 */
static volatile bool temp_fault = false;
static float ctrl_temp = 0.0f;
static bool ctrl_temp_valid = false;
static TaskHandle_t compressor_task_handle = NULL;

// for pwm define
#define PWM_GPIO GPIO_NUM_5
#define PWM_FREQUENCY 300
#define PWM_RESOLUTION LEDC_TIMER_10_BIT

// for dsb20 variables define

#define TEMP_BUS 21 // 26

#define digitalWrite gpio_set_level

DeviceAddress tempSensors[2];
uint8_t DS_GPIO;
int len;

// for switching between compresser and bms data variable define

#define UART0_TASK_STACK 4096
#define UART0_TASK_PRIO 1
#define UART0_TASK_PERIOD 10 /* ms between polls */

float humidity = 0.0f;
float temperature = 0.0f;

unsigned int raw_temperature = 0;
unsigned int raw_humidity = 0;

float SET_TEMP = 10.0f;

volatile bool gps_connected = false;

volatile int csq_value = 0;
volatile int csq_ber = 0;
volatile bool csq_ready = false;

int pwm_value = 0;

int swap_screen = 0; // for temp and humidity swap

// #define TAG "SWAP_SCREEN"

char buffer[20];
char buffer1[20];
char buffer2[20];
char buffer3[40];

extern uint8_t BMS_soc;
extern uint8_t BMS_lds;
extern uint16_t BMS_Cycles;
extern uint16_t BMS_volt;
extern uint16_t BMS_cur;
extern uint16_t BMS_faults;
extern uint8_t BMS_soh;

// for compresser

#define main_pBit_onOffCtrl_for_compresser 12

extern uint8_t compresser_spd;
extern uint8_t compresser_voltage;
extern uint8_t compresser_current;
extern uint8_t compresser_temp;

extern int Cds;
extern int Bfl;
extern char Stt[];
extern int Flt;

extern int Tco;
extern float Dcv;
extern float Cur;

// for gps data variables define

#define TAG2 "GPS_RTC"

/* ------------------------------------------------------------------ */
/* Configuration                                                       */
/* ------------------------------------------------------------------ */
#define TXD_PIN (GPIO_NUM_16) /* ESP TX -> BG95 RX */
#define RXD_PIN (GPIO_NUM_17) /* ESP RX <- BG95 TX */
#define BG95_BAUD 19200

/* Set to 1 if THIS esp32 must send the AT commands itself. */
#define GPS_POLL_MODEM 0

static const int RX_BUF_SIZE = 2048;

/* ------------------------------------------------------------------ */
/* Globals                                                             */
/* ------------------------------------------------------------------ */
uint8_t bg95_rsp_buff[2048]; /* last UART read from BG95            */
uint8_t gps_rtc_buff[1024];  /* complete "+QGPSLOC: ... OK" string  */

char latitude[20], longitude[20], date_buff[8], time_buff[8], speed[10];

char imei_number[16] = {0};
volatile bool imei_ready = false;

volatile bool gpsFix = false;
struct timespec rtc_time;

/* set by rx_task when gps_rtc_buff holds a complete response,
 * cleared by gps_process_task after use */
volatile bool gps_rtc_ready = false;

/* =========================================================
 * BUTTON CONFIG (GPIO 14)
 * ========================================================= */
#define BTN_PIN 14         /* GPIO 14                               */
#define BTN_ACTIVE_LEVEL 0 /* 0 = pressed pulls pin LOW (pull-up)   */
#define DEBOUNCE_MS 30
#define LONG_PRESS_MS 1000 /* hold this long = long press           */

#define main_pBit_onOffCtrl 35 /* ON/OFF key input (input-only pin: needs external pull-up) */

volatile bool onOffState = false; // false = OFF, true = ON

/* =========================================================
 * UP / DOWN KEYS (debounce + auto-repeat while held)
 * ========================================================= */
#define main_pBit_upKey 34 /* input-only pin: needs an EXTERNAL pull-up */
#define main_pBit_downKey 27
#define KEY_ACTIVE_LEVEL 0      /* 0 = pressed pulls the pin LOW             */
#define KEY_REPEAT_START_MS 500 /* hold this long before repeating           */
#define KEY_REPEAT_MS 150       /* repeat interval while held                */

typedef enum
{
    KEY_EVENT_NONE = 0,
    KEY_EVENT_PRESS,
    KEY_EVENT_REPEAT
} key_event_t;

typedef struct
{
    int pin;
    bool stable_pressed;
    bool last_raw;
    uint32_t last_change_ms;
    uint32_t press_start_ms;
    uint32_t last_repeat_ms;
} ui_key_t;

static ui_key_t up_key = {.pin = main_pBit_upKey};
static ui_key_t down_key = {.pin = main_pBit_downKey};

/* Range for the set-temperature screen */
#define SET_TEMP_MIN bt_temp_min
#define SET_TEMP_MAX bt_temp_max

#define SET_TEMP_STEP 1.0f

/* =========================================================
 * UI STATE
 * ========================================================= */
typedef enum
{
    UI_MAIN = 0,      /* page 2 (main screen)     */
    UI_SET_TEMP,      /* set temperature screen   */
    UI_SET_MODE,      /* set mode screens         */
    UI_BMS_PARAMETERS /* BMS parameters screen    */
} ui_state_t;

typedef enum
{
    MODE_ECO = 0,
    MODE_NORMAL,
    MODE_AUTO,
    MODE_POWER,
    MODE_COUNT
} op_mode_t;

static const char *const mode_names[MODE_COUNT] =
    {
        "ECO", "NORMAL", "AUTO", "POWER"};

static ui_state_t ui_state = UI_MAIN;
static volatile op_mode_t active_mode = MODE_NORMAL;
int mode_no_flag = 0;

typedef enum
{
    BTN_EVENT_NONE = 0,
    BTN_EVENT_SHORT,
    BTN_EVENT_LONG
} btn_event_t;

// for states variables define

typedef enum
{
    /* Sequence: IDLE -> CONFIG -> RUN -> ERROR */
    SYS_IDLE = 0, /* 1. main screen, compressor OFF, waiting     */
    SYS_CONFIG,   /* 2. set temp / set mode / BMS parameter screens */
    SYS_RUN,      /* 3. main screen, compressor ON               */
    SYS_ERROR     /* 4. fault detected                           */
} sys_state_t;

typedef enum
{
    ERR_NONE = 0,
    ERR_TEMP_SENSOR, /* DS18B20 returned NaN/Inf repeatedly */
    ERR_MODEM
} sys_error_t;

#define TEMP_FAIL_LIMIT 5 /* consecutive bad reads before ERROR */
#define REFRESH_MS 1000
#define JSON_MS 60000

static volatile sys_state_t sys_state = SYS_IDLE;
static sys_error_t sys_error = ERR_NONE;
static volatile uint8_t temp_fail_cnt = 0;
static uint32_t last_refresh_ms;
static uint32_t last_json_ms;

// for door status
#define main_pBit_doorSw 36

static const char *DOOR_TAG = "DOOR";
#define DOOR_EVENT_COUNT 5
#define DOOR_JSON_SIZE 512
static const char *JSON_TAG = "JSON";
static bool door_batch_ready = false;

/* Structure to store each door event */
typedef struct
{
    long long ts;
    int Dor;
} door_event_t;

/* Array to store 5 door events */
static door_event_t door_events[DOOR_EVENT_COUNT];

static SemaphoreHandle_t door_mutex = NULL;
static int door_event_count = 0;

#define JSON_BUFFER_SIZE 4096

char json_buffer[JSON_BUFFER_SIZE];
char door_json[DOOR_JSON_SIZE];

// FOR FUNCTIONS DECLARATION

static void redraw(void);
float ds18b20_get_temp(void);

void init(void);
static void rx_task(void *arg);
static void format_lat_lon(const char *input, char *output, size_t out_size, int degree_digits);
bool extract_gps_info(const char *gpsrtc_buff, char *latitude, char *longitude,
                      char *gpsdate, char *gpstime, char *gpsspeed);
void set_rtc_time(int gps_date, int gps_time);
void get_rtc_time(struct timespec *rtc_time);
void rtc_time_update_task(void *pvParameters);
static void gps_process_task(void *arg);
static void process_imei_line(char *line,
                              bool *expect_imei_line);
static void pwm_init(void);
static void pwm_set_duty(uint32_t duty_percent);
void door_gpio_init(void);
void door_task(void *arg);

static bool build_door_event_array(const door_event_t *batch);
void save_active_mode(op_mode_t mode);
void save_compressor_status(bool status);
void save_set_temp(void);
float load_set_temp(void);
static void compressor_kick(void);

/* state machine helpers (RUN state support) */
static void change_state(sys_state_t next);
static sys_state_t home_state(void);
static esp_err_t save_bt_variables(void);
static esp_err_t load_bt_variables(void);
void read_temperature_humidity(void);

/* ============================================================
 * RX QUEUE ITEM
 * ==========================================================*/
typedef struct
{
    bool connected;

    uint32_t handle;

    esp_bd_addr_t remote_bda;

    /*
     * RX state belongs to THIS connection.
     */
    char rx_buffer[RX_BUFFER_SIZE];

    size_t rx_index;

    /*
     * TX state.
     *
     * true  = previous write not completed
     * false = another write may be attempted
     */
    bool tx_busy;

    /*
     * true = SPP currently congested
     */
    bool tx_congested;

} bt_connection_t;

typedef struct
{
    uint32_t handle;

    uint16_t len;

    uint8_t data[RX_PACKET_SIZE];

} bt_rx_packet_t;

/* ============================================================
 * GLOBAL VARIABLES
 * ==========================================================*/

static bt_connection_t
    bt_connections[MAX_BT_CONNECTIONS];

static QueueHandle_t
    bt_rx_queue = NULL;

/* ============================================================
 * FIND FREE SLOT
 * ==========================================================*/

void main_glob_delayI2C(unsigned long dly)
{
    esp_rom_delay_us((uint32_t)dly);
}

static int bt_find_free_slot(void)
{
    for (int i = 0;
         i < MAX_BT_CONNECTIONS;
         i++)
    {
        if (!bt_connections[i].connected)
        {
            return i;
        }
    }

    return -1;
}

/* ============================================================
 * FIND SLOT BY HANDLE
 * ==========================================================*/

static int bt_find_slot_by_handle(uint32_t handle)
{
    for (int i = 0;
         i < MAX_BT_CONNECTIONS;
         i++)
    {
        if (bt_connections[i].connected &&
            bt_connections[i].handle == handle)
        {
            return i;
        }
    }

    return -1;
}

/* ============================================================
 * COUNT CONNECTIONS
 * ==========================================================*/

static int bt_connection_count(void)
{
    int count = 0;

    for (int i = 0;
         i < MAX_BT_CONNECTIONS;
         i++)
    {
        if (bt_connections[i].connected)
        {
            count++;
        }
    }

    return count;
}

/* ============================================================
 * PRINT BLUETOOTH ADDRESS
 * ==========================================================*/

static void print_bd_addr(const esp_bd_addr_t addr)
{
    ESP_LOGI(
        TAG,
        "%02X:%02X:%02X:%02X:%02X:%02X",
        addr[0],
        addr[1],
        addr[2],
        addr[3],
        addr[4],
        addr[5]);
}

/* ============================================================
 * SEND DATA TO ONE CONNECTION
 *
 * IMPORTANT:
 * ESP-IDF requires us to respect WRITE_EVT / CONG_EVT.
 * ==========================================================*/

static esp_err_t bt_send_to_handle(
    uint32_t handle,
    const char *data)
{
    if (data == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    int slot =
        bt_find_slot_by_handle(handle);

    if (slot < 0)
    {
        ESP_LOGE(
            TAG,
            "TX: invalid handle %lu",
            (unsigned long)handle);

        return ESP_ERR_NOT_FOUND;
    }

    bt_connection_t *conn =
        &bt_connections[slot];

    if (!conn->connected)
    {
        return ESP_ERR_INVALID_STATE;
    }

    /*
     * Previous write is still pending.
     */
    if (conn->tx_busy)
    {
        ESP_LOGW(
            TAG,
            "TX busy: slot=%d handle=%lu",
            slot,
            (unsigned long)handle);

        return ESP_ERR_INVALID_STATE;
    }

    /*
     * SPP is congested.
     */
    if (conn->tx_congested)
    {
        ESP_LOGW(
            TAG,
            "TX congested: slot=%d handle=%lu",
            slot,
            (unsigned long)handle);

        return ESP_ERR_INVALID_STATE;
    }

    size_t len = strlen(data);

    if (len == 0)
    {
        return ESP_OK;
    }

    /*
     * Mark busy BEFORE calling esp_spp_write().
     */
    conn->tx_busy = true;

    esp_err_t ret =
        esp_spp_write(
            handle,
            (int)len,
            (uint8_t *)data);

    if (ret != ESP_OK)
    {
        conn->tx_busy = false;

        ESP_LOGE(
            TAG,
            "SPP write failed: %s",
            esp_err_to_name(ret));

        return ret;
    }

    ESP_LOGI(
        TAG,
        "TX slot=%d: %s",
        slot,
        data);

    return ESP_OK;
}

// ble code
static void process_received_line(
    const char *line,
    uint32_t handle)
{
    if (line == NULL)
    {
        return;
    }

    /*
     * ============================================================
     * MAKE A LOCAL COPY
     * ============================================================
     *
     * We do this because the received Bluetooth buffer may contain:
     *
     * T100\r\n
     * D50\r\n
     *
     * We remove \r, \n, spaces and tabs from the end.
     */

    char cmd[128];

    strncpy(cmd, line, sizeof(cmd) - 1);
    cmd[sizeof(cmd) - 1] = '\0';

    /*
     * ============================================================
     * REMOVE CR / LF / SPACE / TAB
     * ============================================================
     */

    size_t len = strlen(cmd);

    while (len > 0)
    {
        if (cmd[len - 1] == '\r' ||
            cmd[len - 1] == '\n' ||
            cmd[len - 1] == ' ' ||
            cmd[len - 1] == '\t')
        {
            cmd[len - 1] = '\0';
            len--;
        }
        else
        {
            break;
        }
    }

    /*
     * Ignore empty command
     */

    if (cmd[0] == '\0')
    {
        return;
    }

    ESP_LOGI(
        TAG,
        "RX command [%s]",
        cmd);

    /*
     * ============================================================
     * TEMPERATURE COMMAND
     *
     * Example:
     *
     * T100
     * t100
     *
     * Result:
     *
     * bt_temp = 100
     * ============================================================
     */

    // if (cmd[0] == 'T' ||
    //     cmd[0] == 't')
    // {
    //     char *endptr = NULL;

    //     long value = strtol(
    //         &cmd[1],
    //         &endptr,
    //         10);

    //     /*
    //      * Check that:
    //      *
    //      * T + number
    //      *
    //      * was received.
    //      */

    //     if (endptr != &cmd[1] &&
    //         *endptr == '\0')
    //     {
    //         /*
    //          * Update variable
    //          */

    //         bt_temp = (int)value;

    //         ESP_LOGI(
    //             TAG,
    //             "Temperature received: bt_temp = %d",
    //             bt_temp);

    //         /*
    //          * ====================================================
    //          * SAVE TEMPERATURE TO NVS
    //          * ====================================================
    //          */

    //         esp_err_t ret = save_bt_variables();

    //         if (ret == ESP_OK)
    //         {
    //             ESP_LOGI(
    //                 TAG,
    //                 "Temperature saved successfully");

    //             char response[64];

    //             snprintf(
    //                 response,
    //                 sizeof(response),
    //                 "TEMP SAVED: %d\r\n",
    //                 bt_temp);

    //             bt_send_to_handle(
    //                 handle,
    //                 response);
    //         }
    //         else
    //         {
    //             ESP_LOGE(
    //                 TAG,
    //                 "Temperature save failed: %s",
    //                 esp_err_to_name(ret));

    //             bt_send_to_handle(
    //                 handle,
    //                 "TEMP SAVE ERROR\r\n");
    //         }

    //         return;
    //     }

    //     /*
    //      * Invalid T command
    //      */

    //     ESP_LOGW(
    //         TAG,
    //         "Invalid temperature command: [%s]",
    //         cmd);

    //     bt_send_to_handle(
    //         handle,
    //         "INVALID TEMP COMMAND\r\n");

    //     return;
    // }

    /*
     * ============================================================
     * DUTY COMMAND
     *
     * Example:
     *
     * D50
     * d50
     *
     * Result:
     *
     * bt_duty = 50
     * ============================================================
     */

    if (cmd[0] == 'D' ||
        cmd[0] == 'd')
    {
        char *endptr = NULL;

        long value = strtol(
            &cmd[1],
            &endptr,
            10);

        /*
         * Check that:
         *
         * D + number
         *
         * was received.
         */

        if (endptr != &cmd[1] &&
            *endptr == '\0')
        {
            /*
             * Update variable
             */

            bt_duty = (int)value;

            ESP_LOGI(
                TAG,
                "Duty received: bt_duty = %d",
                bt_duty);

            /*
             * ====================================================
             * SAVE DUTY TO NVS
             * ====================================================
             */

            esp_err_t ret = save_bt_variables();

            if (ret == ESP_OK)
            {
                ESP_LOGI(
                    TAG,
                    "Duty saved successfully");

                char response[64];

                snprintf(
                    response,
                    sizeof(response),
                    "DUTY SAVED: %d\r\n",
                    bt_duty);

                bt_send_to_handle(
                    handle,
                    response);
            }
            else
            {
                ESP_LOGE(
                    TAG,
                    "Duty save failed: %s",
                    esp_err_to_name(ret));

                bt_send_to_handle(
                    handle,
                    "DUTY SAVE ERROR\r\n");
            }

            return;
        }

        /*
         * Invalid D command
         */

        ESP_LOGW(
            TAG,
            "Invalid duty command: [%s]",
            cmd);

        bt_send_to_handle(
            handle,
            "INVALID DUTY COMMAND\r\n");

        return;
    }

    if (cmd[0] == 'A' || cmd[0] == 'a' ||
        cmd[0] == 'E' || cmd[0] == 'e' ||
        cmd[0] == 'P' || cmd[0] == 'p' ||
        cmd[0] == 'N' || cmd[0] == 'n')
    {
        char *endptr = NULL;
        const char *mode_name = NULL;

        long value = strtol(&cmd[1], &endptr, 10);

        if (endptr != &cmd[1] && *endptr == '\0')
        {
            if (value < 0 || value > 100)
            {
                bt_send_to_handle(handle,
                                  "INVALID PWM RANGE (0-100)\r\n");
                return;
            }

            /* UPDATE PWM VARIABLE */
            switch (cmd[0])
            {
            case 'A':
            case 'a':
                bt_pwm_auto = (int)value;
                mode_name = "AUTO";
                break;

            case 'E':
            case 'e':
                bt_pwm_eco = (int)value;
                mode_name = "ECO";
                break;

            case 'P':
            case 'p':
                bt_pwm_power = (int)value;
                mode_name = "POWER";
                break;

            case 'N':
            case 'n':
                bt_pwm_normal = (int)value;
                mode_name = "NORMAL";
                break;
            }

            ESP_LOGI(TAG,
                     "%s PWM UPDATED = %d",
                     mode_name,
                     (int)value);

            /* SAVE TO NVS */
            esp_err_t ret = save_bt_variables();

            if (ret == ESP_OK)
            {
                char response[64];

                snprintf(response,
                         sizeof(response),
                         "%s PWM SAVED = %d\r\n",
                         mode_name,
                         (int)value);

                bt_send_to_handle(handle, response);

                ESP_LOGI(TAG,
                         "%s PWM SAVED SUCCESSFULLY",
                         mode_name);
            }
            else
            {
                ESP_LOGE(TAG,
                         "%s PWM SAVE FAILED: %s",
                         mode_name,
                         esp_err_to_name(ret));

                bt_send_to_handle(handle,
                                  "PWM SAVE ERROR\r\n");
            }

            return;
        }

        ESP_LOGW(TAG, "Invalid PWM command: [%s]", cmd);

        bt_send_to_handle(handle,
                          "INVALID PWM COMMAND\r\n");

        return;
    }

    if (cmd[0] == 'T' || cmd[0] == 't')
    {
        if (cmd[1] != '+' && cmd[1] != '-')
        {
            bt_send_to_handle(handle, "INVALID TEMP COMMAND\r\n");
            return;
        }

        char *endptr = NULL;

        bool is_max = (cmd[1] == '+');

        long value = strtol(&cmd[1], &endptr, 10);

        /* Validate received number */
        if (endptr == &cmd[1] || *endptr != '\0')
        {
            bt_send_to_handle(handle, "INVALID TEMP COMMAND\r\n");
            return;
        }

        /* Validate received value in tenths */
        if (value < -500 || value > 1000)
        {
            bt_send_to_handle(handle, "INVALID TEMPERATURE RANGE\r\n");
            return;
        }

        /* Convert received value into float temperature */
        float temperature = (float)value / 10.0f;

        /* Validate MIN/MAX relationship */
        if (is_max)
        {
            if (bt_temp_min != 0.0f &&
                temperature <= bt_temp_min)
            {
                bt_send_to_handle(handle,
                                  "MAX MUST BE GREATER THAN MIN\r\n");
                return;
            }

            bt_temp_max = temperature;
        }
        else
        {
            if (bt_temp_max != 0.0f &&
                temperature >= bt_temp_max)
            {
                bt_send_to_handle(handle,
                                  "MIN MUST BE LESS THAN MAX\r\n");
                return;
            }

            bt_temp_min = temperature;
        }

        /* Save float temperatures using existing NVS function */
        esp_err_t ret = save_bt_variables();

        if (ret == ESP_OK)
        {
            char response[80];

            snprintf(response,
                     sizeof(response),
                     "%s TEMP SAVED = %.1f\r\n",
                     is_max ? "MAX" : "MIN",
                     temperature);

            bt_send_to_handle(handle, response);
        }
        else
        {
            bt_send_to_handle(handle, "TEMP SAVE ERROR\r\n");
        }

        return;
    }

    /*
     * ============================================================
     * LED ON
     * ============================================================
     */

    if (strcmp(cmd, "LED ON") == 0)
    {
        ESP_LOGI(
            TAG,
            "LED ON command received");

        /*
         * gpio_set_level(LED_GPIO, 1);
         */

        bt_send_to_handle(
            handle,
            "LED ON OK\r\n");

        return;
    }

    /*
     * ============================================================
     * LED OFF
     * ============================================================
     */

    if (strcmp(cmd, "LED OFF") == 0)
    {
        ESP_LOGI(
            TAG,
            "LED OFF command received");

        /*
         * gpio_set_level(LED_GPIO, 0);
         */

        bt_send_to_handle(
            handle,
            "LED OFF OK\r\n");

        return;
    }

    /*
     * ============================================================
     * INTEGER VALUE
     *
     * Example:
     *
     * 250
     * ============================================================
     */

    char *endptr = NULL;

    long value = strtol(
        cmd,
        &endptr,
        10);

    if (endptr != cmd &&
        *endptr == '\0')
    {
        int received_value = (int)value;

        ESP_LOGI(
            TAG,
            "received_value = %d",
            received_value);

        char response[64];

        snprintf(
            response,
            sizeof(response),
            "VALUE RECEIVED: %d\r\n",
            received_value);

        bt_send_to_handle(
            handle,
            response);

        return;
    }

    /*
     * ============================================================
     * UNKNOWN COMMAND
     * ============================================================
     */

    ESP_LOGW(
        TAG,
        "Unknown command: [%s]",
        cmd);

    bt_send_to_handle(
        handle,
        "UNKNOWN COMMAND\r\n");
}

/* ============================================================
 * PROCESS BYTES FOR ONE CONNECTION
 *
 * This function runs from the Bluetooth application task,
 * NOT directly inside the SPP callback.
 * ==========================================================*/

static void process_received_bytes(
    const uint8_t *data,
    int len,
    uint32_t handle)
{
    if (data == NULL ||
        len <= 0)
    {
        return;
    }

    int slot =
        bt_find_slot_by_handle(handle);

    if (slot < 0)
    {
        ESP_LOGW(
            TAG,
            "RX from unknown handle=%lu",
            (unsigned long)handle);

        return;
    }

    bt_connection_t *conn =
        &bt_connections[slot];

    for (int i = 0;
         i < len;
         i++)
    {
        char ch =
            (char)data[i];

        /* ----------------------------------------------------
         * END OF COMMAND
         * --------------------------------------------------*/

        if (ch == '\r' ||
            ch == '\n')
        {
            if (conn->rx_index > 0)
            {
                conn->rx_buffer[conn->rx_index] = '\0';

                process_received_line(
                    conn->rx_buffer,
                    handle);

                conn->rx_index = 0;

                memset(
                    conn->rx_buffer,
                    0,
                    sizeof(conn->rx_buffer));
            }

            continue;
        }

        /* ----------------------------------------------------
         * PRINTABLE DATA
         * --------------------------------------------------*/

        if (isprint(
                (unsigned char)ch))
        {
            if (conn->rx_index <
                RX_BUFFER_SIZE - 1)
            {
                conn->rx_buffer[conn->rx_index++] = ch;
            }
            else
            {
                /*
                 * Prevent buffer overflow.
                 */
                ESP_LOGW(
                    TAG,
                    "RX buffer overflow slot=%d",
                    slot);

                conn->rx_index = 0;

                memset(
                    conn->rx_buffer,
                    0,
                    sizeof(conn->rx_buffer));
            }
        }
    }
}

/* ============================================================
 * BLUETOOTH RX TASK
 * ==========================================================*/

static void bluetooth_rx_task(void *arg)
{
    bt_rx_packet_t packet;

    ESP_LOGI(
        TAG,
        "Bluetooth RX task started");

    while (1)
    {
        if (xQueueReceive(
                bt_rx_queue,
                &packet,
                portMAX_DELAY) == pdTRUE)
        {
            /*
             * Process outside SPP callback.
             */
            process_received_bytes(
                packet.data,
                packet.len,
                packet.handle);
        }
    }
}

/* ============================================================
 * GAP CALLBACK
 * ==========================================================*/

static void bt_gap_callback(
    esp_bt_gap_cb_event_t event,
    esp_bt_gap_cb_param_t *param)
{
    switch (event)
    {

        /* ----------------------------------------------------
         * AUTHENTICATION COMPLETE
         * --------------------------------------------------*/

    case ESP_BT_GAP_AUTH_CMPL_EVT:

        if (param->auth_cmpl.stat ==
            ESP_BT_STATUS_SUCCESS)
        {
            ESP_LOGI(
                TAG,
                "Bluetooth authentication SUCCESS");

            ESP_LOGI(
                TAG,
                "Remote address:");

            print_bd_addr(
                param->auth_cmpl.bda);
        }
        else
        {
            ESP_LOGE(
                TAG,
                "Bluetooth authentication FAILED");

            ESP_LOGE(
                TAG,
                "Status=%d",
                param->auth_cmpl.stat);
        }

        break;

        /* ----------------------------------------------------
         * PIN REQUEST
         * --------------------------------------------------*/

    case ESP_BT_GAP_PIN_REQ_EVT:
    {
        esp_bt_pin_code_t pin_code;

        memset(
            pin_code,
            0,
            sizeof(pin_code));

        memcpy(
            pin_code,
            BT_PIN,
            4);

        ESP_LOGI(
            TAG,
            "PIN requested");

        esp_err_t ret =
            esp_bt_gap_pin_reply(
                param->pin_req.bda,
                true,
                4,
                pin_code);

        if (ret != ESP_OK)
        {
            ESP_LOGE(
                TAG,
                "PIN reply failed: %s",
                esp_err_to_name(ret));
        }

        break;
    }

        /* ----------------------------------------------------
         * SSP CONFIRM
         * --------------------------------------------------*/

    case ESP_BT_GAP_CFM_REQ_EVT:

        ESP_LOGI(
            TAG,
            "SSP confirmation requested");

        ESP_LOGI(
            TAG,
            "Passkey=%lu",
            (unsigned long)
                param->cfm_req.num_val);

        esp_bt_gap_ssp_confirm_reply(
            param->cfm_req.bda,
            true);

        break;

        /* ----------------------------------------------------
         * SSP PASSKEY
         * --------------------------------------------------*/

    case ESP_BT_GAP_KEY_NOTIF_EVT:

        ESP_LOGI(
            TAG,
            "SSP passkey=%lu",
            (unsigned long)
                param->key_notif.passkey);

        break;

        /* ----------------------------------------------------
         * SSP KEY REQUEST
         * --------------------------------------------------*/

    case ESP_BT_GAP_KEY_REQ_EVT:

        ESP_LOGI(
            TAG,
            "SSP key requested");

        break;

        /* ----------------------------------------------------
         * MODE CHANGE
         * --------------------------------------------------*/

    case ESP_BT_GAP_MODE_CHG_EVT:

        ESP_LOGI(
            TAG,
            "Bluetooth mode=%d",
            param->mode_chg.mode);

        break;

    default:

        break;
    }
}

/* ============================================================
 * SPP CALLBACK
 * ==========================================================*/

static void bt_spp_callback(
    esp_spp_cb_event_t event,
    esp_spp_cb_param_t *param)
{
    switch (event)
    {

        /* ====================================================
         * SPP INIT
         * ==================================================*/

    case ESP_SPP_INIT_EVT:
    {
        ESP_LOGI(
            TAG,
            "ESP_SPP_INIT_EVT");

        if (param->init.status !=
            ESP_SPP_SUCCESS)
        {
            ESP_LOGE(
                TAG,
                "SPP initialization FAILED");

            break;
        }

        ESP_LOGI(
            TAG,
            "SPP initialized successfully");

        /*
         * Start SPP server.
         */
        esp_err_t ret =
            esp_spp_start_srv(
                ESP_SPP_SEC_AUTHENTICATE,
                ESP_SPP_ROLE_SLAVE,
                0,
                SPP_SERVER_NAME);

        if (ret != ESP_OK)
        {
            ESP_LOGE(
                TAG,
                "SPP server start failed: %s",
                esp_err_to_name(ret));
        }

        break;
    }

        /* ====================================================
         * SERVER START
         * ==================================================*/

    case ESP_SPP_START_EVT:
    {
        ESP_LOGI(
            TAG,
            "================================");

        if (param->start.status ==
            ESP_SPP_SUCCESS)
        {
            ESP_LOGI(
                TAG,
                "SPP SERVER STARTED");

            ESP_LOGI(
                TAG,
                "Server=%s",
                SPP_SERVER_NAME);

            ESP_LOGI(
                TAG,
                "Waiting for connections...");
        }
        else
        {
            ESP_LOGE(
                TAG,
                "SPP server failed");
        }

        ESP_LOGI(
            TAG,
            "================================");

        break;
    }

        /* ====================================================
         * CLIENT CONNECTED
         * ==================================================*/

    case ESP_SPP_SRV_OPEN_EVT:
    {
        uint32_t handle =
            param->srv_open.handle;

        ESP_LOGI(
            TAG,
            "================================");

        ESP_LOGI(
            TAG,
            "NEW SPP CLIENT CONNECTED");

        ESP_LOGI(
            TAG,
            "Handle=%lu",
            (unsigned long)handle);

        /*
         * Find free slot.
         */
        int slot =
            bt_find_free_slot();

        if (slot < 0)
        {
            ESP_LOGE(
                TAG,
                "No free connection slot");

            esp_spp_disconnect(
                handle);

            break;
        }

        /*
         * Clear the complete slot first.
         */
        memset(
            &bt_connections[slot],
            0,
            sizeof(
                bt_connections[slot]));

        /*
         * Store connection.
         */
        bt_connections[slot].connected =
            true;

        bt_connections[slot].handle =
            handle;

        memcpy(
            bt_connections[slot].remote_bda,
            param->srv_open.rem_bda,
            sizeof(esp_bd_addr_t));

        ESP_LOGI(
            TAG,
            "Slot=%d",
            slot);

        ESP_LOGI(
            TAG,
            "Remote MAC:");

        print_bd_addr(
            param->srv_open.rem_bda);

        ESP_LOGI(
            TAG,
            "Connections=%d/%d",
            bt_connection_count(),
            MAX_BT_CONNECTIONS);

        ESP_LOGI(
            TAG,
            "New listen handle=%lu",
            (unsigned long)
                param->srv_open.new_listen_handle);

        /*
         * Welcome message.
         *
         * It may be possible for the connection to be
         * immediately ready, but TX is now protected by
         * tx_busy/congestion handling.
         */
        esp_err_t ret =
            bt_send_to_handle(
                handle,
                "VAARA CONNECTED\r\n");

        if (ret != ESP_OK)
        {
            ESP_LOGW(
                TAG,
                "Welcome TX deferred/failed: %s",
                esp_err_to_name(ret));
        }

        ESP_LOGI(
            TAG,
            "================================");

        break;
    }

        /* ====================================================
         * DATA RECEIVED
         *
         * IMPORTANT:
         * DO NOT process data here.
         *
         * Copy it into a queue and return quickly.
         * ==================================================*/

    case ESP_SPP_DATA_IND_EVT:
    {
        uint32_t handle =
            param->data_ind.handle;

        uint16_t len =
            param->data_ind.len;

        if (len == 0)
        {
            break;
        }

        /*
         * Limit queue packet size.
         */
        if (len > RX_PACKET_SIZE)
        {
            ESP_LOGW(
                TAG,
                "RX packet too large: %u",
                len);

            /*
             * Process only what fits.
             *
             * If your protocol needs packets larger
             * than this, increase RX_PACKET_SIZE.
             */
            len = RX_PACKET_SIZE;
        }

        bt_rx_packet_t packet;

        packet.handle =
            handle;

        packet.len =
            len;

        memcpy(
            packet.data,
            param->data_ind.data,
            len);

        /*
         * Queue the received packet.
         *
         * ZERO BLOCK TIME is intentional.
         *
         * Never block the Bluetooth callback.
         */
        if (xQueueSend(
                bt_rx_queue,
                &packet,
                0) != pdTRUE)
        {
            ESP_LOGW(
                TAG,
                "BT RX queue full - data dropped");
        }

        break;
    }

        /* ====================================================
         * WRITE COMPLETE
         * ==================================================*/

    case ESP_SPP_WRITE_EVT:
    {
        uint32_t handle =
            param->write.handle;

        int slot =
            bt_find_slot_by_handle(
                handle);

        if (slot >= 0)
        {
            bt_connections[slot].tx_busy =
                false;

            bt_connections[slot].tx_congested =
                param->write.cong;
        }

        ESP_LOGI(
            TAG,
            "WRITE complete handle=%lu len=%d cong=%d",
            (unsigned long)handle,
            param->write.len,
            param->write.cong);

        break;
    }

        /* ====================================================
         * CONGESTION
         * ==================================================*/

    case ESP_SPP_CONG_EVT:
    {
        uint32_t handle =
            param->cong.handle;

        int slot =
            bt_find_slot_by_handle(
                handle);

        if (slot >= 0)
        {
            bt_connections[slot].tx_congested =
                param->cong.cong;

            /*
             * When congestion becomes false,
             * a new TX is allowed.
             *
             * tx_busy is cleared by WRITE_EVT.
             */
        }

        ESP_LOGI(
            TAG,
            "CONGESTION handle=%lu cong=%d",
            (unsigned long)handle,
            param->cong.cong);

        break;
    }

        /* ====================================================
         * DISCONNECTED
         * ==================================================*/

    case ESP_SPP_CLOSE_EVT:
    {
        uint32_t handle =
            param->close.handle;

        ESP_LOGI(
            TAG,
            "================================");

        ESP_LOGI(
            TAG,
            "SPP CLIENT DISCONNECTED");

        ESP_LOGI(
            TAG,
            "Handle=%lu",
            (unsigned long)handle);

        int slot =
            bt_find_slot_by_handle(
                handle);

        if (slot >= 0)
        {
            ESP_LOGI(
                TAG,
                "Slot=%d",
                slot);

            ESP_LOGI(
                TAG,
                "Remote MAC:");

            print_bd_addr(
                bt_connections[slot].remote_bda);

            /*
             * Completely clear connection.
             */
            memset(
                &bt_connections[slot],
                0,
                sizeof(
                    bt_connections[slot]));
        }

        ESP_LOGI(
            TAG,
            "Connections=%d/%d",
            bt_connection_count(),
            MAX_BT_CONNECTIONS);

        ESP_LOGI(
            TAG,
            "================================");

        break;
    }

    default:

        break;
    }
}

/* ============================================================
 * BLUETOOTH INITIALIZATION
 * ==========================================================*/

static esp_err_t bluetooth_init(void)
{
    esp_err_t ret;

    /* ========================================================
     * RELEASE BLE MEMORY
     *
     * This application uses Classic Bluetooth SPP only.
     * ======================================================*/

    ret =
        esp_bt_controller_mem_release(
            ESP_BT_MODE_BLE);

    if (ret != ESP_OK)
    {
        ESP_LOGW(
            TAG,
            "BLE memory release: %s",
            esp_err_to_name(ret));
    }

    /* ========================================================
     * CONTROLLER CONFIGURATION
     * ======================================================*/

    esp_bt_controller_config_t bt_cfg =
        BT_CONTROLLER_INIT_CONFIG_DEFAULT();

    ret =
        esp_bt_controller_init(
            &bt_cfg);

    if (ret != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "BT controller init failed: %s",
            esp_err_to_name(ret));

        return ret;
    }

    /* ========================================================
     * ENABLE CLASSIC BLUETOOTH
     * ======================================================*/

    ret =
        esp_bt_controller_enable(
            ESP_BT_MODE_CLASSIC_BT);

    if (ret != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "BT controller enable failed: %s",
            esp_err_to_name(ret));

        return ret;
    }

    /* ========================================================
     * BLUEDROID INIT
     * ======================================================*/

    ret =
        esp_bluedroid_init();

    if (ret != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Bluedroid init failed: %s",
            esp_err_to_name(ret));

        return ret;
    }

    /* ========================================================
     * BLUEDROID ENABLE
     * ======================================================*/

    ret =
        esp_bluedroid_enable();

    if (ret != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Bluedroid enable failed: %s",
            esp_err_to_name(ret));

        return ret;
    }

    /* ========================================================
     * GAP CALLBACK
     * ======================================================*/

    ret =
        esp_bt_gap_register_callback(
            bt_gap_callback);

    if (ret != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "GAP callback failed: %s",
            esp_err_to_name(ret));

        return ret;
    }

    /* ========================================================
     * DEVICE NAME
     * ======================================================*/

    ret =
        esp_bt_gap_set_device_name(
            BT_DEVICE_NAME);

    if (ret != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Set device name failed: %s",
            esp_err_to_name(ret));

        return ret;
    }

    /* ========================================================
     * CONNECTABLE + DISCOVERABLE
     * ======================================================*/

    ret =
        esp_bt_gap_set_scan_mode(
            ESP_BT_CONNECTABLE,
            ESP_BT_GENERAL_DISCOVERABLE);

    if (ret != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Set scan mode failed: %s",
            esp_err_to_name(ret));

        return ret;
    }

    /* ========================================================
     * FIXED PIN
     * ======================================================*/

    esp_bt_pin_code_t pin_code;

    memset(
        pin_code,
        0,
        sizeof(pin_code));

    memcpy(
        pin_code,
        BT_PIN,
        4);

    ret =
        esp_bt_gap_set_pin(
            ESP_BT_PIN_TYPE_FIXED,
            4,
            pin_code);

    if (ret != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Set PIN failed: %s",
            esp_err_to_name(ret));

        return ret;
    }

    /* ========================================================
     * SPP CALLBACK
     * ======================================================*/

    ret =
        esp_spp_register_callback(
            bt_spp_callback);

    if (ret != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "SPP callback registration failed: %s",
            esp_err_to_name(ret));

        return ret;
    }

    /* ========================================================
     * SPP ENHANCED INIT
     *
     * ESP-IDF 6.x
     *
     * We use CALLBACK mode.
     * ======================================================*/

    esp_spp_cfg_t spp_cfg =
        {
            .mode =
                ESP_SPP_MODE_CB,

            /*
             * Keep this disabled initially.
             */
            .enable_l2cap_ertm =
                false,

            /*
             * Not important for CB mode.
             */
            .tx_buffer_size =
                0};

    ret =
        esp_spp_enhanced_init(
            &spp_cfg);

    if (ret != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "SPP enhanced init failed: %s",
            esp_err_to_name(ret));

        return ret;
    }

    /* ========================================================
     * GET BLUETOOTH ADDRESS
     * ======================================================*/

    const uint8_t *addr =
        esp_bt_dev_get_address();

    ESP_LOGI(
        TAG,
        "==========================================");

    ESP_LOGI(
        TAG,
        "VAARA BLUETOOTH");

    ESP_LOGI(
        TAG,
        "Device Name : %s",
        BT_DEVICE_NAME);

    ESP_LOGI(
        TAG,
        "PIN         : %s",
        BT_PIN);

    ESP_LOGI(
        TAG,
        "SPP Server  : %s",
        SPP_SERVER_NAME);

    ESP_LOGI(
        TAG,
        "Maximum     : %d connections",
        MAX_BT_CONNECTIONS);

    if (addr != NULL)
    {
        ESP_LOGI(
            TAG,
            "Bluetooth MAC:");

        ESP_LOGI(
            TAG,
            "%02X:%02X:%02X:%02X:%02X:%02X",
            addr[0],
            addr[1],
            addr[2],
            addr[3],
            addr[4],
            addr[5]);
    }

    ESP_LOGI(
        TAG,
        "==========================================");

    return ESP_OK;
}

// NEW CODE 18-11-2026

static esp_err_t save_bt_variables(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err;

    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "NVS open failed: %s",
                 esp_err_to_name(err));
        return err;
    }

    /* SAVE TEMPERATURE */
    err = nvs_set_i32(nvs_handle, NVS_KEY_TEMP, (int32_t)bt_temp);
    if (err != ESP_OK)
        goto save_error;

    /* SAVE DUTY */
    err = nvs_set_i32(nvs_handle, NVS_KEY_DUTY, (int32_t)bt_duty);
    if (err != ESP_OK)
        goto save_error;

    /* SAVE AUTO PWM */
    err = nvs_set_i32(nvs_handle, NVS_KEY_PWM_AUTO, (int32_t)bt_pwm_auto);
    if (err != ESP_OK)
        goto save_error;

    /* SAVE ECO PWM */
    err = nvs_set_i32(nvs_handle, NVS_KEY_PWM_ECO, (int32_t)bt_pwm_eco);
    if (err != ESP_OK)
        goto save_error;

    /* SAVE POWER PWM */
    err = nvs_set_i32(nvs_handle, NVS_KEY_PWM_POWER, (int32_t)bt_pwm_power);
    if (err != ESP_OK)
        goto save_error;

    /* SAVE NORMAL PWM */
    err = nvs_set_i32(nvs_handle, NVS_KEY_PWM_NORMAL, (int32_t)bt_pwm_normal);
    if (err != ESP_OK)
        goto save_error;

    /* SAVE MIN TEMPERATURE (FLOAT x 10) */
    int32_t min_temp_value = (int32_t)(bt_temp_min * 10.0f);

    err = nvs_set_i32(nvs_handle, NVS_KEY_TEMP_MIN, min_temp_value);
    if (err != ESP_OK)
        goto save_error;

    /* SAVE MAX TEMPERATURE (FLOAT x 10) */
    int32_t max_temp_value = (int32_t)(bt_temp_max * 10.0f);

    err = nvs_set_i32(nvs_handle, NVS_KEY_TEMP_MAX, max_temp_value);
    if (err != ESP_OK)
        goto save_error;

    /* COMMIT ALL VALUES */
    err = nvs_commit(nvs_handle);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "NVS commit failed: %s",
                 esp_err_to_name(err));

        nvs_close(nvs_handle);
        return err;
    }

    nvs_close(nvs_handle);

    ESP_LOGI(TAG,
             "NVS SAVE OK -> TEMP=%d DUTY=%d AUTO=%d ECO=%d POWER=%d NORMAL=%d MIN=%.1f MAX=%.1f",
             bt_temp,
             bt_duty,
             bt_pwm_auto,
             bt_pwm_eco,
             bt_pwm_power,
             bt_pwm_normal,
             bt_temp_min,
             bt_temp_max);

    return ESP_OK;

save_error:

    ESP_LOGE(TAG, "NVS save failed: %s",
             esp_err_to_name(err));

    nvs_close(nvs_handle);

    return err;
}

static esp_err_t load_bt_variables(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err;

    int32_t temp_value;
    int32_t duty_value;

    int32_t auto_value;
    int32_t eco_value;
    int32_t power_value;
    int32_t normal_value;

    int32_t min_temp_value;
    int32_t max_temp_value;

    /* DEFAULT VALUES */
    bt_temp = 0;
    bt_duty = 0;

    bt_pwm_auto = 0;
    bt_pwm_eco = 0;
    bt_pwm_power = 0;
    bt_pwm_normal = 0;

    bt_temp_min = 0.0f;
    bt_temp_max = 0.0f;

    /* OPEN NVS */
    err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);

    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "NVS namespace not available: %s",
                 esp_err_to_name(err));
        return err;
    }

    /* READ TEMPERATURE */
    err = nvs_get_i32(nvs_handle, NVS_KEY_TEMP, &temp_value);

    if (err == ESP_OK)
        bt_temp = (int)temp_value;
    else if (err != ESP_ERR_NVS_NOT_FOUND)
        goto load_error;

    /* READ DUTY */
    err = nvs_get_i32(nvs_handle, NVS_KEY_DUTY, &duty_value);

    if (err == ESP_OK)
        bt_duty = (int)duty_value;
    else if (err != ESP_ERR_NVS_NOT_FOUND)
        goto load_error;

    /* READ AUTO PWM */
    err = nvs_get_i32(nvs_handle, NVS_KEY_PWM_AUTO, &auto_value);

    if (err == ESP_OK)
        bt_pwm_auto = (int)auto_value;
    else if (err != ESP_ERR_NVS_NOT_FOUND)
        goto load_error;

    /* READ ECO PWM */
    err = nvs_get_i32(nvs_handle, NVS_KEY_PWM_ECO, &eco_value);

    if (err == ESP_OK)
        bt_pwm_eco = (int)eco_value;
    else if (err != ESP_ERR_NVS_NOT_FOUND)
        goto load_error;

    /* READ POWER PWM */
    err = nvs_get_i32(nvs_handle, NVS_KEY_PWM_POWER, &power_value);

    if (err == ESP_OK)
        bt_pwm_power = (int)power_value;
    else if (err != ESP_ERR_NVS_NOT_FOUND)
        goto load_error;

    /* READ NORMAL PWM */
    err = nvs_get_i32(nvs_handle, NVS_KEY_PWM_NORMAL, &normal_value);

    if (err == ESP_OK)
        bt_pwm_normal = (int)normal_value;
    else if (err != ESP_ERR_NVS_NOT_FOUND)
        goto load_error;

    /* READ MIN TEMPERATURE */
    err = nvs_get_i32(nvs_handle, NVS_KEY_TEMP_MIN, &min_temp_value);

    if (err == ESP_OK)
        bt_temp_min = min_temp_value / 10.0f;
    else if (err != ESP_ERR_NVS_NOT_FOUND)
        goto load_error;

    /* READ MAX TEMPERATURE */
    err = nvs_get_i32(nvs_handle, NVS_KEY_TEMP_MAX, &max_temp_value);

    if (err == ESP_OK)
        bt_temp_max = max_temp_value / 10.0f;
    else if (err != ESP_ERR_NVS_NOT_FOUND)
        goto load_error;

    /* CLOSE NVS */
    nvs_close(nvs_handle);

    ESP_LOGI(TAG,
             "NVS LOAD OK -> TEMP=%d DUTY=%d AUTO=%d ECO=%d POWER=%d NORMAL=%d MIN=%.1f MAX=%.1f",
             bt_temp,
             bt_duty,
             bt_pwm_auto,
             bt_pwm_eco,
             bt_pwm_power,
             bt_pwm_normal,
             bt_temp_min,
             bt_temp_max);

    return ESP_OK;

load_error:

    ESP_LOGE(TAG, "NVS read failed: %s",
             esp_err_to_name(err));

    nvs_close(nvs_handle);

    return err;
}

// static void check_nvs_space(void)
// {
//     nvs_stats_t stats;

//     esp_err_t ret = nvs_get_stats(NULL, &stats);

//     if (ret == ESP_OK)
//     {
//         ESP_LOGI(
//             TAG,
//             "NVS SPACE: used=%lu free=%lu available=%lu total=%lu namespaces=%lu",
//             (unsigned long)stats.used_entries,
//             (unsigned long)stats.free_entries,
//             (unsigned long)stats.available_entries,
//             (unsigned long)stats.total_entries,
//             (unsigned long)stats.namespace_count);
//     }
//     else
//     {
//         ESP_LOGE(
//             TAG,
//             "nvs_get_stats() failed: %s",
//             esp_err_to_name(ret));
//     }
// }

/* Initialize door switch GPIO + compressor relay GPIO */
void door_gpio_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << main_pBit_doorSw),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE};

    ESP_ERROR_CHECK(gpio_config(&io_conf));

    gpio_config_t compressor_gpio = {
        .pin_bit_mask = (1ULL << main_pBit_onOffCtrl_for_compresser),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE

    };

    ESP_ERROR_CHECK(gpio_config(&compressor_gpio));

    // Default compressor OFF
    gpio_set_level(main_pBit_onOffCtrl_for_compresser, COMP_RELAY_OFF_LEVEL);
}

static void pwm_init(void)
{
    ledc_timer_config_t timer_conf = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = PWM_RESOLUTION,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = PWM_FREQUENCY,
        .clk_cfg = LEDC_AUTO_CLK};

    ESP_ERROR_CHECK(ledc_timer_config(&timer_conf));

    ledc_channel_config_t channel_conf = {
        .gpio_num = PWM_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0};

    ESP_ERROR_CHECK(ledc_channel_config(&channel_conf));
}

static void pwm_set_duty(uint32_t duty_percent)
{
    if (duty_percent > 100)
        duty_percent = 100;

    uint32_t duty_value = (((1U << 10) - 1U) * duty_percent) / 100U; /* 10-bit resolution */

    ESP_ERROR_CHECK(
        ledc_set_duty(LEDC_LOW_SPEED_MODE,
                      LEDC_CHANNEL_0,
                      duty_value));

    ESP_ERROR_CHECK(
        ledc_update_duty(LEDC_LOW_SPEED_MODE,
                         LEDC_CHANNEL_0));

    printf("PWM Frequency: %d Hz, Duty: %lu%%\n",
           PWM_FREQUENCY,
           (unsigned long)duty_percent);
}

static void tskUART0(void *pvParameters)
{
    vInit_tskUART0(); /* debug port, UART0, mux pin; starts on BMS */

    TickType_t xLastWake = xTaskGetTickCount();

    for (;;)
    {
        if (utUART0Mux.flags.bBMSTime)
        {
            vdoBMSTime();
        }
        else if (utUART0Mux.flags.bCompTime)
        {
            vdoCompTime();
        }

        vTaskDelayUntil(&xLastWake, pdMS_TO_TICKS(UART0_TASK_PERIOD));
    }
}

void nvs_init(void)
{
    esp_err_t err = nvs_flash_init();

    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }

    ESP_ERROR_CHECK(err);
}

void save_swap_screen(int swap_screen)
{
    nvs_handle_t nvs_handle;

    esp_err_t err = nvs_open("storage", NVS_READWRITE, &nvs_handle);

    if (err == ESP_OK)
    {
        nvs_set_i32(nvs_handle, "swap_screen", swap_screen);

        nvs_commit(nvs_handle);

        nvs_close(nvs_handle);
    }
}

int load_swap_screen(void)
{
    nvs_handle_t nvs_handle;
    int32_t value = 0;

    esp_err_t err = nvs_open("storage", NVS_READONLY, &nvs_handle);

    if (err == ESP_OK)
    {
        err = nvs_get_i32(nvs_handle, "swap_screen", &value);

        nvs_close(nvs_handle);

        if (err == ESP_OK)
        {
            return (int)value;
        }
    }

    return 0; // Default value
}

/* Setpoint is stored in 0.1 C units (int32) */
void save_set_temp(void)
{
    nvs_handle_t h;

    if (nvs_open("storage", NVS_READWRITE, &h) == ESP_OK)
    {
        nvs_set_i32(h, "set_temp10", (int32_t)lroundf(SET_TEMP * 10.0f));
        nvs_commit(h);
        nvs_close(h);
    }
}

float load_set_temp(void)
{
    nvs_handle_t h;
    int32_t v = 100; /* default 10.0 C */

    if (nvs_open("storage", NVS_READONLY, &h) == ESP_OK)
    {
        if (nvs_get_i32(h, "set_temp10", &v) != ESP_OK)
        {
            v = 100;
        }
        nvs_close(h);
    }

    float t = (float)v / 10.0f;

    if (t < SET_TEMP_MIN)
        t = SET_TEMP_MIN;
    if (t > SET_TEMP_MAX)
        t = SET_TEMP_MAX;

    return t;
}

static uint32_t millis(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* =========================================================
 * BUTTON DRIVER: debounce + short / long press
 * ========================================================= */
static void button_init(void)
{
    gpio_config_t cfg =
        {
            .pin_bit_mask = 1ULL << BTN_PIN,
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << main_pBit_onOffCtrl),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE};

    ESP_ERROR_CHECK(gpio_config(&io_conf));
    ESP_ERROR_CHECK(gpio_config(&cfg));
}

static btn_event_t button_poll(void)
{
    static bool stable_pressed = false;
    static bool last_raw = false;
    static uint32_t last_change_ms = 0;
    static uint32_t press_start_ms = 0;
    static bool long_fired = false;

    uint32_t now = millis();
    bool raw = (gpio_get_level(BTN_PIN) == BTN_ACTIVE_LEVEL);

    /* Debounce: accept a new state only after it stays stable */
    if (raw != last_raw)
    {
        last_raw = raw;
        last_change_ms = now;
    }

    if ((now - last_change_ms) >= DEBOUNCE_MS && raw != stable_pressed)
    {
        stable_pressed = raw;

        if (stable_pressed) /* just pressed */
        {
            press_start_ms = now;
            long_fired = false;
        }
        else if (!long_fired) /* released before long press */
        {
            return BTN_EVENT_SHORT;
        }
    }

    /* Long press fires once while the button is still held */
    if (stable_pressed && !long_fired &&
        (now - press_start_ms) >= LONG_PRESS_MS)
    {
        long_fired = true;
        return BTN_EVENT_LONG;
    }

    return BTN_EVENT_NONE;
}

static void key_init(ui_key_t *k, bool internal_pullup)
{
    gpio_config_t cfg =
        {
            .pin_bit_mask = 1ULL << k->pin,
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = internal_pullup ? GPIO_PULLUP_ENABLE
                                          : GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_ERROR_CHECK(gpio_config(&cfg));
}

static key_event_t key_poll(ui_key_t *k)
{
    uint32_t now = millis();
    bool raw = (gpio_get_level(k->pin) == KEY_ACTIVE_LEVEL);

    /* Debounce: accept a new state only after it stays stable */
    if (raw != k->last_raw)
    {
        k->last_raw = raw;
        k->last_change_ms = now;
    }

    if ((now - k->last_change_ms) >= DEBOUNCE_MS && raw != k->stable_pressed)
    {
        k->stable_pressed = raw;

        if (raw) /* just pressed */
        {
            k->press_start_ms = now;
            k->last_repeat_ms = now;
            return KEY_EVENT_PRESS;
        }
    }

    /* Auto-repeat while the key is held */
    if (k->stable_pressed &&
        (now - k->press_start_ms) >= KEY_REPEAT_START_MS &&
        (now - k->last_repeat_ms) >= KEY_REPEAT_MS)
    {
        k->last_repeat_ms = now;
        return KEY_EVENT_REPEAT;
    }

    return KEY_EVENT_NONE;
}

/* ON/OFF key: toggles onOffState, saves it ONCE, wakes the control task,
 * and moves the state machine IDLE <-> RUN */
static void main_onOffButton(void)
{
    static bool last_state = true;
    static uint32_t last_press_time = 0;

    bool current_state = gpio_get_level(main_pBit_onOffCtrl);
    uint32_t now = millis();

    /* Button pressed: HIGH -> LOW */
    if (last_state == true && current_state == false)
    {
        /* Simple debounce */
        if ((now - last_press_time) >= 200)
        {
            last_press_time = now;

            /* Toggle ON/OFF */
            onOffState = !onOffState;
            save_compressor_status(onOffState);

            /* React immediately, not after the next 1 s control cycle */
            compressor_kick();

            /* IDLE <-> RUN follows the ON/OFF key (CONFIG/ERROR keep their state) */
            if (sys_state == SYS_IDLE || sys_state == SYS_RUN)
                change_state(home_state());

            /* Immediately update display */
            redraw();
        }
    }

    last_state = current_state;
}

static void update_charger_logo(void)
{
    static int64_t last_toggle_time = 0;
    static bool blink_state = true;

    int64_t current_time = esp_timer_get_time();

    if (BMS_soc <= 25)
    {
        /* Toggle every 300 ms */
        if (current_time - last_toggle_time >= 300000)
        {
            last_toggle_time = current_time;
            blink_state = !blink_state;
        }

        draw_charger_logo(35, 54, blink_state);
    }
    else
    {
        /* Keep logo visible when SOC is above 25% */
        blink_state = false;
        last_toggle_time = current_time;

        draw_charger_logo(35, 54, false);
    }
}

/* Draws ON/OFF text only. No GPIO and no NVS access here. */
static void draw_onoff_status(void)
{
    if (onOffState)
    {
        st7565_draw_string(117, 55, "ON", true);
    }
    else
    {
        st7565_draw_string(110, 55, "OFF", true);
    }
}

/* =========================================================
 * PAGE 1 (logo)
 * ========================================================= */
static void display_page1(void)
{
    st7565_clear();

    st7565_draw_bitmap(
        4,
        11,
        VAARA_LOGO_WIDTH,
        VAARA_LOGO_HEIGHT,
        vaara_logo);

    st7565_update();
}

void swap_temp_humidity(void)
{
    if (gpio_get_level(main_pBit_downKey) == 0)
    {
        vTaskDelay(pdMS_TO_TICKS(30));
        if (gpio_get_level(main_pBit_downKey) == 0)
        {
            swap_screen = !swap_screen;
            save_swap_screen(swap_screen);
            while (gpio_get_level(main_pBit_downKey) == 0)
            {
                vTaskDelay(pdMS_TO_TICKS(10));
            }
        }
    }
}

/* =========================================================
 * PAGE 2 (main screen)
 * ========================================================= */
static void display_page2(void)
{
    st7565_clear();

    swap_temp_humidity();

    if (swap_screen == 0)
    {
        st7565_draw_rect(0, 0, 128, 53, true);
        st7565_draw_horizontal_line(0, 40, 128, true);
        st7565_draw_horizontal_line(0, 12, 128, true);

        st7565_draw_string(30, 43, "TEMPERATURE", true);

        /* Active mode name, right aligned (assumes ~6 px per character) */
        const char *name = mode_names[active_mode];
        st7565_draw_string(126 - (int)strlen(name) * 6, 3, name, true);

        snprintf(buffer, sizeof(buffer), "%.1f", temperature);
        st7565_draw_string_scaled(39, 20, buffer, 2, true);

        snprintf(buffer1, sizeof(buffer1), "%.1f", SET_TEMP);
        st7565_draw_string_scaled(5, 15, buffer1, 1, true);

        snprintf(buffer2, sizeof(buffer2), "%d %%", BMS_soc);
        st7565_draw_string_scaled(75, 55, buffer2, 1, true);

        draw_onoff_status();

        update_charger_logo();

        draw_battery(50, 54, BMS_soc);
        draw_signal(1, 54, value_to_level(csq_value));

        if (gps_connected)
        {
            draw_gps(25, 54, true);
        }
        else
        {
            draw_gps(25, 54, false);
        }
    }

    if (swap_screen == 1)
    {

        st7565_clear();

        st7565_draw_rect(0, 0, 128, 53, true);

        st7565_draw_vertical_line(64, 12, 40, true);

        st7565_draw_horizontal_line(0, 12, 128, true);

        const char *name = mode_names[active_mode];
        st7565_draw_string(126 - (int)strlen(name) * 6, 3, name, true);

        st7565_draw_string_small1(70, 42, "TEMPERATURE", true, true);
        st7565_draw_string_small1(13, 42, "HUMIDITY", true, true);

        snprintf(buffer, sizeof(buffer), "%.1f", temperature);
        st7565_draw_string_scaled(68, 20, buffer, 2, true);

        snprintf(buffer, sizeof(buffer), "%.1f", humidity);
        st7565_draw_string_scaled(18, 20, buffer, 2, true);

        draw_onoff_status();

        snprintf(buffer2, sizeof(buffer2), "%d %%", BMS_soc);
        st7565_draw_string_scaled(75, 55, buffer2, 1, true);
        draw_battery(50, 54, BMS_soc);
        update_charger_logo();
        draw_signal(1, 54, value_to_level(csq_value));

        if (gps_connected)
        {
            draw_gps(25, 54, true);
        }
        else
        {
            draw_gps(25, 54, false);
        }
    }

    st7565_update();
}

/* =========================================================
 * SET TEMPERATURE PAGE
 * ========================================================= */
void set_temperature_page(void)
{
    st7565_clear();

    st7565_draw_rect(0, 0, 128, 53, true);

    st7565_draw_string_scaled(18, 40, "SET TEMPERATURE", 1, true);

    draw_onoff_status();

    snprintf(buffer2, sizeof(buffer2), "%d %%", BMS_soc);
    st7565_draw_string_scaled(75, 55, buffer2, 1, true);

    draw_battery(50, 54, BMS_soc);
    draw_signal(1, 54, value_to_level(csq_value));

    if (gps_connected)
    {
        draw_gps(25, 54, true);
    }
    else
    {
        draw_gps(25, 54, false);
    }

    snprintf(buffer, sizeof(buffer), "%.1f", SET_TEMP);
    st7565_draw_string_scaled(41, 13, buffer, 2, true);
    update_charger_logo();
    draw_arrow_right(100, 5, true);
    draw_arrow_left(10, 5, true);

    st7565_update();
}

// set mode pages here

/* Mode 1: ECO */
void set_eco_mode(void)
{
    st7565_clear();

    st7565_draw_rect(0, 0, 128, 53, true);

    st7565_draw_string_scaled(40, 40, "SET MODE", 1, true);

    draw_onoff_status();

    snprintf(buffer2, sizeof(buffer2), "%d %%", BMS_soc);
    st7565_draw_string_scaled(75, 55, buffer2, 1, true);

    draw_battery(50, 54, BMS_soc);
    draw_signal(1, 54, value_to_level(csq_value));
    update_charger_logo();
    if (gps_connected)
    {
        draw_gps(25, 54, true);
    }
    else
    {
        draw_gps(25, 54, false);
    }

    draw_eco_mode(54, 10, true);
    st7565_draw_string_scaled(57, 2, "ECO", 1, true);

    draw_arrow_right(100, 5, true);
    draw_arrow_left(10, 5, true);

    st7565_update();
}

/* Mode 2: NORMAL */
void set_normal_mode(void)
{
    st7565_clear();

    st7565_draw_rect(0, 0, 128, 53, true);

    st7565_draw_string_scaled(40, 40, "SET MODE", 1, true);

    draw_onoff_status();

    snprintf(buffer2, sizeof(buffer2), "%d %%", BMS_soc);

    st7565_draw_string_scaled(75, 55, buffer2, 1, true);

    draw_battery(50, 54, BMS_soc);
    draw_signal(1, 54, value_to_level(csq_value));
    if (gps_connected)
    {
        draw_gps(25, 54, true);
    }
    else
    {
        draw_gps(25, 54, false);
    }
    update_charger_logo();
    st7565_draw_string_scaled(46, 2, "NORMAL", 1, true);
    draw_normal_mode(55, 15, true);
    draw_arrow_right(100, 5, true);
    draw_arrow_left(10, 5, true);

    st7565_update();
}

/* Mode 3: AUTO */
void set_auto_mode(void)
{
    st7565_clear();

    st7565_draw_rect(0, 0, 128, 53, true);

    st7565_draw_string_scaled(40, 40, "SET MODE", 1, true);

    draw_onoff_status();

    snprintf(buffer2, sizeof(buffer2), "%d %%", BMS_soc);

    st7565_draw_string_scaled(75, 55, buffer2, 1, true);
    update_charger_logo();
    draw_battery(50, 54, BMS_soc);
    draw_signal(1, 54, value_to_level(csq_value));

    if (gps_connected)
    {
        draw_gps(25, 54, true);
    }
    else
    {
        draw_gps(25, 54, false);
    }
    st7565_draw_string_scaled(52, 2, " AUTO", 1, true);
    draw_auto_mode(53, 12, true);

    draw_arrow_right(100, 5, true);
    draw_arrow_left(10, 5, true);

    st7565_update();
}

/* Mode 4: POWER */
void set_power_mode(void)
{
    st7565_clear();

    st7565_draw_rect(0, 0, 128, 53, true);

    st7565_draw_string_scaled(40, 40, "SET MODE", 1, true);

    draw_onoff_status();

    snprintf(buffer2, sizeof(buffer2), "%d %%", BMS_soc);
    st7565_draw_string_scaled(75, 55, buffer2, 1, true);

    draw_battery(50, 54, BMS_soc);
    draw_signal(1, 54, value_to_level(csq_value));

    if (gps_connected)
    {
        draw_gps(25, 54, true);
    }
    else
    {
        draw_gps(25, 54, false);
    }

    st7565_draw_string_scaled(51, 2, " POWER", 1, true);
    draw_bolt(55, 15, true);
    update_charger_logo();
    draw_arrow_right(100, 5, true);
    draw_arrow_left(10, 5, true);

    st7565_update();
}

void show_bms_parameters(void)
{
    st7565_clear();

    st7565_draw_rect(0, 0, 128, 53, true);

    st7565_draw_vertical_line(64, 12, 40, true);
    st7565_draw_horizontal_line(0, 12, 128, true);

    st7565_draw_string_scaled(109, 38, "SOC", 1, true);
    snprintf(buffer3, sizeof(buffer3), "%04d ", BMS_soc);
    st7565_draw_string_8x10(60, 37, buffer3, true);

    st7565_draw_string_scaled(109, 18, "CYC", 1, true);
    snprintf(buffer3, sizeof(buffer3), "%04d ", (int)BMS_Cycles);
    st7565_draw_string_8x10(60, 17, buffer3, true);

    ///////////////////////////////////////////////////////////////////////////////

    st7565_draw_string_scaled(45, 38, "SOH", 1, true);
    snprintf(buffer3, sizeof(buffer3), "%04d ", (int)BMS_soh);
    st7565_draw_string_8x10(-5, 37, buffer3, true);

    st7565_draw_string_scaled(45, 18, "CDS", 1, true);
    snprintf(buffer3, sizeof(buffer3), "%04d ", Cds);
    st7565_draw_string_8x10(-5, 17, buffer3, true);

    const char *name = mode_names[active_mode];
    st7565_draw_string(126 - (int)strlen(name) * 6, 3, name, true);

    draw_onoff_status();

    snprintf(buffer2, sizeof(buffer2), "%d %%", BMS_soc);
    st7565_draw_string_scaled(75, 55, buffer2, 1, true);
    update_charger_logo();
    draw_battery(50, 54, BMS_soc);
    draw_signal(1, 54, value_to_level(csq_value));

    if (gps_connected)
    {
        draw_gps(25, 54, true);
    }
    else
    {
        draw_gps(25, 54, false);
    }

    st7565_update();
}

void show_compresser_parameters(void)
{
    st7565_clear();

    st7565_draw_rect(0, 0, 128, 53, true);

    st7565_draw_vertical_line(64, 12, 40, true);
    st7565_draw_horizontal_line(0, 12, 128, true);

    st7565_draw_string_scaled(109, 38, "TEM", 1, true);
    snprintf(buffer3, sizeof(buffer3), "%04d ", compresser_temp);
    st7565_draw_string_8x10(60, 37, buffer3, true);

    st7565_draw_string_scaled(109, 18, "SPD", 1, true);
    snprintf(buffer3, sizeof(buffer3), "%04d ", compresser_spd);
    st7565_draw_string_8x10(60, 17, buffer3, true);

    ///////////////////////////////////////////////////////////////////////////////

    st7565_draw_string_scaled(45, 38, "VLT", 1, true);
    snprintf(buffer3, sizeof(buffer3), "%04d ", compresser_voltage);
    st7565_draw_string_8x10(-5, 37, buffer3, true);

    st7565_draw_string_scaled(45, 18, "CUR", 1, true);
    snprintf(buffer3, sizeof(buffer3), "%04d ", compresser_current);
    st7565_draw_string_8x10(-5, 17, buffer3, true);

    const char *name = mode_names[active_mode];
    st7565_draw_string(126 - (int)strlen(name) * 6, 3, name, true);

    draw_onoff_status();

    snprintf(buffer2, sizeof(buffer2), "%d %%", BMS_soc);
    st7565_draw_string_scaled(75, 55, buffer2, 1, true);
    update_charger_logo();
    draw_battery(50, 54, BMS_soc);
    draw_signal(1, 54, value_to_level(csq_value));

    if (gps_connected)
    {
        draw_gps(25, 54, true);
    }
    else
    {
        draw_gps(25, 54, false);
    }

    st7565_update();
}

static void show_mode_page(op_mode_t mode)
{
    switch (mode)
    {
    case MODE_ECO:
        set_eco_mode();
        break;
    case MODE_NORMAL:
        set_normal_mode();
        break;
    case MODE_AUTO:
        set_auto_mode();
        break;
    case MODE_POWER:
        set_power_mode();
        break;
    default:
        break;
    }
}

/* =========================================================
 * SCREEN REFRESH + BUTTON ACTIONS
 * ========================================================= */
static void redraw(void)
{
    switch (ui_state)
    {
    case UI_MAIN:
        display_page2();
        break;
    case UI_SET_TEMP:
        set_temperature_page();
        break;
    case UI_SET_MODE:
        show_mode_page(active_mode);
        break;

    case UI_BMS_PARAMETERS:
        if (swap_screen == 0)
        {
            show_bms_parameters();
        }
        else
        {
            show_compresser_parameters();
        }
        break;

    default:
        break;
    }
}

/* Long press: main -> set temp -> set mode -> parameters -> main */
static void on_long_press(void)
{
    switch (ui_state)
    {
    case UI_MAIN:
        ui_state = UI_SET_TEMP;
        break;

    case UI_SET_TEMP:
        save_set_temp(); /* save the setpoint once, when leaving the page */
        ui_state = UI_SET_MODE;
        break;

    case UI_SET_MODE:
        ui_state = UI_BMS_PARAMETERS;
        break;

    case UI_BMS_PARAMETERS:
        ui_state = UI_MAIN;
        break;

    default:
        ui_state = UI_MAIN;
        break;
    }

    redraw();
}

/* UP key: temperature up / next mode */
static void on_up(bool repeat)
{
    switch (ui_state)
    {
    case UI_SET_TEMP:
        SET_TEMP += SET_TEMP_STEP;
        if (SET_TEMP > SET_TEMP_MAX)
            SET_TEMP = SET_TEMP_MAX;
        redraw();
        break;

    case UI_SET_MODE:
        /* ECO -> NORMAL -> AUTO -> POWER -> ECO ... */
        if (!repeat) /* no fast repeat when shifting modes */
        {
            active_mode = (op_mode_t)((active_mode + 1) % MODE_COUNT);
            save_active_mode(active_mode);
            redraw();
        }
        break;

    default:
        break;
    }
}

/* DOWN key: temperature down / previous mode */
static void on_down(bool repeat)
{
    switch (ui_state)
    {
    case UI_SET_TEMP:
        SET_TEMP -= SET_TEMP_STEP;
        if (SET_TEMP < SET_TEMP_MIN)
            SET_TEMP = SET_TEMP_MIN;
        redraw();
        break;

    case UI_SET_MODE:
        /* ECO <- NORMAL <- AUTO <- POWER <- ECO ... */
        if (!repeat)
        {
            active_mode = (op_mode_t)((active_mode + MODE_COUNT - 1) % MODE_COUNT);
            save_active_mode(active_mode);
            redraw();
        }
        break;

    default:
        break;
    }
}

void send_json_uart2(void)
{
    door_event_t batch[DOOR_EVENT_COUNT];
    bool batch_available = false;

    int len;
    int tx_bytes;
    size_t offset;

    if (door_mutex == NULL)
    {
        ESP_LOGE(JSON_TAG, "Door mutex is NULL");
        return;
    }

    /* STEP 1: Copy the batch if five events are ready */
    xSemaphoreTake(door_mutex, portMAX_DELAY);

    if (door_batch_ready &&
        door_event_count == DOOR_EVENT_COUNT)
    {
        memcpy(batch, door_events, sizeof(batch));
        batch_available = true;
    }

    xSemaphoreGive(door_mutex);

    /* STEP 2: Build door entries; on failure send telemetry only */
    door_json[0] = '\0';

    if (batch_available && !build_door_event_array(batch))
    {
        ESP_LOGE(JSON_TAG, "Door JSON failed, sending telemetry only");
        batch_available = false;
        door_json[0] = '\0';
    }

    /* STEP 3: Telemetry object (opens the top-level array) */
    len = snprintf(
        json_buffer,
        sizeof(json_buffer),

        "[{\"ts\":%lld000,\"values\":{"
        "\"IME\":\"%s\","
        "\"lat\":\"%s\","
        "\"lng\":\"%s\","
        "\"SIG\":%u,"
        "\"Gsp\":\"%s\","
        "\"Tsn\":%.2f,"
        "\"Hum\":%.2f,"
        "\"Pwm\":%u,"
        "\"Tco\":%u,"
        "\"Dcv\":%u,"
        "\"Spd\":%u,"
        "\"Cur\":%u,"
        "\"Cds\":%u,"
        "\"SoC\":%u,"
        "\"SoH\":%u,"
        "\"Cyc\":%u,"
        "\"Bvl\":%u,"
        "\"Bcr\":%u,"
        "\"Bfl\":%u,"
        "\"Mod\":%u,"
        "\"Stt\":%s,"
        "\"Flt\":%u",

        (long long)(rtc_time.tv_sec - 20),

        imei_number,
        latitude,
        longitude,
        (unsigned int)csq_value,
        speed,
        (double)temperature,
        (double)humidity,
        (unsigned int)pwm_value,
        (unsigned int)compresser_temp,
        (unsigned int)compresser_voltage,
        (unsigned int)compresser_spd,
        (unsigned int)compresser_current,
        (unsigned int)Cds,
        (unsigned int)BMS_soc,
        (unsigned int)BMS_soh,
        (unsigned int)BMS_Cycles,
        (unsigned int)BMS_volt,
        (unsigned int)BMS_cur,
        (unsigned int)Bfl,
        (unsigned int)mode_no_flag,
        Stt,
        (unsigned int)Flt);

    if (len < 0 || (size_t)len >= sizeof(json_buffer))
    {
        ESP_LOGE(JSON_TAG, "Telemetry JSON buffer overflow");
        return;
    }

    offset = (size_t)len;

    /*
     * STEP 4: Close the telemetry object, append the door entries
     * (empty string when no batch), then close the array.
     */
    len = snprintf(
        json_buffer + offset,
        sizeof(json_buffer) - offset,
        "}}%s]\r\n",
        door_json);

    if (len < 0 ||
        (size_t)len >= sizeof(json_buffer) - offset)
    {
        ESP_LOGE(JSON_TAG, "JSON buffer too small");
        return;
    }

    offset += (size_t)len;

    /* STEP 5: Send everything in ONE UART write */
    tx_bytes = uart_write_bytes(UART_NUM_2, json_buffer, offset);

    if (tx_bytes != (int)offset)
    {
        ESP_LOGE(JSON_TAG, "UART2 transmission failed");
        return;
    }

    if (uart_wait_tx_done(UART_NUM_2, pdMS_TO_TICKS(1000)) != ESP_OK)
    {
        ESP_LOGE(JSON_TAG, "UART2 TX timeout");
        return;
    }

    if (batch_available)
    {
        ESP_LOGI(JSON_TAG, "Telemetry + 5 door events transmitted");
    }
    else
    {
        ESP_LOGI(JSON_TAG, "Telemetry transmitted; no full door batch");
    }

    ESP_LOGI(JSON_TAG, "%s", json_buffer);

    /* STEP 6: Clear the batch only after a successful transmit */
    if (batch_available)
    {
        xSemaphoreTake(door_mutex, portMAX_DELAY);

        if (door_batch_ready &&
            door_event_count == DOOR_EVENT_COUNT)
        {
            door_event_count = 0;
            door_batch_ready = false;

            ESP_LOGI(DOOR_TAG, "Door batch cleared; collecting next 5 events");
        }

        xSemaphoreGive(door_mutex);
    }
}

void init(void)
{
    const uart_config_t uart_config = {
        .baud_rate = BG95_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_2, RX_BUF_SIZE * 2, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_NUM_2, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART_NUM_2, TXD_PIN, RXD_PIN,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
}

static void rx_task(void *arg)
{
    static const char *RX_TASK_TAG = "BG95_RSP";
    (void)arg;

    esp_log_level_set(RX_TASK_TAG, ESP_LOG_INFO);

    uint8_t *data = malloc(RX_BUF_SIZE + 1);

    if (data == NULL)
    {
        ESP_LOGE(RX_TASK_TAG, "Memory allocation failed");
        vTaskDelete(NULL);
        return;
    }

    /* GPS response accumulator */
    char gps_acc[512] = {0};
    size_t gps_acc_len = 0;

    /* IMEI line buffer */
    char imei_line[64] = {0};
    size_t imei_line_len = 0;
    bool expect_imei_line = false;

    /* CSQ line buffer */
    char csq_line[64] = {0};
    size_t csq_line_len = 0;

    size_t read_size = sizeof(bg95_rsp_buff) - 1;

    if (read_size > RX_BUF_SIZE)
        read_size = RX_BUF_SIZE;

    while (1)
    {
        int rxBytes = uart_read_bytes(
            UART_NUM_2,
            data,
            read_size,
            pdMS_TO_TICKS(1000));

        if (rxBytes <= 0)
            continue;

        data[rxBytes] = '\0';

        /* Save latest received chunk */
        memcpy(bg95_rsp_buff, data, (size_t)rxBytes);
        bg95_rsp_buff[rxBytes] = '\0';

        ESP_LOGI(RX_TASK_TAG,
                 "RX[%d]: %s",
                 rxBytes, (char *)data);

        /* =====================================================
         * 1. IMEI PARSING
         * ===================================================== */
        if (!imei_ready)
        {
            for (int i = 0; i < rxBytes; i++)
            {
                char ch = (char)data[i];

                if (ch == '\r' || ch == '\n')
                {
                    if (imei_line_len > 0)
                    {
                        imei_line[imei_line_len] = '\0';

                        process_imei_line(
                            imei_line,
                            &expect_imei_line);

                        imei_line_len = 0;
                        imei_line[0] = '\0';
                    }
                }
                else if (imei_line_len < sizeof(imei_line) - 1)
                {
                    imei_line[imei_line_len++] = ch;
                }
                else
                {
                    imei_line_len = 0;
                    imei_line[0] = '\0';
                    expect_imei_line = false;
                }
            }
        }

        /* =====================================================
         * 2. CSQ PARSING
         * Example: +CSQ: 18,99
         * ===================================================== */
        for (int i = 0; i < rxBytes; i++)
        {
            char ch = (char)data[i];

            if (ch == '\r' || ch == '\n')
            {
                if (csq_line_len > 0)
                {
                    csq_line[csq_line_len] = '\0';

                    if (strncmp(csq_line, "+CSQ:", 5) == 0)
                    {
                        int rssi;
                        int ber;
                        char extra;

                        if (sscanf(csq_line,
                                   "+CSQ: %d,%d %c",
                                   &rssi, &ber, &extra) == 2)
                        {
                            if (((rssi >= 0 && rssi <= 31) ||
                                 rssi == 99) &&
                                ((ber >= 0 && ber <= 7) ||
                                 ber == 99))
                            {
                                csq_value = rssi;
                                csq_ber = ber;
                                csq_ready = true;

                                ESP_LOGI(RX_TASK_TAG,
                                         "CSQ RSSI=%d BER=%d",
                                         csq_value, csq_ber);
                            }
                        }
                    }

                    csq_line_len = 0;
                    csq_line[0] = '\0';
                }
            }
            else if (csq_line_len < sizeof(csq_line) - 1)
            {
                csq_line[csq_line_len++] = ch;
            }
            else
            {
                csq_line_len = 0;
                csq_line[0] = '\0';
            }
        }

        /* =====================================================
         * 3. ACCUMULATE GPS RESPONSE
         * ===================================================== */
        if (gps_acc_len + (size_t)rxBytes < sizeof(gps_acc))
        {
            memcpy(gps_acc + gps_acc_len,
                   data, (size_t)rxBytes);

            gps_acc_len += (size_t)rxBytes;
            gps_acc[gps_acc_len] = '\0';
        }
        else
        {
            ESP_LOGW(RX_TASK_TAG, "GPS accumulator overflow");

            gps_acc_len = 0;
            gps_acc[0] = '\0';

            if ((size_t)rxBytes < sizeof(gps_acc))
            {
                memcpy(gps_acc, data, (size_t)rxBytes);
                gps_acc_len = (size_t)rxBytes;
                gps_acc[gps_acc_len] = '\0';
            }
        }

        /* =====================================================
         * 4. FIND COMPLETE +QGPSLOC RESPONSE
         * ===================================================== */
        char *start1 = strstr(gps_acc, "+QGPSLOC:");

        if (start1 != NULL)
        {
            /* Find OK after the GPS location response */
            char *ok = strstr(start1, "OK");

            if (ok != NULL)
            {
                size_t len1 = (size_t)(ok - start1) + 2;

                if (len1 < sizeof(gps_rtc_buff))
                {
                    gps_rtc_ready = false;

                    memcpy(gps_rtc_buff, start1, len1);
                    gps_rtc_buff[len1] = '\0';

                    /* Parse BG95 format:
                     * +QGPSLOC: UTC,latitude,N/S,longitude,E/W,... */
                    char gps_copy[sizeof(gps_rtc_buff)];

                    memcpy(gps_copy,
                           gps_rtc_buff,
                           len1 + 1);

                    bool valid_fix = false;

                    char *colon = strchr(gps_copy, ':');

                    if (colon != NULL)
                    {
                        char *saveptr = NULL;

                        char *utc = strtok_r(
                            colon + 1, ",", &saveptr);

                        char *lat = strtok_r(
                            NULL, ",", &saveptr);

                        char *ns = strtok_r(
                            NULL, ",", &saveptr);

                        char *lon = strtok_r(
                            NULL, ",", &saveptr);

                        char *ew = strtok_r(
                            NULL, ",", &saveptr);

                        if (utc != NULL &&
                            lat != NULL &&
                            ns != NULL &&
                            lon != NULL &&
                            ew != NULL)
                        {
                            char *lat_end;
                            char *lon_end;

                            double lat_dm = strtod(lat, &lat_end);
                            double lon_dm = strtod(lon, &lon_end);

                            /* BG95 uses DDMM.MMMM and DDDMM.MMMM */
                            bool numeric =
                                lat_end != lat &&
                                *lat_end == '\0' &&
                                lon_end != lon &&
                                *lon_end == '\0';

                            bool directions =
                                (strcmp(ns, "N") == 0 ||
                                 strcmp(ns, "S") == 0) &&
                                (strcmp(ew, "E") == 0 ||
                                 strcmp(ew, "W") == 0);

                            double lat_deg = (int)(lat_dm / 100.0);
                            double lon_deg = (int)(lon_dm / 100.0);

                            double lat_min =
                                lat_dm - (lat_deg * 100.0);

                            double lon_min =
                                lon_dm - (lon_deg * 100.0);

                            bool coordinates_valid =
                                lat_deg <= 90.0 &&
                                lon_deg <= 180.0 &&
                                lat_min >= 0.0 &&
                                lat_min < 60.0 &&
                                lon_min >= 0.0 &&
                                lon_min < 60.0 &&
                                (lat_deg < 90.0 || lat_min == 0.0) &&
                                (lon_deg < 180.0 || lon_min == 0.0);

                            if (numeric &&
                                directions &&
                                coordinates_valid)
                            {
                                valid_fix = true;
                            }

                            ESP_LOGI(RX_TASK_TAG,
                                     "UTC=%s LAT=%s %s LON=%s %s",
                                     utc, lat, ns, lon, ew);
                        }
                    }

                    /* Update shared GPS status */
                    gpsFix = valid_fix;
                    gps_connected = valid_fix;
                    gps_rtc_ready = true;

                    ESP_LOGI(RX_TASK_TAG,
                             "GPS CONNECTED=%d",
                             (int)gps_connected);
                }

                /* Clear processed GPS response */
                gps_acc_len = 0;
                gps_acc[0] = '\0';
            }
        }

        /* =====================================================
         * 5. SAFETY RESET
         * ===================================================== */
        if (gps_acc_len >= sizeof(gps_acc) - 1)
        {
            ESP_LOGW(RX_TASK_TAG,
                     "Incomplete GPS response; resetting");

            gps_acc_len = 0;
            gps_acc[0] = '\0';
        }
    }

    /* Normally unreachable */
    free(data);
    vTaskDelete(NULL);
}

/* ================================================================== */
/* GPS parsing                                                         */
/* ================================================================== */

/* "1833.952200" -> "18 33.952200"  (2 digits = latitude, 3 = longitude) */
static void format_lat_lon(const char *input, char *output, size_t out_size, int degree_digits)
{
    size_t in_len = strlen(input);
    if (in_len <= (size_t)degree_digits || out_size < in_len + 2)
    {
        output[0] = '\0';
        return;
    }
    memcpy(output, input, degree_digits);
    output[degree_digits] = ' ';
    strcpy(output + degree_digits + 1, input + degree_digits);
}

/*
 * +QGPSLOC: 051055.000,1833.952200,N,07346.086180,E,2.6,558.7,3,0.00,0.0,10.8,291024,04
 *           time       lat         NS lon          EW hdop alt fix cog  kmh spkn date  nsat
 *
 * Returns true only if every field parsed; outputs untouched on failure.
 */
bool extract_gps_info(const char *gpsrtc_buff,
                      char *latitude,
                      char *longitude,
                      char *gpsdate,
                      char *gpstime,
                      char *gpsspeed)
{
    char temp_buff[256];
    strncpy(temp_buff, gpsrtc_buff, sizeof(temp_buff) - 1);
    temp_buff[sizeof(temp_buff) - 1] = '\0';

    char *data = strstr(temp_buff, "+QGPSLOC:");
    if (data == NULL)
    {
        ESP_LOGW(TAG2, "QGPSLOC not found");
        return false;
    }
    data += strlen("+QGPSLOC:");
    while (*data == ' ')
        data++;

    char lat[20] = {0}, lon[20] = {0}, spd[8] = {0}, dat[8] = {0}, tim[8] = {0};
    char *save = NULL;
    char *token;

    /* TIME */
    token = strtok_r(data, ",", &save);
    if (token == NULL || strlen(token) < 6)
        return false;
    memcpy(tim, token, 6);

    /* LATITUDE, N/S */
    token = strtok_r(NULL, ",", &save);
    if (token == NULL)
        return false;
    format_lat_lon(token, lat, sizeof(lat), 2);
    if (lat[0] == '\0')
        return false;
    strtok_r(NULL, ",", &save);

    /* LONGITUDE, E/W */
    token = strtok_r(NULL, ",", &save);
    if (token == NULL)
        return false;
    format_lat_lon(token, lon, sizeof(lon), 3);
    if (lon[0] == '\0')
        return false;
    strtok_r(NULL, ",", &save);

    /* skip HDOP, ALTITUDE, FIX, COURSE */
    for (int i = 0; i < 4; i++)
    {
        if (strtok_r(NULL, ",", &save) == NULL)
            return false;
    }

    /* SPEED km/h */
    token = strtok_r(NULL, ",", &save);
    if (token == NULL)
        return false;
    strncpy(spd, token, 5);

    /* skip speed knots */
    if (strtok_r(NULL, ",", &save) == NULL)
        return false;

    /* DATE ddmmyy */
    token = strtok_r(NULL, ",", &save);
    if (token == NULL || strlen(token) < 6)
        return false;
    memcpy(dat, token, 6);

    strcpy(latitude, lat);
    strcpy(longitude, lon);
    strcpy(gpsspeed, spd);
    strcpy(gpsdate, dat);
    strcpy(gpstime, tim);
    return true;
}

/* ================================================================== */
/* RTC                                                                 */
/* ================================================================== */

/* gps_date = YYYYMMDD, gps_time = HHMMSS (UTC) */
void set_rtc_time(int gps_date, int gps_time)
{
    struct tm t = {0};
    t.tm_year = (gps_date / 10000) - 1900;
    t.tm_mon = ((gps_date / 100) % 100) - 1;
    t.tm_mday = gps_date % 100;
    t.tm_hour = gps_time / 10000;
    t.tm_min = (gps_time / 100) % 100;
    t.tm_sec = gps_time % 100;
    t.tm_isdst = 0;

    if (t.tm_year < 124 || t.tm_mon < 0 || t.tm_mon > 11 ||
        t.tm_mday < 1 || t.tm_mday > 31 ||
        t.tm_hour > 23 || t.tm_min > 59 || t.tm_sec > 60)
    {
        ESP_LOGW("RTC", "Invalid GPS date/time: %d %d", gps_date, gps_time);
        return;
    }

    /* GPS is UTC: no timezone / DST in mktime() */
    setenv("TZ", "UTC0", 1);
    tzset();

    time_t epoch = mktime(&t);
    if (epoch == (time_t)-1)
    {
        ESP_LOGE("RTC", "mktime() failed");
        return;
    }

    struct timespec ts = {.tv_sec = epoch, .tv_nsec = 0};
    if (clock_settime(CLOCK_REALTIME, &ts) != 0)
    {
        ESP_LOGE("RTC", "clock_settime() failed errno=%d", errno);
        return;
    }

    ESP_LOGI("RTC", "RTC set: %04d-%02d-%02d %02d:%02d:%02d UTC epoch=%lld",
             t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
             t.tm_hour, t.tm_min, t.tm_sec, (long long)epoch);
}

void get_rtc_time(struct timespec *rtc_time)
{
    if (clock_gettime(CLOCK_REALTIME, rtc_time) != 0)
    {
        rtc_time->tv_sec = 0;
        rtc_time->tv_nsec = 0;
    }
}

void rtc_time_update_task(void *pvParameters)
{
    while (1)
    {
        get_rtc_time(&rtc_time);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/* ================================================================== */
/* GPS -> RTC                                                          */
/* ================================================================== */
static void gps_process_task(void *arg)
{
    while (1)
    {
        if (gps_rtc_ready)
        {
            if (extract_gps_info((char *)gps_rtc_buff, latitude, longitude,
                                 date_buff, time_buff, speed))
            {
                /* ddmmyy -> YYYYMMDD */
                int dd = (date_buff[0] - '0') * 10 + (date_buff[1] - '0');
                int mm = (date_buff[2] - '0') * 10 + (date_buff[3] - '0');
                int yy = (date_buff[4] - '0') * 10 + (date_buff[5] - '0');

                int gsp_date = (2000 + yy) * 10000 + mm * 100 + dd;
                int gsp_time = atoi(time_buff); /* "051055" -> 05:10:55 */

                set_rtc_time(gsp_date, gsp_time);
            }
            else
            {
                ESP_LOGW(TAG2, "GPS parse failed");
            }

            memset(gps_rtc_buff, '\0', sizeof(gps_rtc_buff));
            gps_rtc_ready = false;
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

#if GPS_POLL_MODEM
static void gps_poll_task(void *arg)
{
    const char *on = "AT+QGPS=1\r\n";
    uart_write_bytes(UART_NUM_2, on, strlen(on));
    vTaskDelay(pdMS_TO_TICKS(2000));
    while (1)
    {
        const char *loc = "AT+QGPSLOC=2\r\n";
        uart_write_bytes(UART_NUM_2, loc, strlen(loc));
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
#endif

static void process_imei_line(char *line,
                              bool *expect_imei_line)
{
    /* Remove leading whitespace */
    while (*line && isspace((unsigned char)*line))
    {
        line++;
    }

    /* Remove trailing whitespace */
    size_t len = strlen(line);

    while (len > 0 &&
           isspace((unsigned char)line[len - 1]))
    {
        line[--len] = '\0';
    }

    if (len == 0)
    {
        return;
    }

    /* Accept a standalone 15-digit IMEI line */
    if (len == 15)
    {
        bool valid = true;

        for (size_t i = 0; i < 15; i++)
        {
            if (!isdigit((unsigned char)line[i]))
            {
                valid = false;
                break;
            }
        }

        if (valid)
        {
            memcpy(imei_number, line, 15);
            imei_number[15] = '\0';
            imei_ready = true;

            ESP_LOGI("BG95_RSP",
                     "IMEI RECEIVED: %s",
                     imei_number);

            *expect_imei_line = false;
            return;
        }
    }
}

/* ---- Helpers ---- */

/* Main-screen state that matches the ON/OFF key:
 * compressor ON  -> SYS_RUN
 * compressor OFF -> SYS_IDLE */
static sys_state_t home_state(void)
{
    return onOffState ? SYS_RUN : SYS_IDLE;
}

static void change_state(sys_state_t next)
{
    if (next == sys_state)
        return;

    ESP_LOGI(TAG, "STATE %d -> %d", sys_state, next);
    sys_state = next;

    /* entry actions */
    switch (next)
    {
    case SYS_IDLE:
    case SYS_RUN:
        ui_state = UI_MAIN;
        break;
    case SYS_CONFIG:
        /* ui_state already set by on_long_press() */
        break;
    case SYS_ERROR:
        /* compressor is already forced OFF by compressor_task (temp_fault) */
        break;
    }
    redraw();
}

static void enter_error(sys_error_t err)
{
    sys_error = err;
    change_state(SYS_ERROR);
}

/* Reads the sensor; returns true on a good reading */
static bool read_temperature(void)
{
    float t = ds18b20_get_temp();

    if (!isnan(t) && !isinf(t))
    {
        temperature = t;
        temp_fail_cnt = 0;
        return true;
    }

    temperature = 0.0f; /* display only; control uses ctrl_temp */
    if (temp_fail_cnt < 255)
        temp_fail_cnt++;
    return false;
}

/* =========================================================
 * COMPRESSOR / PWM CONTROL
 * ========================================================= */

static int mode_code(op_mode_t m) /* value sent in JSON "Mod" */
{
    switch (m)
    {
    case MODE_NORMAL:
        return 1;
    case MODE_AUTO:
        return 2;
    case MODE_ECO:
        return 3;
    case MODE_POWER:
        return 4;
    default:
        return 1;
    }
}

/* UI mode -> mode number used by the original control logic */
static unsigned int mode_to_sysmode(op_mode_t m)
{
    switch (m)
    {
    case MODE_ECO:
        return defn_val_sysModeEco;
    case MODE_POWER:
        return defn_val_sysModePower;
    case MODE_NORMAL:
        return defn_val_sysModeNormal;
    case MODE_AUTO:
    default:
        return defn_val_sysModeAuto;
    }
}

//-----------------------------------------------------------------------------------------------------------------------------------------
//--------------------------- ORIGINAL PWM CONTROL LOGIC (unchanged, only names of pins/types adapted) -----------------------------------
//-----------------------------------------------------------------------------------------------------------------------------------------
static void sFvProcessPwmControlModeEco(void)                                                    // Process ECO PWM control mode
{                                                                                                // Start of Process ECO PWM control mode
    main_intr_pwrModeTimer = Clear;                                                              // Clear the timer
    if (main_glob_limitTemp < main_intr_snsTemp)                                                 // Check if
    {                                                                                            // Start Processing the PWM
        main_prog_tempDifference = main_intr_snsTemp - main_glob_limitTemp;                      // Get the difference
        if ((main_prog_divPrevStatus == Set) && (main_prog_tempDifference < defn_val_returnHys)) // If hyst flag set and value below hyst
        {
            main_intr_divPwmIndx = defn_val_autoOff; // CLear PWM
        }
        else
        {
            main_prog_divPrevStatus = Clear;                  // CLear Hyst flag
            if (main_prog_tempDifference <= defn_lim_autoOff) // If value below limit
            {
                main_intr_divPwmIndx = defn_val_autoOff; // Assign the value for off
                main_prog_divPrevStatus = Set;           // Set Hyst flag
            }
            else if (main_prog_tempDifference <= defn_lim_autoLimL) // If value above limit
                main_intr_divPwmIndx = defn_val_autoL;              // Assign the value for low
            else                                                    // Exception
                main_intr_divPwmIndx = defn_val_autoL;              // Assign the value for high
        }
    } // End   Processing the PWM
    else                              // Oter condition when value fall other side
    {                                 // Start for negative difference
        main_intr_divPwmIndx = Clear; // Clear the PWM
    } // End   for negative difference
} // End   of Process ECO PWM control mode
//-----------------------------------------------------------------------------------------------------------------------------------------
static void sFvProcessPwmControlModePower(void)                                                  // Process Power PWM control mode
{                                                                                                // Start of Process Power PWM control mode
    if (main_glob_limitTemp < main_intr_snsTemp)                                                 // Check if
    {                                                                                            // Start Processing the PWM
        main_prog_tempDifference = main_intr_snsTemp - main_glob_limitTemp;                      // Get the difference
        if ((main_prog_divPrevStatus == Set) && (main_prog_tempDifference < defn_val_returnHys)) // If hyst flag set and value below hyst
        {
            main_intr_divPwmIndx = defn_val_autoOff; // CLear PWM
        }
        else
        {
            main_prog_divPrevStatus = Clear;                  // CLear Hyst flag
            if (main_prog_tempDifference <= defn_lim_autoOff) // If value below limit
            {
                main_prog_divPrevStatus = Set;           // Set Hyst flag
                main_intr_divPwmIndx = defn_val_autoOff; // Assign the value for off
            }
            else if (main_prog_tempDifference <= defn_lim_autoLimL) // If value above limit
                main_intr_divPwmIndx = defn_val_autoL;              // Assign the value for low
            else                                                    // Exception
                main_intr_divPwmIndx = defn_val_autoH;              // Assign the value for high
        }
    } // End   Processing the PWM
    else                              // Oter condition when value fall other side
    {                                 // Start for negative difference
        main_intr_divPwmIndx = Clear; // Clear the PWM
    } // End   for negative difference
} // End   of Process Power PWM control mode
//-----------------------------------------------------------------------------------------------------------------------------------------
static void sFvProcessPwmControlModeNormal(void)                                                 // Process ECO Normalcontrol mode
{                                                                                                // Start of Process Normal PWM control mode
    main_intr_pwrModeTimer = Clear;                                                              // Clear the timer
    if (main_glob_limitTemp < main_intr_snsTemp)                                                 // Check if
    {                                                                                            // Start Processing the PWM
        main_prog_tempDifference = main_intr_snsTemp - main_glob_limitTemp;                      // Get the difference
        if ((main_prog_divPrevStatus == Set) && (main_prog_tempDifference < defn_val_returnHys)) // If hyst flag set and value below hyst
        {
            main_intr_divPwmIndx = defn_val_autoOff; // CLear PWM
        }
        else
        {
            main_prog_divPrevStatus = Clear;                  // CLear Hyst flag
            if (main_prog_tempDifference <= defn_lim_autoOff) // If value below limit
            {
                main_prog_divPrevStatus = Set;           // Set Hyst flag
                main_intr_divPwmIndx = defn_val_autoOff; // Assign the value for off
            }
            else if (main_prog_tempDifference <= defn_lim_autoLimL) // If value above limit
                main_intr_divPwmIndx = defn_val_autoL;              // Assign the value for low
            else                                                    // Exception
                main_intr_divPwmIndx = defn_val_autoM;              // Assign the value for high
        }
    } // End   Processing the PWM
    else                              // Oter condition when value fall other side
    {                                 // Start for negative difference
        main_intr_divPwmIndx = Clear; // Clear the PWM
    } // End   for negative difference
} // End   of Process Normal PWM control mode
//-----------------------------------------------------------------------------------------------------------------------------------------
static void sFvProcessPwmControlModeAuto(void)                                                   // Process ECO Auto control mode
{                                                                                                // Start of Process Auto PWM control mode
    main_intr_pwrModeTimer = Clear;                                                              // Clear the timer
    if (main_glob_limitTemp < main_intr_snsTemp)                                                 // Check if
    {                                                                                            // Start Processing the PWM
        main_prog_tempDifference = main_intr_snsTemp - main_glob_limitTemp;                      // Get the difference
        if ((main_prog_divPrevStatus == Set) && (main_prog_tempDifference < defn_val_returnHys)) // If hyst flag set and value below hyst
        {
            main_intr_divPwmIndx = defn_val_autoOff; // CLear PWM
        }
        else
        {
            main_prog_divPrevStatus = Clear;                  // CLear Hyst flag
            if (main_prog_tempDifference <= defn_lim_autoOff) // If value below limit
            {
                main_prog_divPrevStatus = Set;           // Set Hyst flag
                main_intr_divPwmIndx = defn_val_autoOff; // Assign the value for off
            }
            else if (main_prog_tempDifference <= defn_lim_autoLimL) // If value above limit
                main_intr_divPwmIndx = defn_val_autoL;              // Assign the value for low
            else if (main_prog_tempDifference <= defn_lim_autoLimM) // If value above limit
                main_intr_divPwmIndx = defn_val_autoM;              // Assign the value for mid
            else if (defn_lim_autoLimH < main_prog_tempDifference)  // If value above limit
                main_intr_divPwmIndx = defn_val_autoH;              // Assign the value for high
            else                                                    // Exception
                main_intr_divPwmIndx = defn_val_autoH;              // Assign the value for high
        }
    } // End   Processing the PWM
    else                              // Oter condition when value fall other side
    {                                 // Start for negative difference
        main_intr_divPwmIndx = Clear; // Clear the PWM
    } // End   for negative difference
} // End   of Process Auto PWM control mode
//-----------------------------------------------------------------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------------------------------------------------------------
static void sFvProcessPwmControl(void)    // Function to control the PWM signal as needed
{                                         // Start of function to control the PWM
    if (vc_main_prog_systemStatus == Set) // If need to control the System status
    {
        gpio_set_level(main_pBit_onOffCtrl_for_compresser, COMP_RELAY_ON_LEVEL);      // Start generating the Control SIgnal for PWM
        if (vc_main_prog_systemMode == defn_val_sysModeEco)                           // if operation mode is Eco
            sFvProcessPwmControlModeEco();                                            // Process ECO PWM control mode
        else if (vc_main_prog_systemMode == defn_val_sysModePower)                    // if operation mode is Power
            sFvProcessPwmControlModePower();                                          // Process Power PWM control mode
        else if (vc_main_prog_systemMode == defn_val_sysModeNormal)                   // if operation mode is Normal
            sFvProcessPwmControlModeNormal();                                         // Mode Normal
        else                                                                          // In case auto mode
            sFvProcessPwmControlModeAuto();                                           // Mode auto
        if (main_intr_divPwmIndx == defn_val_autoOff)                                 // If need to turn Off the Out put relay
        {                                                                             // Tunr Off the relay [Force Off-Amol-1502]
            gpio_set_level(main_pBit_onOffCtrl_for_compresser, COMP_RELAY_OFF_LEVEL); // Off the Port pin
        } // End condition t turn off the relay
        else                                                                         // Make sure to make it on, if PWM is above limit
        {                                                                            // Make it On
            gpio_set_level(main_pBit_onOffCtrl_for_compresser, COMP_RELAY_ON_LEVEL); // On the Port pin
        } // Make it on
    } // End   generating the Control SIgnal for PWM
    else                                                                          // Condition when need to Off the PWM
    {                                                                             // Say need to off the PWM
        gpio_set_level(main_pBit_onOffCtrl_for_compresser, COMP_RELAY_OFF_LEVEL); // Off the Port pin
        main_intr_divPwmIndx = Clear;                                             // Clear the PWM
        main_intr_pwrModeTimer = Clear;                                           // Clear the timer
    } // End to off the PWM
} // End   of function to control the PWM
//-----------------------------------------------------------------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------------------------------------------------------------

/* Sends main_intr_divPwmIndx to the LEDC PWM (duty %) only when it changes. */
static void compressor_apply_pwm(void)
{
    static int last_duty = -1;

    int duty = (int)main_intr_divPwmIndx;

    if (duty > 100)
        duty = 100;

    pwm_value = duty; /* value shown in JSON "Pwm" */
    mode_no_flag = mode_code(active_mode);

    if (duty != last_duty)
    {
        pwm_set_duty((uint32_t)duty);
        last_duty = duty;
    }
}

static void compressor_kick(void)
{
    if (compressor_task_handle != NULL)
        xTaskNotifyGive(compressor_task_handle);
}

/* Own task: the slow DS18B20 read no longer blocks keys / display.
 * Once per second (or immediately when the ON/OFF key kicks it):
 *   1. read the sensor
 *   2. feed the ORIGINAL variables (0.1 C units)
 *   3. run sFvProcessPwmControl()  (original logic)
 *   4. send the result to the PWM                                        */
static void compressor_task(void *arg)
{
    (void)arg;

    for (;;)
    {
        bool kicked = (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(CTRL_PERIOD_MS)) > 0);

        if (!kicked)
        {
            if (read_temperature())
            {
                temp_fault = false;
                ctrl_temp = temperature;
                ctrl_temp_valid = true;
            }
            else if (temp_fail_cnt >= TEMP_FAIL_LIMIT)
            {
                temp_fault = true;
                ctrl_temp_valid = false;
            }
            /* else: one bad read -> keep last good ctrl_temp */
        }

        /* ---- feed the original control variables ---- */
        main_intr_snsTemp = (int)lroundf(ctrl_temp * 10.0f);
        main_glob_limitTemp = (int)lroundf(SET_TEMP * 10.0f);
        vc_main_prog_systemMode = mode_to_sysmode(active_mode);

        /* system is ON only if the key is ON, the sensor is healthy
         * (valid reading, no fault) and the state machine is not in ERROR */
        bool comp_allowed = onOffState &&
                            ctrl_temp_valid &&
                            !temp_fault &&
                            (sys_state != SYS_ERROR);

        if (comp_allowed)
        {
            vc_main_prog_systemStatus = Set;
        }
        else
        {
            vc_main_prog_systemStatus = Clear;
            main_prog_divPrevStatus = Clear; /* fresh hysteresis on next start */
        }

        /* ---- original logic ---- */
        sFvProcessPwmControl();

        compressor_apply_pwm();
    }
}

/* Called from the main loop: turns the sensor fault into the SYS_ERROR state. */
static void fault_supervisor(void)
{
    static bool prev_fault = false;
    bool fault = temp_fault;

    if (fault && !prev_fault && onOffState)
    {
        enter_error(ERR_TEMP_SENSOR);
    }
    else if (!fault && prev_fault &&
             sys_state == SYS_ERROR && sys_error == ERR_TEMP_SENSOR)
    {
        sys_error = ERR_NONE;
        change_state(home_state()); /* RUN if compressor ON, else IDLE */
    }

    prev_fault = fault;
}

static void periodic_json(void)
{
    if ((millis() - last_json_ms) >= JSON_MS)
    {
        last_json_ms = millis();
        send_json_uart2();
    }
}

/* ===================== State handlers ===================== */

/* Shared main-screen behaviour for SYS_IDLE (compressor OFF)
 * and SYS_RUN (compressor ON). */
static void state_main_screen(void)
{
    /* Menu long press: open settings */
    if (button_poll() == BTN_EVENT_LONG)
    {
        on_long_press();
        if (ui_state != UI_MAIN)
        {
            change_state(SYS_CONFIG);
            return;
        }
    }

    if ((millis() - last_refresh_ms) >= REFRESH_MS)
    {
        last_refresh_ms = millis();
        redraw(); /* temperature is read by compressor_task */
    }

    periodic_json();
}

static void state_idle(void)
{
    state_main_screen();
}

static void state_run(void)
{
    state_main_screen();
}

static void state_config(void)
{
    /* Menu long press cycles screens */
    if (button_poll() == BTN_EVENT_LONG)
    {
        on_long_press();
        if (ui_state == UI_MAIN) /* back at main screen -> leave config */
        {
            change_state(home_state()); /* RUN if compressor ON, else IDLE */
            return;
        }
    }
    if ((millis() - last_refresh_ms) >= 1000)
    {
        last_refresh_ms = millis();

        if (ui_state == UI_BMS_PARAMETERS)
        {
            redraw();
        }
    }
    /* Up / down keys only active in CONFIG */
    key_event_t ev = key_poll(&up_key);
    if (ev != KEY_EVENT_NONE)
        on_up(ev == KEY_EVENT_REPEAT);

    ev = key_poll(&down_key);
    if (ev != KEY_EVENT_NONE)
        on_down(ev == KEY_EVENT_REPEAT);

    periodic_json();
}

static void state_error(void)
{
    /* Long press on menu key = acknowledge / clear error */
    if (button_poll() == BTN_EVENT_LONG)
    {
        sys_error = ERR_NONE;
        temp_fail_cnt = 0;
        change_state(home_state()); /* RUN if compressor ON, else IDLE */
        return;
    }

    if ((millis() - last_refresh_ms) >= REFRESH_MS)
    {
        last_refresh_ms = millis();
        redraw(); /* auto-recovery is done in fault_supervisor() */
    }

    periodic_json(); /* keep reporting so the server sees the fault */
}

void door_event_init(void)
{
    door_mutex = xSemaphoreCreateMutex();

    if (door_mutex == NULL)
    {
        ESP_LOGE(DOOR_TAG, "Failed to create door mutex");
    }
}

/* Get timestamp in milliseconds.
   The RTC/system clock must already be initialized. */
static long long get_door_timestamp_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);

    /* Matches the -20 second offset in your telemetry JSON. */
    return (((long long)tv.tv_sec - 20LL) * 1000LL) + (tv.tv_usec / 1000LL);
}

static void store_door_event(int door_status)
{
    if (door_mutex == NULL)
    {
        return;
    }

    xSemaphoreTake(door_mutex, portMAX_DELAY);

    /* Do not overwrite a full batch waiting for transmission */
    if (!door_batch_ready &&
        door_event_count < DOOR_EVENT_COUNT)
    {
        door_events[door_event_count].ts = get_door_timestamp_ms();
        door_events[door_event_count].Dor = door_status;

        door_event_count++;

        ESP_LOGI(DOOR_TAG,
                 "Stored event %d/%d: ts=%lld, Dor=%d",
                 door_event_count,
                 DOOR_EVENT_COUNT,
                 (long long)door_events[door_event_count - 1].ts,
                 door_status);

        if (door_event_count == DOOR_EVENT_COUNT)
        {
            door_batch_ready = true;
            ESP_LOGI(DOOR_TAG, "Five door events ready for transmission");
        }
    }
    else
    {
        ESP_LOGW(DOOR_TAG, "Batch full, door event %d dropped", door_status);
    }

    xSemaphoreGive(door_mutex);
}

static bool build_door_event_array(const door_event_t *batch)
{
    size_t offset = 0;
    int written;

    if (batch == NULL)
    {
        return false;
    }

    door_json[0] = '\0';

    for (int i = 0; i < DOOR_EVENT_COUNT; i++)
    {
        written = snprintf(
            door_json + offset,
            sizeof(door_json) - offset,
            ",{\"ts\":%lld,\"values\":{\"Dor\":\"%02d\"}}",
            (long long)batch[i].ts, /* already in milliseconds */
            batch[i].Dor);

        if (written < 0 ||
            (size_t)written >= sizeof(door_json) - offset)
        {
            door_json[0] = '\0';
            return false;
        }

        offset += (size_t)written;
    }

    return true;
}

void door_task(void *arg)
{
    int currentstatus = gpio_get_level(main_pBit_doorSw);
    int laststatus = currentstatus;

    ESP_LOGI(DOOR_TAG, "Initial door status: %d", currentstatus);

    while (1)
    {
        currentstatus = gpio_get_level(main_pBit_doorSw);

        if (currentstatus != laststatus)
        {
            ESP_LOGI(DOOR_TAG,
                     "Door status changed: %d -> %d",
                     laststatus,
                     currentstatus);

            store_door_event(currentstatus);
            laststatus = currentstatus;
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void save_active_mode(op_mode_t mode)
{
    nvs_handle_t handle;
    esp_err_t err;

    err = nvs_open("storage", NVS_READWRITE, &handle);

    if (err == ESP_OK)
    {
        err = nvs_set_i32(handle, "active_mode", (int32_t)mode);

        if (err == ESP_OK)
        {
            err = nvs_commit(handle);
        }

        nvs_close(handle);
    }

    if (err != ESP_OK)
    {
        ESP_LOGE("NVS", "Failed to save mode: %s",
                 esp_err_to_name(err));
    }
    else
    {
        ESP_LOGI("NVS", "Mode saved: %s", mode_names[mode]);
    }
}

op_mode_t load_active_mode(void)
{
    nvs_handle_t handle;
    int32_t saved_mode = MODE_NORMAL;

    esp_err_t err = nvs_open("storage", NVS_READONLY, &handle);

    if (err == ESP_OK)
    {
        err = nvs_get_i32(handle, "active_mode", &saved_mode);
        nvs_close(handle);
    }

    if (err != ESP_OK ||
        saved_mode < MODE_ECO ||
        saved_mode >= MODE_COUNT)
    {
        ESP_LOGW("NVS", "Invalid or missing mode. Loading NORMAL");
        return MODE_NORMAL;
    }

    ESP_LOGI("NVS", "Loaded mode: %s",
             mode_names[saved_mode]);

    return (op_mode_t)saved_mode;
}

void save_compressor_status(bool status)
{
    nvs_handle_t nvs_handle;
    esp_err_t err;

    err = nvs_open("compressor", NVS_READWRITE, &nvs_handle);

    if (err == ESP_OK)
    {
        err = nvs_set_u8(nvs_handle, "onoff", status ? 1 : 0);

        if (err == ESP_OK)
        {
            err = nvs_commit(nvs_handle);
        }

        nvs_close(nvs_handle);
    }

    if (err != ESP_OK)
    {
        ESP_LOGE("COMPRESSOR", "Failed to save status: %s",
                 esp_err_to_name(err));
    }
    else
    {
        ESP_LOGI("COMPRESSOR", "Status saved: %s",
                 status ? "ON" : "OFF");
    }
}

bool load_compressor_status(void)
{
    nvs_handle_t nvs_handle;
    uint8_t status = 0; // Default OFF

    esp_err_t err = nvs_open("compressor", NVS_READONLY, &nvs_handle);

    if (err == ESP_OK)
    {
        err = nvs_get_u8(nvs_handle, "onoff", &status);
        nvs_close(nvs_handle);

        if (err == ESP_ERR_NVS_NOT_FOUND)
        {
            ESP_LOGW("COMPRESSOR", "No saved status. Default OFF");
            return false;
        }

        if (err != ESP_OK)
        {
            ESP_LOGE("COMPRESSOR", "Failed to load status: %s",
                     esp_err_to_name(err));
            return false;
        }
    }
    else
    {
        ESP_LOGW("COMPRESSOR", "NVS namespace not found. Default OFF");
        return false;
    }

    return (status == 1);
}
static void bt_start(void)
{
    esp_err_t ret;

    ESP_LOGI(TAG, "Free heap before BT = %lu",
             (unsigned long)esp_get_free_heap_size());

    /* NVS is already initialised by nvs_init(). Do NOT call
     * nvs_flash_init()/nvs_flash_erase() again: an erase would wipe
     * SET_TEMP, active_mode, swap_screen, compressor state. */
    if (load_bt_variables() != ESP_OK)
    {
        ESP_LOGW(TAG, "Using default BT values");
        bt_temp = 0;
        bt_duty = 0;
    }
    ESP_LOGI(TAG, "bt_temp=%d bt_duty=%d", (int)bt_temp, (int)bt_duty);

    memset(bt_connections, 0, sizeof(bt_connections));

    bt_rx_queue = xQueueCreate(BT_RX_QUEUE_LENGTH, sizeof(bt_rx_packet_t));
    if (bt_rx_queue == NULL)
    {
        ESP_LOGE(TAG, "BT RX queue create failed - Bluetooth disabled");
        return;
    }

    if (xTaskCreate(bluetooth_rx_task, "bt_rx_task", BT_TASK_STACK_SIZE,
                    NULL, BT_TASK_PRIORITY, NULL) != pdPASS)
    {
        ESP_LOGE(TAG, "BT RX task create failed - Bluetooth disabled");
        return;
    }

    ret = bluetooth_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Bluetooth init FAILED (%s) - continuing without BT",
                 esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "Bluetooth OK. Free heap=%lu min=%lu",
             (unsigned long)esp_get_free_heap_size(),
             (unsigned long)esp_get_minimum_free_heap_size());
}

void read_temperature_humidity(void)
{
    raw_temperature = 0;

    error = SHT2x_MeasureHM(TEMP, &raw_temperature);

    if (error == 0)
    {
        temperature = SHT2x_CalcTemperatureC(raw_temperature);

        ESP_LOGI(TAG, "Temperature = %.2f C", temperature);
    }
    else
    {
        ESP_LOGE(TAG, "Temperature Read Failed: %d", error);
    }

    vTaskDelay(pdMS_TO_TICKS(100));

    /*---------------- Humidity Reading ----------------*/

    raw_humidity = 0;

    error = SHT2x_MeasureHM(HUMIDITY, &raw_humidity);

    if (error == 0)
    {
        humidity = SHT2x_CalcRH(raw_humidity);

        ESP_LOGI(TAG, "Humidity = %.2f %%RH", humidity);
    }
    else
    {
        ESP_LOGE(TAG, "Humidity Read Failed: %d", error);
    }
}
// void sht20_task(void *pvParameters)
// {
//     while (1)
//     {
//         read_temperature_humidity();

//         vTaskDelay(pdMS_TO_TICKS(1000));
//     }
// }
void sht20_task(void *pvParameters)
{
    while (1)
    {
        sht20_error = SHT2x_ReadTemperatureHumidity(
            &temperature,
            &humidity);

        if (sht20_error == 0)
        {
            ESP_LOGI(TAG,
                     "SHT20 Temperature: %.2f C | Humidity: %.2f %%",
                     temperature,
                     humidity);
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
void app_main(void)
{
    ESP_LOGI(TAG, " V8.0 CODE UPDATED BY VIVEK PAWAR TIME : 12:00 PM 19/11/2026");
    boot_time_us = esp_timer_get_time();
    ESP_ERROR_CHECK(st7565_init());
    nvs_init();
    button_init(); /* menu key (GPIO 14) + ON/OFF key (GPIO 35)  */
    door_gpio_init();
    door_event_init();
    key_init(&up_key, false);  /* GPIO 34: no internal pull-up exists    */
    key_init(&down_key, true); /* GPIO 27: internal pull-up is available */

    display_page1(); /* logo */
    vTaskDelay(pdMS_TO_TICKS(1500));

    swap_screen = load_swap_screen();
    active_mode = load_active_mode();
    onOffState = load_compressor_status();
    load_bt_variables();
    SET_TEMP = load_set_temp();

    ds18b20_init(TEMP_BUS);

    /* Initialize software I2C */
    I2c_Init();

    vTaskDelay(pdMS_TO_TICKS(100));

    /* Reset SHT20 sensor */
    SHT2x_SoftReset();

    vTaskDelay(pdMS_TO_TICKS(20));

    /* Start SHT20 task */
    xTaskCreate(
        sht20_task,
        "sht20_task",
        3072,
        NULL,
        1,
        NULL);
    pwm_init();

    xTaskCreate(tskUART0, "tskUART0", UART0_TASK_STACK, NULL, UART0_TASK_PRIO, NULL);

    init(); /* UART2 -> BG95 */

    xTaskCreate(rx_task, "uart_rx_task", 4096, NULL, 2, NULL);
    xTaskCreate(gps_process_task, "gps_proc_task", 4096, NULL, 1, NULL);
    xTaskCreate(rtc_time_update_task, "RTC_Time_Update_Task", 4096, NULL, 1, NULL);
    xTaskCreate(door_task, "door_task", 2048, NULL, 2, NULL);
#if GPS_POLL_MODEM
    xTaskCreate(gps_poll_task, "gps_poll_task", 2048, NULL, 1, NULL);
#endif

    /* Compressor control: reads sensor, runs hysteresis, drives relay + PWM */
    xTaskCreate(compressor_task, "compressor", 4096, NULL, 3, &compressor_task_handle);
    // bt_start();
    if (!bt_started)
    {
        int64_t elapsed_time =
            (esp_timer_get_time() - boot_time_us) / 1000000;

        if (elapsed_time < 120)
        {
            bt_start();
            bt_started = true;

            ESP_LOGI(TAG, "Bluetooth started within 2-minute window");
        }
        else
        {
            ESP_LOGW(TAG, "Bluetooth startup time expired");
        }
    }

    last_refresh_ms = millis();
    last_json_ms = millis();

    sys_state = home_state(); /* RUN if the saved ON/OFF state is ON, else IDLE */
    ui_state = UI_MAIN;
    redraw();

    while (1)
    {
        main_onOffButton(); /* ON/OFF key is polled in every state */

        switch (sys_state)
        {
        case SYS_IDLE:
            state_idle();
            break;
        case SYS_CONFIG:
            state_config();
            break;
        case SYS_RUN:
            state_run();
            break;
        case SYS_ERROR:
            state_error();
            break;
        default:
            change_state(home_state());
            break;
        }

        fault_supervisor(); /* sensor fault -> SYS_ERROR state */
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}