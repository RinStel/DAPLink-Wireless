/*
 * DAPLink-Wireless — Wireless CMSIS-DAP v2 debug probe firmware
 * Copyright (C) 2025 RinStel <me@rinx.nz>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/*
 * 本项目自带的 SCSI 命令核心，替代厂商 usbd_msc_scsi.c。
 *
 * 为什么必须替代：厂商实现把 MSC_MEDIA_PACKET_SIZE（512 B）整块交给
 * usbd_ep_send()/usbd_ep_recev()，而驱动层 usbd_ep_data_write() 既不按
 * maxpacket 也不按 PMA 槽长度夹取，tx_count 还直接等于该长度。本板 PMA 总共只有
 * 512 B，还要容纳 EP0/CDC/DAP，任何端点都拿不到 512 B 槽：实测一次扇区读会从
 * MSC 的 TX 槽连写 512 B，踩过 CDC 通知与数据缓冲、DAP 收发缓冲，并越过 PMA
 * 末尾 0x200 进入 USB 外设寄存器窗口。表现为"复合设备已枚举、CDC 正常，但 MSC
 * 与 WinUSB 两个子功能近一分钟起不来"。
 *
 * 厂商的 MSC_MEDIA_PACKET_SIZE 又不能小于一逻辑块：它用 len / scsi_blk_size 作
 * 为块数传给 mem_read()，分片小于 512 B 会算成 0 块。
 *
 * 所以这里的做法是：扇区留在 RAM（msc->bbb_data），每次只把一个扇区里不超过
 * MSC_DATA_PACKET_SIZE（64 B）的部分交给 PMA，靠 BBB 的端点完成回调续传。
 * BOT 状态机（CBW 31 B / CSW 13 B，本来就 ≤64 B）继续复用未改动的厂商
 * usbd_msc_bbb.c，因此本文件只导出它需要的 scsi_process_cmd()/scsi_sense_code()。
 */
#include "usbd_msc_scsi.h"

#include <string.h>

#include "usbd_msc_bbb.h"
#include "usbd_msc_mem.h"

#define SCSI_STD_INQUIRY_LENGTH     36U
#define SCSI_SENSE_LENGTH           18U
#define SCSI_CAPACITY_LENGTH        8U
#define SCSI_FORMAT_CAP_LENGTH      12U
#define SCSI_MODE6_HEADER_LENGTH    4U
#define SCSI_MODE10_HEADER_LENGTH   8U
#define SCSI_BLOCK_DESCRIPTOR       8U
#define SCSI_FORMATTED_FLAG         (1U << 24)

/* 两个定长页以平台头为准，避免与同一栈里的另一份定义漂移。 */
_Static_assert(SCSI_STD_INQUIRY_LENGTH == STANDARD_INQUIRY_DATA_LEN,
               "INQUIRY 页长度必须等于 STANDARD_INQUIRY_DATA_LEN");
_Static_assert(SCSI_SENSE_LENGTH == REQUEST_SENSE_DATA_LEN,
               "SENSE 页长度必须等于 REQUEST_SENSE_DATA_LEN");

/* 单 LUN RAM 盘；MEM_LUN_NUM 变大时这里的暂存状态要一并重新设计。 */
_Static_assert(MSC_MEDIA_PACKET_SIZE >= 512U,
               "扇区级存必须容纳一个逻辑块（DISK_BLOCK_SIZE 为 512）");
_Static_assert(MSC_MEDIA_PACKET_SIZE % MSC_DATA_PACKET_SIZE == 0U,
               "扇区必须是端点事务长度的整数倍");

/* bbb_data 中已填充的字节数：IN 时是已读入待发的扇区长度，OUT 时是已收进的
 * 扇区前缀长度。只在 USB 中断上下文（含本文件的回调）修改。 */
static uint32_t s_stage_len;

static uint32_t min_u32(uint32_t left, uint32_t right)
{
    return (left < right) ? left : right;
}

static usbd_msc_handler *scsi_handler(usb_dev *udev)
{
    return (usbd_msc_handler *)udev->class_data[USBD_MSC_INTERFACE];
}

