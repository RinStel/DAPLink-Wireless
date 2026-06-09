#include "usb_config_disk.h"

#include <ctype.h>
#include <stddef.h>
#include <string.h>

#include "board.h"
#include "cmsis_dap_usb.h"
#include "device_config.h"
#include "device_config_storage.h"
#include "firmware_version.h"
#include "gd32f30x_misc.h"
#include "gd32f30x_rcu.h"
#include "serial_bridge.h"
#include "usb_composite.h"
#include "usb_disk_geometry.h"
#include "usbd_lld_int.h"
#include "usbd_msc_bbb.h"
#include "usbd_msc_core.h"
#include "usbd_msc_mem.h"

#define DISK_BLOCK_SIZE          512U
#define DISK_BLOCK_COUNT         32U
#define DISK_SIZE                (DISK_BLOCK_SIZE * DISK_BLOCK_COUNT)
#define DISK_FAT_SECTOR          1U
#define DISK_ROOT_SECTOR         2U
#define DISK_DATA_SECTOR         3U
#define DISK_CONFIG_DELAY_MS     800U
#define CONFIG_BUFFER_SIZE       512U
#define STATUS_BUFFER_SIZE       256U

typedef enum {
    CONFIG_APPLY_OK = 0,
    CONFIG_APPLY_INVALID,
    CONFIG_APPLY_RADIO_FAILED,
    CONFIG_APPLY_FLASH_FAILED
} config_apply_result_t;

static usb_dev s_usb_device;
static uint8_t s_disk[DISK_SIZE];
static char s_config_buffer[CONFIG_BUFFER_SIZE];
static char s_status_buffer[STATUS_BUFFER_SIZE];
static volatile bool s_disk_dirty;
static volatile uint32_t s_last_write_ms;
static volatile uint32_t s_disk_write_version;
static volatile bool s_refresh_requested;
static device_config_t s_refresh_previous;
static bool s_last_apply_ok;
static config_apply_result_t s_apply_result;

static uint8_t s_inquiry_data[USBD_STD_INQUIRY_LENGTH] = {
    0x00U, 0x80U, 0x02U, 0x02U, 31U, 0x00U, 0x00U, 0x00U,
    'D', 'A', 'P', 'L', 'I', 'N', 'K', ' ',
    'C', 'O', 'N', 'F', 'I', 'G', ' ', 'D',
    'I', 'S', 'K', ' ', ' ', ' ', ' ', ' ',
    '0', '.', '1', '0'
};
static uint8_t s_toc_data[12];

static int8_t disk_init(uint8_t lun);
static int8_t disk_ready(uint8_t lun);
static int8_t disk_protected(uint8_t lun);
static int8_t disk_read(uint8_t lun, uint8_t *buffer,
                        uint32_t byte_address, uint16_t block_count);
static int8_t disk_write(uint8_t lun, uint8_t *buffer,
                         uint32_t byte_address, uint16_t block_count);
static int8_t disk_max_lun(void);

static usbd_mem_cb s_memory_ops = {
    .mem_init = disk_init,
    .mem_ready = disk_ready,
    .mem_protected = disk_protected,
    .mem_read = disk_read,
    .mem_write = disk_write,
    .mem_maxlun = disk_max_lun,
    .mem_toc_data = s_toc_data,
    .mem_inquiry_data = {s_inquiry_data},
    .mem_block_size = {DISK_BLOCK_SIZE},
    .mem_block_len = {DISK_BLOCK_COUNT}
};

usbd_mem_cb *usbd_mem_fops = &s_memory_ops;

static void encode_u16_le(uint8_t *output, uint16_t value)
{
    output[0] = (uint8_t)value;
    output[1] = (uint8_t)(value >> 8);
}

static void encode_u32_le(uint8_t *output, uint32_t value)
{
    output[0] = (uint8_t)value;
    output[1] = (uint8_t)(value >> 8);
    output[2] = (uint8_t)(value >> 16);
    output[3] = (uint8_t)(value >> 24);
}

