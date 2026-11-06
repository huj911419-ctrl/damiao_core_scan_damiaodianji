/* DM-J4310-2EC V1.1 ID discovery on FDCAN1 + FDCAN2.
 * This firmware DOES NOT send motor enable, stop, position, velocity, torque,
 * parameter write, save or reset frames.
 * Only read-register requests (CAN 0x7FF, opcode 0x33, RID 7/8) are transmitted
 * after an explicit USB command and after the operator connects the motor
 * with power disconnected. Use ONE motor per bus while discovering IDs.
 */
#include "j4310_scan.h"
#include "fdcan.h"
#include "main.h"
#include "usbd_cdc_if.h"
#include "usbd_def.h"
#include "usb_device.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#define MAX_ESC_ID 0x7feU
#define SCAN_PERIOD_MS 20U

typedef struct {
    FDCAN_HandleTypeDef *can;
    uint16_t candidate;
    uint16_t esc_id;
    uint16_t master_id;
    uint32_t rx_frames;
    uint32_t tx_queries;
    uint32_t tx_errors;
    uint8_t have_esc;
    uint8_t have_master;
    uint8_t scanning;
    uint8_t master_pending;
    uint32_t last_query_ms;
} BusScan;

extern USBD_HandleTypeDef hUsbDeviceHS;
static BusScan buses[2];
static volatile char usb_line[64];
static volatile uint8_t usb_len;
static volatile uint8_t usb_ready;
static uint8_t *usb_rx;
static uint8_t tx_buf[196];
static volatile uint8_t tx_busy;
static uint8_t power_on;
static uint32_t last_status_ms;

static void UsbTxDone(uint16_t len) { (void)len; tx_busy = 0U; }

static void UsbRxReceived(uint16_t len)
{
    if (!usb_rx || usb_ready) return;
    for (uint16_t i = 0; i < len; ++i) {
        char c = (char)usb_rx[i];
        if (c == '\r' || c == '\n') {
            if (usb_len) {
                usb_line[usb_len] = '\0';
                usb_ready = 1U;
                return;
            }
        } else if (usb_len < sizeof(usb_line) - 1U) {
            usb_line[usb_len++] = c;
        } else {
            usb_len = 0U; /* reject oversized command */
        }
    }
}

static void Log(const char *format, ...)
{
    /* DAPLink alone does not enumerate the STM32 USB CDC interface. */
    if (hUsbDeviceHS.dev_state != USBD_STATE_CONFIGURED ||
        hUsbDeviceHS.pClassData == NULL || tx_busy) return;
    va_list args;
    va_start(args, format);
    int n = vsnprintf((char *)tx_buf, sizeof(tx_buf), format, args);
    va_end(args);
    if (n <= 0 || n >= (int)sizeof(tx_buf)) return;
    tx_busy = 1U;
    if (CDC_Transmit_HS(tx_buf, (uint16_t)n) != USBD_OK)
        tx_busy = 0U;
}