static void encode_be32(uint8_t *output, uint32_t value)
{
    output[0] = (uint8_t)(value >> 24);
    output[1] = (uint8_t)(value >> 16);
    output[2] = (uint8_t)(value >> 8);
    output[3] = (uint8_t)value;
}

static void encode_be16(uint8_t *output, uint16_t value)
{
    output[0] = (uint8_t)(value >> 8);
    output[1] = (uint8_t)value;
}

static void scsi_geometry_sync(usb_dev *udev, uint8_t lun)
{
    usbd_msc_handler *msc = scsi_handler(udev);

    msc->scsi_blk_size[lun] = usbd_mem_fops->mem_block_size[lun];
    msc->scsi_blk_nbr[lun] = usbd_mem_fops->mem_block_len[lun];
}

void scsi_sense_code(usb_dev *udev, uint8_t lun, uint8_t skey, uint8_t asc)
{
    usbd_msc_handler *msc = scsi_handler(udev);
    uint8_t head = (uint8_t)((msc->scsi_sense_head + 1U) % SENSE_LIST_DEEPTH);

    (void)lun;
    if (head == msc->scsi_sense_tail) {
        /* 环满：丢最旧的一条，保证最新错误一定读得到。 */
        msc->scsi_sense_tail = (uint8_t)((msc->scsi_sense_tail + 1U) %
                                        SENSE_LIST_DEEPTH);
    }
    msc->scsi_sense_head = head;
    msc->scsi_sense[head].SenseKey = skey;
    msc->scsi_sense[head].ASC = asc;
    msc->scsi_sense[head].ASCQ = 0U;
    msc->scsi_sense[head].Information = 0U;
}

static void scsi_sense_take(usb_dev *udev, uint8_t *skey, uint8_t *asc)
{
    usbd_msc_handler *msc = scsi_handler(udev);
    uint8_t next = (uint8_t)((msc->scsi_sense_tail + 1U) % SENSE_LIST_DEEPTH);

    /* 约定：head 指向最新一条，tail 指向最旧一条的前一格；因此空是
     * head == tail，而不是 next == head（后者会吞掉最后一条）。 */
    if (msc->scsi_sense_tail == msc->scsi_sense_head) {
        *skey = NO_SENSE;
        *asc = 0U;
        return;
    }
    *skey = msc->scsi_sense[next].SenseKey;
    *asc = msc->scsi_sense[next].ASC;
    msc->scsi_sense_tail = next;
}

static int8_t scsi_reject(usb_dev *udev, uint8_t lun, uint8_t asc)
{
    s_stage_len = 0U;
    scsi_sense_code(udev, lun, ILLEGAL_REQUEST, asc);
    return -1;
}

static int8_t scsi_media_guard(usb_dev *udev, uint8_t lun)
{
    if (lun >= MEM_LUN_NUM) {
        scsi_sense_code(udev, lun, ILLEGAL_REQUEST, INVALID_CDB);
        return -1;
    }
    if (0U != usbd_mem_fops->mem_ready(lun)) {
        scsi_sense_code(udev, lun, NOT_READY, MEDIUM_NOT_PRESENT);
        return -1;
    }
    return 0;
}

/*
 * 解析并校验一次块传输：CBW 传输长度必须正好是 blocks * block_size，
 * 否则无法用"整块暂存 + 定长分片"表达（Hi <> Dn 属非法命令）。
 * 通过后 scsi_blk_addr 是字节地址、scsi_blk_len 是剩余字节数。
 */