static void fat12_set(uint16_t cluster, uint16_t value)
{
    uint8_t *fat = &s_disk[DISK_FAT_SECTOR * DISK_BLOCK_SIZE];
    uint16_t offset = (uint16_t)(cluster + cluster / 2U);

    if ((cluster & 1U) == 0U) {
        fat[offset] = (uint8_t)value;
        fat[offset + 1U] =
            (uint8_t)((fat[offset + 1U] & 0xF0U) | (value >> 8));
    } else {
        fat[offset] =
            (uint8_t)((fat[offset] & 0x0FU) | (value << 4));
        fat[offset + 1U] = (uint8_t)(value >> 4);
    }
}

static uint16_t fat12_get(uint16_t cluster)
{
    const uint8_t *fat = &s_disk[DISK_FAT_SECTOR * DISK_BLOCK_SIZE];
    uint16_t offset = (uint16_t)(cluster + cluster / 2U);
    uint16_t value = (uint16_t)fat[offset] |
                     ((uint16_t)fat[offset + 1U] << 8);

    return ((cluster & 1U) == 0U) ? (value & 0x0FFFU) : (value >> 4);
}

static void root_entry_write(uint8_t entry, const char name[11],
                             uint16_t cluster, uint32_t size)
{
    uint8_t *item = &s_disk[DISK_ROOT_SECTOR * DISK_BLOCK_SIZE +
                            (uint32_t)entry * 32U];

    memcpy(item, name, 11U);
    item[11] = 0x20U;
    encode_u16_le(&item[26], cluster);
    encode_u32_le(&item[28], size);
}

static bool text_append(char *output, uint32_t capacity,
                        uint32_t *length, const char *text)
{
    size_t text_length = strlen(text);

    if (*length + text_length >= capacity) {
        return false;
    }
    memcpy(&output[*length], text, text_length);
    *length += (uint32_t)text_length;
    output[*length] = '\0';
    return true;
}

static uint32_t config_text_build(char output[CONFIG_BUFFER_SIZE])
{
    const device_config_t *config = device_config_get();
    const char *device_mode;
    const char *mode = config->rate_mode == DEVICE_RATE_AUTO
                           ? "AUTO"
                           : "FIXED";
    uint32_t length = 0U;

    switch (config->device_mode) {
    case DEVICE_MODE_WIRED:
        device_mode = "WIRED";
        break;
    case DEVICE_MODE_WIRELESS_SLAVE:
        device_mode = "WIRELESS_SLAVE";
        break;
    case DEVICE_MODE_WIRELESS_HOST:
    default:
        device_mode = "WIRELESS_HOST";
        break;
    }

    output[0] = '\0';
    if (!text_append(output, CONFIG_BUFFER_SIZE, &length,
                     "# DAPLink-Wireless configuration\r\n"
                     "# SYNC must contain exactly 16 letters or digits.\r\n"
                     "# DEVICE_MODE: WIRED, WIRELESS_HOST, WIRELESS_SLAVE.\r\n"
                     "# MODE is AUTO or FIXED. PROFILE: GFSK2M, GFSK1M,\r\n"
                     "# GFSK500K, FLRC1M3, FLRC650K.\r\n"
                     "SYNC=") ||
        !text_append(output, CONFIG_BUFFER_SIZE, &length,
                     config->sync_code) ||
        !text_append(output, CONFIG_BUFFER_SIZE, &length,
                     "\r\nDEVICE_MODE=") ||
        !text_append(output, CONFIG_BUFFER_SIZE, &length, device_mode) ||
        !text_append(output, CONFIG_BUFFER_SIZE, &length, "\r\nMODE=") ||
        !text_append(output, CONFIG_BUFFER_SIZE, &length, mode) ||
        !text_append(output, CONFIG_BUFFER_SIZE, &length,
                     "\r\nPROFILE=") ||
        !text_append(output, CONFIG_BUFFER_SIZE, &length,
                     sx128x_profile_name(config->fixed_profile)) ||
        !text_append(output, CONFIG_BUFFER_SIZE, &length, "\r\n")) {
        return 0U;
    }
    return length;
}