static void PowerOff(void)
{
    /* Existing board firmware asserts these outputs HIGH for 24V.
     * Electrical output must still be verified with a meter. */
    HAL_GPIO_WritePin(POWER_24V_1_GPIO_Port, POWER_24V_1_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(POWER_24V_2_GPIO_Port, POWER_24V_2_Pin, GPIO_PIN_RESET);
    power_on = 0U;
    buses[0].scanning = 0U;
    buses[1].scanning = 0U;
}

static void Status(void)
{
    Log("PWR=%s CAN1:rx=%lu ESC=%s0x%03X MST=%s0x%03X CAN2:rx=%lu ESC=%s0x%03X MST=%s0x%03X\r\n",
        power_on ? "ON" : "OFF",
        (unsigned long)buses[0].rx_frames, buses[0].have_esc ? "" : "?",
        buses[0].esc_id, buses[0].have_master ? "" : "?", buses[0].master_id,
        (unsigned long)buses[1].rx_frames, buses[1].have_esc ? "" : "?",
        buses[1].esc_id, buses[1].have_master ? "" : "?", buses[1].master_id);
}

static void ParseCommand(void)
{
    char command[sizeof(usb_line)];
    __disable_irq();
    if (!usb_ready) { __enable_irq(); return; }
    for (unsigned i=0; i<sizeof(command); ++i) {
        command[i] = (char)usb_line[i];
        if (!command[i]) break;
    }
    usb_len = 0U;
    usb_ready = 0U;
    __enable_irq();

    if (!strcmp(command, "POWER OFF")) {
        PowerOff();
        Log("POWER OFF requested; verify output physically before rewiring.\r\n");
    } else if (!strcmp(command, "POWER ON WIRED")) {
        /* Operator must have secured wiring and checked voltage/polarity. */
        HAL_GPIO_WritePin(POWER_24V_1_GPIO_Port, POWER_24V_1_Pin, GPIO_PIN_SET);
        HAL_GPIO_WritePin(POWER_24V_2_GPIO_Port, POWER_24V_2_Pin, GPIO_PIN_SET);
        power_on = 1U;
        Log("POWER ON requested for both 24V channels. NO motor-enable frames sent.\r\n");
    } else if (!strcmp(command, "SCAN") && power_on) {
        for (unsigned i=0; i<2; ++i) {
            buses[i].candidate = 0U;
            buses[i].have_esc = buses[i].have_master = 0U;
            buses[i].master_pending = 0U;
            buses[i].scanning = 1U;
            buses[i].last_query_ms = HAL_GetTick() - SCAN_PERIOD_MS;
        }
        Log("READ-ONLY SCAN started on CAN1 and CAN2 (0x000..0x7FE).\r\n");
    } else if (!strcmp(command, "SCAN")) {
        Log("Power is OFF. Connect and inspect wiring before POWER ON WIRED.\r\n");
    } else if (!strcmp(command, "STATUS")) {
        Status();
    } else {
        Log("Commands: STATUS | POWER ON WIRED | SCAN | POWER OFF\r\n");
    }
}

static HAL_StatusTypeDef ReadRegister(BusScan *bus, uint16_t id, uint8_t reg)
{
    /* Official register-read frame, not an actuator command. */
    FDCAN_TxHeaderTypeDef header = {0};
    header.Identifier = 0x7FFU;
    header.IdType = FDCAN_STANDARD_ID;
    header.TxFrameType = FDCAN_DATA_FRAME;
    header.DataLength = FDCAN_DLC_BYTES_8;
    header.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
    header.BitRateSwitch = FDCAN_BRS_OFF;
    header.FDFormat = FDCAN_CLASSIC_CAN;
    header.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
    uint8_t frame[8] = {(uint8_t)id, (uint8_t)(id >> 8), 0x33U, reg, 0U, 0U, 0U, 0U};
    return HAL_FDCAN_AddMessageToTxFifoQ(bus->can, &header, frame);
}

static void ReceiveFrames(BusScan *bus, unsigned bus_index)
{
    uint8_t data[8];
    FDCAN_RxHeaderTypeDef header;
    unsigned limit = 32U; /* bounded main-loop work */
    while (limit-- && HAL_FDCAN_GetRxFifoFillLevel(bus->can, FDCAN_RX_FIFO0)) {
        if (HAL_FDCAN_GetRxMessage(bus->can, FDCAN_RX_FIFO0, &header, data) != HAL_OK)
            break;
        ++bus->rx_frames;
        if (header.IdType != FDCAN_STANDARD_ID || header.RxFrameType != FDCAN_DATA_FRAME ||
            header.DataLength != FDCAN_DLC_BYTES_8) continue;
        /* Response: ESC_ID_L,ESC_ID_H,0x33,RID, uint32_le value. */
        if (data[2] == 0x33U && (data[3] == 7U || data[3] == 8U)) {
            uint16_t addressed_id = (uint16_t)data[0] | ((uint16_t)data[1] << 8U);
            uint32_t value = (uint32_t)data[4] | ((uint32_t)data[5] << 8U) |
                             ((uint32_t)data[6] << 16U) | ((uint32_t)data[7] << 24U);
            if (addressed_id > MAX_ESC_ID) continue;
            if (data[3] == 8U && value == addressed_id) {
                bus->esc_id = addressed_id;
                bus->have_esc = 1U;
                bus->master_pending = 1U;
                Log("CAN%u FOUND ESC_ID=0x%03X response=0x%03lX\r\n",
                    bus_index + 1U, addressed_id, (unsigned long)header.Identifier);
            } else if (data[3] == 7U && bus->have_esc && addressed_id == bus->esc_id) {
                bus->master_id = (uint16_t)value;
                bus->have_master = 1U;
                Log("CAN%u FOUND ESC_ID=0x%03X MST_ID=0x%03X (read-register reply)\r\n",
                    bus_index + 1U, bus->esc_id, bus->master_id);
            }
        } else if (bus->rx_frames <= 3U) {
            Log("CAN%u passive frame id=0x%03lX d0=0x%02X d1=0x%02X\r\n",
                bus_index + 1U, (unsigned long)header.Identifier, data[0], data[1]);
        }
    }
}

static void Probe(BusScan *bus)
{
    if (!power_on || !bus->scanning) return;
    uint32_t now = HAL_GetTick();
    if ((uint32_t)(now - bus->last_query_ms) < SCAN_PERIOD_MS) return;
    bus->last_query_ms = now;
    if (bus->master_pending) {
        bus->master_pending = 0U;
        if (ReadRegister(bus, bus->esc_id, 7U) == HAL_OK)
            ++bus->tx_queries;
        else ++bus->tx_errors;
        return;
    }
    if (bus->candidate > MAX_ESC_ID) {
        bus->scanning = 0U;
        return;
    }
    if (ReadRegister(bus, bus->candidate++, 8U) == HAL_OK)
        ++bus->tx_queries;
    else ++bus->tx_errors;
}

void J4310ScanInit(void)
{
    PowerOff();
    buses[0].can = &hfdcan1;
    buses[1].can = &hfdcan2;
    for (unsigned i=0; i<2; ++i) {
        if (HAL_FDCAN_ConfigGlobalFilter(buses[i].can, FDCAN_ACCEPT_IN_RX_FIFO0,
                FDCAN_REJECT, FDCAN_REJECT_REMOTE, FDCAN_REJECT_REMOTE) != HAL_OK)
            Error_Handler();
        if (HAL_FDCAN_Start(buses[i].can) != HAL_OK) Error_Handler();
    }
    usb_rx = CDCInitRxbufferNcallback(UsbTxDone, UsbRxReceived);
    last_status_ms = HAL_GetTick();
}

void J4310ScanPoll(void)
{
    ParseCommand();
    ReceiveFrames(&buses[0], 0U);
    ReceiveFrames(&buses[1], 1U);
    Probe(&buses[0]);
    Probe(&buses[1]);
    if ((uint32_t)(HAL_GetTick() - last_status_ms) > 5000U) {
        last_status_ms = HAL_GetTick();
        Status();
    }
}