static int8_t scsi_block_setup(usb_dev *udev, uint8_t lun,
                               uint32_t first_block, uint32_t blocks,
                               uint32_t transfer_length)
{
    usbd_msc_handler *msc = scsi_handler(udev);
    uint32_t block_size = msc->scsi_blk_size[lun];

    if ((0U == block_size) || (block_size > MSC_MEDIA_PACKET_SIZE)) {
        scsi_sense_code(udev, lun, HARDWARE_ERROR, UNRECOVERED_READ_ERROR);
        return -1;
    }
    if ((0U == blocks) || (blocks * block_size != transfer_length)) {
        scsi_sense_code(udev, lun, ILLEGAL_REQUEST, INVALID_CDB);
        return -1;
    }
    if ((blocks > msc->scsi_blk_nbr[lun]) ||
        (first_block > (msc->scsi_blk_nbr[lun] - blocks))) {
        scsi_sense_code(udev, lun, ILLEGAL_REQUEST, ADDRESS_OUT_OF_RANGE);
        return -1;
    }
    s_stage_len = 0U;
    msc->scsi_blk_addr = first_block * block_size;
    msc->scsi_blk_len = blocks * block_size;
    return 0;
}

/*
 * IN 数据阶段：每次至多交出一个端点事务长度。先发数据、后定状态——最后一个分片
 * 发出时状态必须已是 BBB_LAST_DATA_IN，厂商 msc_bbb_data_in() 才会在那一次完成
 * 回调里发 CSW；顺序颠倒会让主机永远等不到状态。
 */
static int8_t scsi_send_next(usb_dev *udev, uint8_t lun)
{
    usbd_msc_handler *msc = scsi_handler(udev);
    uint32_t block_size = msc->scsi_blk_size[lun];
    uint32_t chunk;

    if (0U == msc->scsi_blk_len) {
        msc->bbb_state = BBB_LAST_DATA_IN;
        return 0;
    }
    if (s_stage_len == 0U) {
        /* 暂存空：从存储后端读一整块进 RAM，长度按块计。 */
        if (usbd_mem_fops->mem_read(lun, msc->bbb_data,
                                    msc->scsi_blk_addr, 1U) < 0) {
            scsi_sense_code(udev, lun, HARDWARE_ERROR,
                            UNRECOVERED_READ_ERROR);
            return -1;
        }
        s_stage_len = block_size;
        msc->scsi_blk_addr += block_size;
    }
    chunk = min_u32(min_u32(msc->scsi_blk_len, s_stage_len),
                    MSC_DATA_PACKET_SIZE);
    usbd_ep_send(udev, MSC_IN_EP, msc->bbb_data, (uint16_t)chunk);
    /* 已交出的部分留在 RAM 头部：用长度递减排削，避免额外的偏移变量。 */
    s_stage_len -= chunk;
    msc->scsi_blk_len -= chunk;
    msc->bbb_csw.dCSWDataResidue -= chunk;
    if (0U != s_stage_len) {
        (void)memmove(msc->bbb_data, &msc->bbb_data[chunk], s_stage_len);
    }
    msc->bbb_state = (0U == msc->scsi_blk_len) ? BBB_LAST_DATA_IN
                                               : BBB_DATA_IN;
    return 0;
}

/* OUT 数据阶段：为暂存扇区的当前位置武装一次接收，长度不超过事务上限。 */
static void scsi_receive_arm(usb_dev *udev, uint8_t lun)
{
    usbd_msc_handler *msc = scsi_handler(udev);
    uint32_t room = msc->scsi_blk_size[lun] - s_stage_len;
    uint32_t want = min_u32(min_u32(room, msc->scsi_blk_len),
                            MSC_DATA_PACKET_SIZE);

    usbd_ep_recev(udev, MSC_OUT_EP, &msc->bbb_data[s_stage_len],
                  (uint16_t)want);
}

/*
 * OUT 完成回调：先结算刚收到的这一片（长度只认 xfer_count，主机可以短包收尾），
 * 攒满一块就落盘；数据全部到齐才发 CSW。
 */