static uint32_t status_text_build(char output[STATUS_BUFFER_SIZE])
{
    static const char hex[] = "0123456789ABCDEF";
    const char *result;
    uint32_t length = 0U;
    uint8_t reset_cause = board_reset_cause();

    switch (s_apply_result) {
    case CONFIG_APPLY_INVALID:
        result = "INVALID_CONFIG";
        break;
    case CONFIG_APPLY_RADIO_FAILED:
        result = "HARDWARE_APPLY_FAILED";
        break;
    case CONFIG_APPLY_FLASH_FAILED:
        result = "FLASH_SAVE_FAILED";
        break;
    case CONFIG_APPLY_OK:
    default:
        result = "OK";
        break;
    }
    output[0] = '\0';
    if (!text_append(output, STATUS_BUFFER_SIZE, &length,
                     "PRODUCT=DAPLink-Wireless\r\nFIRMWARE=") ||
        !text_append(output, STATUS_BUFFER_SIZE, &length,
                     FIRMWARE_VERSION_STRING) ||
        !text_append(output, STATUS_BUFFER_SIZE, &length,
                     "\r\nCONFIG_STATUS=") ||
        !text_append(output, STATUS_BUFFER_SIZE, &length, result) ||
        !text_append(output, STATUS_BUFFER_SIZE, &length,
                     "\r\nRESET_CAUSE=0x")) {
        output[0] = '\0';
        return 0U;
    }
    if (length + 4U >= STATUS_BUFFER_SIZE) {
        output[0] = '\0';
        return 0U;
    }
    output[length++] = hex[reset_cause >> 4];
    output[length++] = hex[reset_cause & 0x0FU];
    output[length++] = '\r';
    output[length++] = '\n';
    output[length] = '\0';
    return length;
}

static void disk_format(void)
{
    uint8_t *boot = &s_disk[0];
    static const char readme[] =
        "Edit CONFIG.TXT, save it, then safely eject the drive.\r\n"
        "Invalid settings are ignored and the previous configuration remains.\r\n";
    uint32_t config_length;
    uint32_t status_length;

    memset(s_disk, 0, sizeof(s_disk));
    boot[0] = 0xEBU;
    boot[1] = 0x3CU;
    boot[2] = 0x90U;
    memcpy(&boot[3], "DAPLINK ", 8U);
    encode_u16_le(&boot[11], DISK_BLOCK_SIZE);
    boot[13] = 1U;
    encode_u16_le(&boot[14], 1U);
    boot[16] = 1U;
    encode_u16_le(&boot[17], 16U);
    encode_u16_le(&boot[19], DISK_BLOCK_COUNT);
    boot[21] = 0xF8U;
    encode_u16_le(&boot[22], 1U);
    encode_u16_le(&boot[24], 1U);
    encode_u16_le(&boot[26], 1U);
    boot[36] = 0x80U;
    boot[38] = 0x29U;
    encode_u32_le(&boot[39], board_device_id_hash());
    memcpy(&boot[43], "DAPLINK CFG", 11U);
    memcpy(&boot[54], "FAT12   ", 8U);
    boot[510] = 0x55U;
    boot[511] = 0xAAU;

    s_disk[DISK_FAT_SECTOR * DISK_BLOCK_SIZE] = 0xF8U;
    s_disk[DISK_FAT_SECTOR * DISK_BLOCK_SIZE + 1U] = 0xFFU;
    s_disk[DISK_FAT_SECTOR * DISK_BLOCK_SIZE + 2U] = 0xFFU;
    fat12_set(2U, 0x0FFFU);
    fat12_set(3U, 0x0FFFU);
    fat12_set(4U, 0x0FFFU);

    root_entry_write(0U, "README  TXT", 2U, sizeof(readme) - 1U);
    config_length = config_text_build(s_config_buffer);
    root_entry_write(1U, "CONFIG  TXT", 3U, config_length);
    status_length = status_text_build(s_status_buffer);
    root_entry_write(2U, "STATUS  TXT", 4U, status_length);
    memcpy(&s_disk[DISK_DATA_SECTOR * DISK_BLOCK_SIZE],
           readme, sizeof(readme) - 1U);
    memcpy(&s_disk[(DISK_DATA_SECTOR + 1U) * DISK_BLOCK_SIZE],
           s_config_buffer, config_length);
    memcpy(&s_disk[(DISK_DATA_SECTOR + 2U) * DISK_BLOCK_SIZE],
           s_status_buffer, status_length);
}