static int8_t scsi_receive_settle(usb_dev *udev, uint8_t lun)
{
    usbd_msc_handler *msc = scsi_handler(udev);
    uint32_t block_size = msc->scsi_blk_size[lun];
    uint32_t received = udev->transc_out[EP_ID(MSC_OUT_EP)].xfer_count;

    if (received > msc->scsi_blk_len) {
        s_stage_len = 0U;
        scsi_sense_code(udev, lun, ILLEGAL_REQUEST,
                        INVALID_FIELD_IN_COMMAND);
        return -1;
    }
    s_stage_len += received;
    msc->scsi_blk_len -= received;
    msc->bbb_csw.dCSWDataResidue -= received;

    if (s_stage_len == block_size) {
        if (usbd_mem_fops->mem_write(lun, msc->bbb_data,
                                     msc->scsi_blk_addr, 1U) < 0) {
            scsi_sense_code(udev, lun, HARDWARE_ERROR, WRITE_FAULT);
            return -1;
        }
        msc->scsi_blk_addr += block_size;
        s_stage_len = 0U;
    }
    if (0U != msc->scsi_blk_len) {
        scsi_receive_arm(udev, lun);
        return 0;
    }
    if (0U != s_stage_len) {
        /* 非整块收尾：scsi_block_setup() 已拒掉这种请求，走到这里即实现缺陷。 */
        scsi_sense_code(udev, lun, ILLEGAL_REQUEST, INVALID_CDB);
        return -1;
    }
    s_stage_len = 0U;
    msc_bbb_csw_send(udev, CSW_CMD_PASSED);
    /* 必须自己回到 IDLE：厂商只在 IN 收尾的 msc_bbb_data_in() 里做这件事。
     * 漏掉会让下一条 CBW 被当成续传，写入之后的所有命令全部失效。 */
    msc->bbb_state = BBB_IDLE;
    return 0;
}

/*
 * 短回复（INQUIRY/REQUEST SENSE/READ CAPACITY/MODE SENSE 等）：填好 bbb_data
 * 后把状态留在 IDLE，由未改动的厂商 msc_bbb_cbw_decode() 一次性发出。长度必须
 * 不超过一个事务，否则又回到撑爆 PMA 的老路。
 */
static int8_t scsi_short_reply(usb_dev *udev, uint8_t lun, uint32_t length)
{
    usbd_msc_handler *msc = scsi_handler(udev);

    if (length > MSC_DATA_PACKET_SIZE) {
        scsi_sense_code(udev, lun, ILLEGAL_REQUEST,
                        INVALID_FIELD_IN_COMMAND);
        return -1;
    }
    s_stage_len = 0U;
    msc->bbb_datalen = length;
    return 0;
}

static uint32_t scsi_read_lba10(const uint8_t *cmd)
{
    return ((uint32_t)cmd[2] << 24) | ((uint32_t)cmd[3] << 16) |
           ((uint32_t)cmd[4] << 8) | cmd[5];
}

static uint32_t scsi_read_lba6(const uint8_t *cmd)
{
    return (((uint32_t)cmd[1] & 0x1FU) << 16) | ((uint32_t)cmd[2] << 8) |
           cmd[3];
}

static uint32_t scsi_transfer_blocks(const uint8_t *cmd, uint8_t byte)
{
    /* READ(6)/WRITE(6) 的传输长度为 0 表示 256 块。 */
    return (0U == cmd[byte]) ? 256U : cmd[byte];
}

static int8_t scsi_read_cmd(usb_dev *udev, uint8_t lun, uint32_t first_block,
                           uint32_t blocks)
{
    usbd_msc_handler *msc = scsi_handler(udev);

    if (scsi_block_setup(udev, lun, first_block, blocks,
                         msc->bbb_cbw.dCBWDataTransferLength) < 0) {
        return -1;
    }
    msc->bbb_state = BBB_DATA_IN;
    return scsi_send_next(udev, lun);
}

static int8_t scsi_write_cmd(usb_dev *udev, uint8_t lun, uint32_t first_block,
                            uint32_t blocks)
{
    usbd_msc_handler *msc = scsi_handler(udev);

    if (0U != usbd_mem_fops->mem_protected(lun)) {
        scsi_sense_code(udev, lun, NOT_READY, WRITE_PROTECTED);
        return -1;
    }
    if (scsi_block_setup(udev, lun, first_block, blocks,
                         msc->bbb_cbw.dCBWDataTransferLength) < 0) {
        return -1;
    }
    msc->bbb_state = BBB_DATA_OUT;
    scsi_receive_arm(udev, lun);
    return 0;
}