static void usb_irqs_enable(bool enable)
{
    if (enable) {
        nvic_irq_enable(USBD_LP_CAN0_RX0_IRQn, 1U, 0U);
        nvic_irq_enable(USBD_HP_CAN0_TX_IRQn, 1U, 0U);
        nvic_irq_enable(USBD_WKUP_IRQn, 1U, 0U);
    } else {
        nvic_irq_disable(USBD_LP_CAN0_RX0_IRQn);
        nvic_irq_disable(USBD_HP_CAN0_TX_IRQn);
        nvic_irq_disable(USBD_WKUP_IRQn);
    }
}

static void disk_rebuild_and_reconnect(void)
{
    usbd_disconnect(&s_usb_device);
    usb_irqs_enable(false);
    disk_format();
    s_disk_dirty = false;
    s_refresh_requested = false;
    usb_irqs_enable(true);
    usbd_connect(&s_usb_device);
}

static bool profile_parse(const char *value, sx128x_profile_t *profile)
{
    sx128x_profile_t candidate;

    for (candidate = SX128X_PROFILE_GFSK_2M;
         candidate < SX128X_PROFILE_COUNT;
         candidate = (sx128x_profile_t)(candidate + 1U)) {
        if (strcmp(value, sx128x_profile_name(candidate)) == 0) {
            *profile = candidate;
            return true;
        }
    }
    return false;
}

static bool device_mode_parse(const char *value, device_mode_t *mode)
{
    if (strcmp(value, "WIRED") == 0) {
        *mode = DEVICE_MODE_WIRED;
    } else if (strcmp(value, "WIRELESS_HOST") == 0) {
        *mode = DEVICE_MODE_WIRELESS_HOST;
    } else if (strcmp(value, "WIRELESS_SLAVE") == 0) {
        *mode = DEVICE_MODE_WIRELESS_SLAVE;
    } else {
        return false;
    }
    return true;
}

static void trim_line(char *line)
{
    size_t length = strlen(line);

    while ((length > 0U) &&
           ((line[length - 1U] == '\r') ||
            (line[length - 1U] == '\n') ||
            isspace((unsigned char)line[length - 1U]))) {
        line[--length] = '\0';
    }
}

static bool config_parse(char *text)
{
    char sync_code[DEVICE_SYNC_CODE_LENGTH + 1U] = "";
    char device_mode_text[16] = "";
    char mode_text[8] = "";
    char profile_text[12] = "";
    char *line = strtok(text, "\n");
    device_rate_mode_t mode;
    device_mode_t device_mode;
    sx128x_profile_t profile = SX128X_PROFILE_GFSK_1M;
    bool sync_found = false;
    bool device_mode_found = false;
    bool mode_found = false;
    bool profile_found = false;

    while (line != NULL) {
        trim_line(line);
        if (strncmp(line, "SYNC=", 5U) == 0) {
            if (strlen(line + 5U) != DEVICE_SYNC_CODE_LENGTH) {
                return false;
            }
            strncpy(sync_code, line + 5U, sizeof(sync_code) - 1U);
            sync_found = true;
        } else if (strncmp(line, "DEVICE_MODE=", 12U) == 0) {
            if (strlen(line + 12U) >= sizeof(device_mode_text)) {
                return false;
            }
            strncpy(device_mode_text, line + 12U,
                    sizeof(device_mode_text) - 1U);
            device_mode_found = true;
        } else if (strncmp(line, "MODE=", 5U) == 0) {
            if (strlen(line + 5U) >= sizeof(mode_text)) {
                return false;
            }
            strncpy(mode_text, line + 5U, sizeof(mode_text) - 1U);
            mode_found = true;
        } else if (strncmp(line, "PROFILE=", 8U) == 0) {
            if (strlen(line + 8U) >= sizeof(profile_text)) {
                return false;
            }
            strncpy(profile_text, line + 8U, sizeof(profile_text) - 1U);
            profile_found = true;
        }
        line = strtok(NULL, "\n");
    }

    if (!sync_found || !device_mode_found || !mode_found || !profile_found) {
        return false;
    }
    if (!device_mode_parse(device_mode_text, &device_mode)) {
        return false;
    }
    if (strcmp(mode_text, "AUTO") == 0) {
        mode = DEVICE_RATE_AUTO;
    } else if (strcmp(mode_text, "FIXED") == 0) {
        mode = DEVICE_RATE_FIXED;
    } else {
        return false;
    }
    if (!profile_parse(profile_text, &profile)) {
        return false;
    }
    return device_config_apply(sync_code, device_mode, mode, profile);
}

static bool config_file_extract(char output[CONFIG_BUFFER_SIZE])
{
    const uint8_t *root = &s_disk[DISK_ROOT_SECTOR * DISK_BLOCK_SIZE];
    uint8_t entry;

    for (entry = 0U; entry < 16U; ++entry) {
        const uint8_t *item = &root[(uint32_t)entry * 32U];
        uint16_t cluster;
        uint32_t size;
        uint32_t copied = 0U;

        if ((item[0] == 0x00U) || (item[0] == 0xE5U) ||
            ((item[11] & 0x18U) != 0U) ||
            (memcmp(item, "CONFIG  TXT", 11U) != 0)) {
            continue;
        }
        cluster = (uint16_t)item[26] | ((uint16_t)item[27] << 8);
        size = (uint32_t)item[28] | ((uint32_t)item[29] << 8) |
               ((uint32_t)item[30] << 16) | ((uint32_t)item[31] << 24);
        if ((size == 0U) || (size >= CONFIG_BUFFER_SIZE)) {
            return false;
        }
        while ((cluster >= 2U) && (cluster < 0x0FF8U) &&
               (copied < size)) {
            uint32_t sector = DISK_DATA_SECTOR + cluster - 2U;
            uint32_t chunk = size - copied;

            if (sector >= DISK_BLOCK_COUNT) {
                return false;
            }
            if (chunk > DISK_BLOCK_SIZE) {
                chunk = DISK_BLOCK_SIZE;
            }
            memcpy(&output[copied],
                   &s_disk[sector * DISK_BLOCK_SIZE], chunk);
            copied += chunk;
            cluster = fat12_get(cluster);
        }
        output[copied] = '\0';
        return copied == size;
    }
    return false;
}

static int8_t disk_init(uint8_t lun)
{
    (void)lun;
    return 0;
}

static int8_t disk_ready(uint8_t lun)
{
    (void)lun;
    return 0;
}

static int8_t disk_protected(uint8_t lun)
{
    (void)lun;
    return 0;
}

static int8_t disk_read(uint8_t lun, uint8_t *buffer,
                        uint32_t byte_address, uint16_t block_count)
{
    uint32_t byte_count;

    (void)lun;
    if ((buffer == NULL) ||
        !usb_disk_byte_range_valid(DISK_SIZE, DISK_BLOCK_SIZE,
                                   byte_address, block_count,
                                   &byte_count)) {
        return -1;
    }
    memcpy(buffer, &s_disk[byte_address], byte_count);
    return 0;
}

static int8_t disk_write(uint8_t lun, uint8_t *buffer,
                         uint32_t byte_address, uint16_t block_count)
{
    uint32_t byte_count;

    (void)lun;
    if ((buffer == NULL) ||
        !usb_disk_byte_range_valid(DISK_SIZE, DISK_BLOCK_SIZE,
                                   byte_address, block_count,
                                   &byte_count)) {
        return -1;
    }
    ++s_disk_write_version;
    __DMB();
    memcpy(&s_disk[byte_address], buffer, byte_count);
    __DMB();
    ++s_disk_write_version;
    s_last_write_ms = board_millis();
    s_disk_dirty = true;
    return 0;
}

static int8_t disk_max_lun(void)
{
    return 0;
}

bool usb_config_disk_init(void)
{
    disk_format();
    s_disk_dirty = false;
    s_disk_write_version = 0U;
    s_refresh_requested = false;
    s_last_apply_ok = true;
    s_apply_result = CONFIG_APPLY_OK;

    board_usb_connect(false);
    rcu_usb_clock_config(RCU_CKUSB_CKPLL_DIV2_5);
    rcu_periph_clock_enable(RCU_USBD);
    usb_irqs_enable(true);
    usb_composite_prepare();
    usbd_init(&s_usb_device, &composite_desc, &composite_class);
    usbd_connect(&s_usb_device);
    return true;
}