static int8_t scsi_inquiry(usb_dev *udev, uint8_t lun, const uint8_t *cmd)
{
    usbd_msc_handler *msc = scsi_handler(udev);
    const uint8_t *inquiry = usbd_mem_fops->mem_inquiry_data[lun];

    if (0U != (cmd[1] & 0x01U)) {
        /* EVPD 页（单位序列号等）本卷不发布。 */
        return scsi_reject(udev, lun, INVALID_FIELD_IN_COMMAND);
    }
    if (NULL == inquiry) {
        scsi_sense_code(udev, lun, HARDWARE_ERROR, UNRECOVERED_READ_ERROR);
        return -1;
    }
    (void)memset(msc->bbb_data, 0, SCSI_STD_INQUIRY_LENGTH);
    (void)memcpy(msc->bbb_data, inquiry, SCSI_STD_INQUIRY_LENGTH);
    return scsi_short_reply(udev, lun, SCSI_STD_INQUIRY_LENGTH);
}

static int8_t scsi_request_sense(usb_dev *udev, uint8_t lun)
{
    usbd_msc_handler *msc = scsi_handler(udev);
    uint8_t skey;
    uint8_t asc;

    (void)memset(msc->bbb_data, 0, SCSI_SENSE_LENGTH);
    scsi_sense_take(udev, &skey, &asc);
    msc->bbb_data[0] = 0x70U;                        /* 当前错误响应码 */
    msc->bbb_data[2] = skey;
    msc->bbb_data[7] = (uint8_t)(SCSI_SENSE_LENGTH - 8U);
    msc->bbb_data[12] = asc;
    return scsi_short_reply(udev, lun, SCSI_SENSE_LENGTH);
}

static int8_t scsi_read_capacity(usb_dev *udev, uint8_t lun)
{
    usbd_msc_handler *msc = scsi_handler(udev);
    uint32_t blocks = msc->scsi_blk_nbr[lun];

    if (0U == blocks) {
        scsi_sense_code(udev, lun, NOT_READY, MEDIUM_NOT_PRESENT);
        return -1;
    }
    encode_be32(msc->bbb_data, blocks - 1U);
    encode_be32(&msc->bbb_data[4], msc->scsi_blk_size[lun]);
    return scsi_short_reply(udev, lun, SCSI_CAPACITY_LENGTH);
}

static int8_t scsi_read_format_capacities(usb_dev *udev, uint8_t lun)
{
    usbd_msc_handler *msc = scsi_handler(udev);
    uint32_t blocks = msc->scsi_blk_nbr[lun];

    (void)memset(msc->bbb_data, 0, SCSI_FORMAT_CAP_LENGTH);
    msc->bbb_data[3] = SCSI_BLOCK_DESCRIPTOR;        /* 容量列表长度 */
    encode_be32(&msc->bbb_data[4], blocks - 1U);
    encode_be32(&msc->bbb_data[8],
                msc->scsi_blk_size[lun] | SCSI_FORMATTED_FLAG);
    return scsi_short_reply(udev, lun, SCSI_FORMAT_CAP_LENGTH);
}

static void scsi_block_descriptor(uint8_t *output, uint32_t blocks,
                                  uint32_t block_size)
{
    output[0] = 0U;                                  /* density code */
    encode_be32(&output[1], blocks);
    output[5] = 0U;                                  /* 保留 */
    encode_be16(&output[6], (uint16_t)block_size);
}

static int8_t scsi_mode_sense6(usb_dev *udev, uint8_t lun, const uint8_t *cmd)
{
    usbd_msc_handler *msc = scsi_handler(udev);
    uint32_t length = SCSI_MODE6_HEADER_LENGTH;

    (void)memset(msc->bbb_data, 0, SCSI_MODE6_HEADER_LENGTH +
                 SCSI_BLOCK_DESCRIPTOR);
    msc->bbb_data[0] = (uint8_t)(SCSI_MODE6_HEADER_LENGTH - 1U);
    if (0U == (cmd[1] & 0x08U)) {
        msc->bbb_data[3] = SCSI_BLOCK_DESCRIPTOR;
        scsi_block_descriptor(&msc->bbb_data[4], msc->scsi_blk_nbr[lun],
                              msc->scsi_blk_size[lun]);
        length += SCSI_BLOCK_DESCRIPTOR;
    }
    return scsi_short_reply(udev, lun, length);
}

static int8_t scsi_mode_sense10(usb_dev *udev, uint8_t lun, const uint8_t *cmd)
{
    usbd_msc_handler *msc = scsi_handler(udev);
    uint32_t length = SCSI_MODE10_HEADER_LENGTH;

    (void)memset(msc->bbb_data, 0, SCSI_MODE10_HEADER_LENGTH +
                 SCSI_BLOCK_DESCRIPTOR);
    encode_be16(msc->bbb_data, (uint16_t)(SCSI_MODE10_HEADER_LENGTH - 2U));
    if (0U == (cmd[1] & 0x08U)) {
        encode_be16(&msc->bbb_data[6], SCSI_BLOCK_DESCRIPTOR);
        scsi_block_descriptor(&msc->bbb_data[8], msc->scsi_blk_nbr[lun],
                              msc->scsi_blk_size[lun]);
        length += SCSI_BLOCK_DESCRIPTOR;
    }
    return scsi_short_reply(udev, lun, length);
}

int8_t scsi_process_cmd(usb_dev *udev, uint8_t lun, uint8_t *cmd)
{
    usbd_msc_handler *msc = scsi_handler(udev);

    if (BBB_IDLE != msc->bbb_state) {
        /* 传输进行中：由 BOT 的端点完成回调把我们叫回来续传。 */
        switch (msc->bbb_state) {
        case BBB_DATA_IN:
            return scsi_send_next(udev, lun);

        case BBB_DATA_OUT:
            return scsi_receive_settle(udev, lun);

        default:
            return 0;
        }
    }

    /* 新命令：丢弃上一轮的半截状态，并刷新几何，不依赖外部初始化次序。 */
    s_stage_len = 0U;
    scsi_geometry_sync(udev, lun);

    /* REQUEST SENSE 必须在介质未就绪时也能取到原因，其余命令先过介质门。 */
    if ((SCSI_REQUEST_SENSE != cmd[0]) &&
        (scsi_media_guard(udev, lun) < 0)) {
        return -1;
    }

    switch (cmd[0]) {
    case SCSI_TEST_UNIT_READY:
    case SCSI_START_STOP_UNIT:
    case SCSI_ALLOW_MEDIUM_REMOVAL:
    case SCSI_VERIFY10:
        return scsi_short_reply(udev, lun, 0U);

    case SCSI_REQUEST_SENSE:
        return scsi_request_sense(udev, lun);

    case SCSI_INQUIRY:
        return scsi_inquiry(udev, lun, cmd);

    case SCSI_READ_CAPACITY10:
        return scsi_read_capacity(udev, lun);

    case SCSI_READ_FORMAT_CAPACITIES:
        return scsi_read_format_capacities(udev, lun);

    case SCSI_MODE_SENSE6:
        return scsi_mode_sense6(udev, lun, cmd);

    case SCSI_MODE_SENSE10:
        return scsi_mode_sense10(udev, lun, cmd);

    case SCSI_READ6:
        return scsi_read_cmd(udev, lun, scsi_read_lba6(cmd),
                             scsi_transfer_blocks(cmd, 4U));

    case SCSI_READ10:
        return scsi_read_cmd(udev, lun, scsi_read_lba10(cmd),
                             ((uint32_t)cmd[7] << 8) | cmd[8]);

    case SCSI_WRITE6:
        return scsi_write_cmd(udev, lun, scsi_read_lba6(cmd),
                              scsi_transfer_blocks(cmd, 4U));

    case SCSI_WRITE10:
        return scsi_write_cmd(udev, lun, scsi_read_lba10(cmd),
                              ((uint32_t)cmd[7] << 8) | cmd[8]);

    default:
        return scsi_reject(udev, lun, INVALID_CDB);
    }
}