void usb_config_disk_process(void)
{
    device_config_t previous;
    uint32_t version_before;
    uint32_t version_after;
    uint32_t idle_ms;

    cmsis_dap_usb_process();

    if (s_refresh_requested) {
        if (!cmsis_dap_usb_idle()) {
            return;
        }
        if (!device_config_storage_matches(device_config_get())) {
            usbd_disconnect(&s_usb_device);
            usb_irqs_enable(false);
            s_last_apply_ok =
                device_config_storage_save(device_config_get());
            if (!s_last_apply_ok) {
                s_apply_result = CONFIG_APPLY_FLASH_FAILED;
                (void)device_config_apply(
                    s_refresh_previous.sync_code,
                    s_refresh_previous.device_mode,
                    s_refresh_previous.rate_mode,
                    s_refresh_previous.fixed_profile);
                (void)serial_bridge_apply_config();
            } else {
                s_apply_result = CONFIG_APPLY_OK;
            }
            usb_irqs_enable(true);
        } else {
            s_last_apply_ok = true;
            s_apply_result = CONFIG_APPLY_OK;
        }
        disk_rebuild_and_reconnect();
        return;
    }

    idle_ms = (uint32_t)(board_millis() - s_last_write_ms);
    if (!s_disk_dirty || (idle_ms < DISK_CONFIG_DELAY_MS) ||
        (s_usb_device.class_data[USBD_MSC_INTERFACE] == NULL) ||
        (((usbd_msc_handler *)
              s_usb_device.class_data[USBD_MSC_INTERFACE])->bbb_state !=
         BBB_IDLE) ||
        !cmsis_dap_usb_idle()) {
        return;
    }

    version_before = s_disk_write_version;
    __DMB();
    if ((version_before & 1U) != 0U ||
        !config_file_extract(s_config_buffer)) {
        return;
    }
    __DMB();
    version_after = s_disk_write_version;
    if (version_before != version_after) {
        return;
    }

    previous = *device_config_get();
    if (!config_parse(s_config_buffer)) {
        s_disk_dirty = false;
        s_last_apply_ok = false;
        s_apply_result = CONFIG_APPLY_INVALID;
        disk_rebuild_and_reconnect();
        return;
    }

    usbd_disconnect(&s_usb_device);
    usb_irqs_enable(false);
    __DMB();
    if (version_after != s_disk_write_version) {
        (void)device_config_apply(previous.sync_code,
                                  previous.device_mode,
                                  previous.rate_mode,
                                  previous.fixed_profile);
        usb_irqs_enable(true);
        usbd_connect(&s_usb_device);
        return;
    }

    if (!serial_bridge_apply_config()) {
        (void)device_config_apply(previous.sync_code,
                                  previous.device_mode,
                                  previous.rate_mode,
                                  previous.fixed_profile);
        (void)serial_bridge_apply_config();
        usb_irqs_enable(true);
        usbd_connect(&s_usb_device);
        s_disk_dirty = false;
        s_last_apply_ok = false;
        s_apply_result = CONFIG_APPLY_RADIO_FAILED;
        disk_rebuild_and_reconnect();
        return;
    }

    if (!device_config_storage_save(device_config_get())) {
        (void)device_config_apply(previous.sync_code,
                                  previous.device_mode,
                                  previous.rate_mode,
                                  previous.fixed_profile);
        (void)serial_bridge_apply_config();
        usb_irqs_enable(true);
        usbd_connect(&s_usb_device);
        s_disk_dirty = false;
        s_last_apply_ok = false;
        s_apply_result = CONFIG_APPLY_FLASH_FAILED;
        disk_rebuild_and_reconnect();
        return;
    }
    s_last_apply_ok = true;
    s_apply_result = CONFIG_APPLY_OK;
    disk_rebuild_and_reconnect();
}

bool usb_config_disk_configured(void)
{
    return (s_usb_device.cur_status == USBD_CONFIGURED) && s_last_apply_ok;
}

void usb_config_disk_irq(void)
{
    usbd_isr();
}

void usb_config_disk_hp_irq(void)
{
    usbd_int_hpst();
}

void usb_config_disk_wakeup_irq(void)
{
    resume_mcu(&s_usb_device);
}

void usb_config_disk_refresh(const device_config_t *previous)
{
    if (previous == NULL) {
        return;
    }
    s_refresh_previous = *previous;
    __DMB();
    s_refresh_requested = true;
}
