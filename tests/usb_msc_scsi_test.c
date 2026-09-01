#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "usbd_msc_bbb.h"
#include "usbd_msc_mem.h"

/*
 * 回归目标：本项目自带的 SCSI 核心绝不能把超过一个端点事务（= MSC PMA 槽长）
 * 的数据交给 usbd_ep_send()/usbd_ep_recev()。驱动层的 usbd_ep_data_write() 不
 * 按 maxpacket 也不按槽长夹取，一旦超限就会踩过相邻端点缓冲并越出 PMA 末尾，
 * 现场表现为"复合设备已枚举、CDC 正常，但 MSC 与 WinUSB 近一分钟起不来"。
 * 所以每个桩函数里的那条长度断言才是本文件真正在守的东西。
 */
#define IMAGE_BLOCKS        32U
#define IMAGE_BLOCK_SIZE    512U
#define IMAGE_SIZE          (IMAGE_BLOCKS * IMAGE_BLOCK_SIZE)
#define MAX_EVENTS          64U
#define RX_INDEX            (EP_ID(MSC_OUT_EP))
/* 与 usb_msc_scsi.c 里的私有定义一致：那两个长度是 SCSI 定长页，改了一边就应
 * 在这里编译失败。 */
#define INQUIRY_LENGTH      36U
#define SENSE_LENGTH        18U
#define INQUIRY_CDB_LENGTH  16U

static uint8_t s_image[IMAGE_SIZE];
static uint8_t s_inquiry[INQUIRY_LENGTH];

static uint8_t s_event_ep[MAX_EVENTS];
static uint16_t s_event_len[MAX_EVENTS];
static uint8_t s_event_bytes[MAX_EVENTS][MSC_DATA_PACKET_SIZE];
static uint8_t *s_event_target[MAX_EVENTS];
static unsigned s_event_count;
static unsigned s_csw_count;
static uint8_t s_csw_status;
static unsigned s_read_calls;
static unsigned s_write_calls;
static uint32_t s_write_addr;

static uint8_t image_byte(uint32_t address)
{
    return (uint8_t)(address ^ 0x5AU);
}

void usbd_ep_send(usb_dev *udev, uint8_t ep_addr, uint8_t *pbuf,
                  uint16_t buf_len)
{
    (void)udev;
    assert(s_event_count < MAX_EVENTS);
    assert(buf_len <= (uint16_t)MSC_DATA_PACKET_SIZE);
    s_event_ep[s_event_count] = ep_addr;
    s_event_len[s_event_count] = buf_len;
    memcpy(s_event_bytes[s_event_count], pbuf, buf_len);
    s_event_target[s_event_count] = NULL;
    ++s_event_count;
}

void usbd_ep_recev(usb_dev *udev, uint8_t ep_addr, uint8_t *pbuf,
                   uint16_t buf_len)
{
    (void)udev;
    assert(s_event_count < MAX_EVENTS);
    assert(buf_len <= (uint16_t)MSC_DATA_PACKET_SIZE);
    assert(buf_len > 0U);
    s_event_ep[s_event_count] = ep_addr;
    s_event_len[s_event_count] = buf_len;
    s_event_target[s_event_count] = pbuf;
    ++s_event_count;
}

void msc_bbb_csw_send(usb_dev *udev, uint8_t csw_status)
{
    (void)udev;
    ++s_csw_count;
    s_csw_status = csw_status;
}

static int8_t memory_init(uint8_t lun)
{
    (void)lun;
    return 0;
}

static int8_t memory_ready(uint8_t lun)
{
    (void)lun;
    return 0;
}

static int8_t memory_protected(uint8_t lun)
{
    (void)lun;
    return 0;
}

static int8_t memory_read(uint8_t lun, uint8_t *buffer, uint32_t block_addr,
                          uint16_t block_len)
{
    (void)lun;
    /* 暂存区按整块交换：块数必须是 1，且地址落在块边界上。 */
    assert(block_len == 1U);
    assert((block_addr % IMAGE_BLOCK_SIZE) == 0U);
    assert((block_addr + IMAGE_BLOCK_SIZE) <= IMAGE_SIZE);
    for (uint32_t index = 0U; index < IMAGE_BLOCK_SIZE; ++index) {
        buffer[index] = s_image[block_addr + index];
    }
    ++s_read_calls;
    return 0;
}

static int8_t memory_write(uint8_t lun, uint8_t *buffer, uint32_t block_addr,
                           uint16_t block_len)
{
    (void)lun;
    assert(block_len == 1U);
    assert((block_addr % IMAGE_BLOCK_SIZE) == 0U);
    assert((block_addr + IMAGE_BLOCK_SIZE) <= IMAGE_SIZE);
    memcpy(&s_image[block_addr], buffer, IMAGE_BLOCK_SIZE);
    s_write_addr = block_addr;
    ++s_write_calls;
    return 0;
}

static int8_t memory_max_lun(void)
{
    return 0;
}

static usbd_mem_cb s_memory_ops = {
    .mem_init = memory_init,
    .mem_ready = memory_ready,
    .mem_protected = memory_protected,
    .mem_read = memory_read,
    .mem_write = memory_write,
    .mem_maxlun = memory_max_lun,
    .mem_inquiry_data = {s_inquiry},
    .mem_block_size = {IMAGE_BLOCK_SIZE},
    .mem_block_len = {IMAGE_BLOCKS}
};

usbd_mem_cb *usbd_mem_fops = &s_memory_ops;

static usbd_msc_handler s_msc;
static usb_dev s_device;

static void reset_case(void)
{
    (void)memset(s_image, 0, sizeof(s_image));
    for (uint32_t index = 0U; index < IMAGE_SIZE; ++index) {
        s_image[index] = image_byte(index);
    }
    (void)memset(&s_msc, 0, sizeof(s_msc));
    (void)memset(&s_device, 0, sizeof(s_device));
    s_device.class_data[USBD_MSC_INTERFACE] = &s_msc;
    s_event_count = 0U;
    s_csw_count = 0U;
    s_csw_status = 0xFFU;
    s_read_calls = 0U;
    s_write_calls = 0U;
    s_write_addr = 0U;
}

/* 模拟厂商 msc_bbb_cbw_decode() 收到的 CBW。 */
static void begin_command(uint32_t transfer_length, uint8_t flags,
                          const uint8_t *cdb, uint8_t cdb_length)
{
    s_msc.bbb_cbw.dCBWSignature = BBB_CBW_SIGNATURE;
    s_msc.bbb_cbw.dCBWTag = 0x11223344U;
    s_msc.bbb_cbw.dCBWDataTransferLength = transfer_length;
    s_msc.bbb_cbw.bmCBWFlags = flags;
    s_msc.bbb_cbw.bCBWLUN = 0U;
    s_msc.bbb_cbw.bCBWCBLength = cdb_length;
    memcpy(s_msc.bbb_cbw.CBWCB, cdb, 16U);
    s_msc.bbb_csw.dCSWTag = s_msc.bbb_cbw.dCBWTag;
    s_msc.bbb_csw.dCSWDataResidue = transfer_length;
}

static void cdb_read10(uint8_t *cdb, uint32_t lba, uint16_t blocks)
{
    (void)memset(cdb, 0, 16U);
    cdb[0] = SCSI_READ10;
    cdb[2] = (uint8_t)(lba >> 24);
    cdb[3] = (uint8_t)(lba >> 16);
    cdb[4] = (uint8_t)(lba >> 8);
    cdb[5] = (uint8_t)lba;
    cdb[7] = (uint8_t)(blocks >> 8);
    cdb[8] = (uint8_t)blocks;
}

static void cdb_write10(uint8_t *cdb, uint32_t lba, uint16_t blocks)
{
    cdb_read10(cdb, lba, blocks);
    cdb[0] = SCSI_WRITE10;
}

/* 读两个扇区：必须切成 32 个 64 字节事务，内容与卷一致，且期间不发 CSW。 */
static void test_read_slices_every_transaction_to_pma_limit(void)
{
    uint8_t cdb[16];
    uint32_t lba = 3U;
    uint32_t blocks = 2U;
    uint32_t delivered = 0U;
    unsigned event;

    reset_case();
    cdb_read10(cdb, lba, blocks);
    begin_command(blocks * IMAGE_BLOCK_SIZE, 0x80U, cdb, 16U);

    assert(scsi_process_cmd(&s_device, 0U, cdb) == 0);
    assert(s_msc.bbb_state == BBB_DATA_IN);

    /* 反复模拟 IN 完成回调，直到状态机进入 LAST_DATA_IN。 */
    while (s_msc.bbb_state == BBB_DATA_IN) {
        assert(scsi_process_cmd(&s_device, 0U, cdb) == 0);
    }

    assert(s_msc.bbb_state == BBB_LAST_DATA_IN);
    assert(s_event_count == blocks * IMAGE_BLOCK_SIZE / MSC_DATA_PACKET_SIZE);
    for (event = 0U; event < s_event_count; ++event) {
        assert(s_event_ep[event] == MSC_IN_EP);
        assert(s_event_len[event] == MSC_DATA_PACKET_SIZE);
    }
    for (event = 0U; event < s_event_count; ++event) {
        assert(memcmp(s_event_bytes[event], &s_image[lba * IMAGE_BLOCK_SIZE +
                    delivered], MSC_DATA_PACKET_SIZE) == 0);
        delivered += MSC_DATA_PACKET_SIZE;
    }
    assert(delivered == blocks * IMAGE_BLOCK_SIZE);
    assert(s_read_calls == blocks);
    assert(s_msc.bbb_csw.dCSWDataResidue == 0U);
    assert(s_csw_count == 0U);
    /* 完成回调进 LAST_DATA_IN 时不再发数据：由 BOT 层发 CSW。 */
    assert(scsi_process_cmd(&s_device, 0U, cdb) == 0);
    assert(s_event_count == blocks * IMAGE_BLOCK_SIZE / MSC_DATA_PACKET_SIZE);
}

/* 写一个扇区：8 次接收武装、1 次落盘、恰好一个 CSW。 */
static void test_write_commits_block_only_when_stage_fills(void)
{
    uint8_t cdb[16];
    uint32_t lba = 5U;
    unsigned round;

    reset_case();
    cdb_write10(cdb, lba, 1U);
    begin_command(IMAGE_BLOCK_SIZE, 0x00U, cdb, 16U);

    assert(scsi_process_cmd(&s_device, 0U, cdb) == 0);
    assert(s_msc.bbb_state == BBB_DATA_OUT);
    assert(s_event_count == 1U);
    assert(s_event_ep[0] == MSC_OUT_EP);
    assert(s_event_len[0] == MSC_DATA_PACKET_SIZE);
    assert(s_write_calls == 0U);

    for (round = 0U; round < IMAGE_BLOCK_SIZE / MSC_DATA_PACKET_SIZE; ++round) {
        uint32_t offset = round * MSC_DATA_PACKET_SIZE;
        uint8_t *target = s_event_target[round];
        unsigned events_now = s_event_count;

        assert(target != NULL);
        for (uint32_t index = 0U; index < MSC_DATA_PACKET_SIZE; ++index) {
            target[index] = (uint8_t)(0xC0U + offset + index);
        }
        s_device.transc_out[RX_INDEX].xfer_count = MSC_DATA_PACKET_SIZE;

        assert(scsi_process_cmd(&s_device, 0U, cdb) == 0);
        if (round + 1U < IMAGE_BLOCK_SIZE / MSC_DATA_PACKET_SIZE) {
            assert(s_write_calls == 0U);
            assert(s_event_count == events_now + 1U);
            assert(s_event_len[s_event_count - 1U] == MSC_DATA_PACKET_SIZE);
        }
    }

    assert(s_write_calls == 1U);
    assert(s_write_addr == lba * IMAGE_BLOCK_SIZE);
    assert(s_csw_count == 1U);
    assert(s_csw_status == CSW_CMD_PASSED);
    assert(s_msc.bbb_csw.dCSWDataResidue == 0U);
    for (uint32_t index = 0U; index < IMAGE_BLOCK_SIZE; ++index) {
        assert(s_image[lba * IMAGE_BLOCK_SIZE + index] ==
               (uint8_t)(0xC0U + index));
    }

    /* 状态交接：写完之后必须已经回到 IDLE，否则下一条 CBW 会被当成续传，
     * 整条后续命令链失效。这里故意不重置处理器，把交接当真实情况验证。 */
    assert(s_msc.bbb_state == BBB_IDLE);
    (void)memset(cdb, 0, sizeof(cdb));
    cdb[0] = SCSI_TEST_UNIT_READY;
    begin_command(0U, 0x00U, cdb, 16U);
    assert(scsi_process_cmd(&s_device, 0U, cdb) == 0);
    assert(s_msc.bbb_datalen == 0U);
    assert(s_event_count == IMAGE_BLOCK_SIZE / MSC_DATA_PACKET_SIZE);
}

/* 短包收尾：xfer_count 才是真相，长度超出剩余量必须按协议违例拒绝。 */
static void test_write_rejects_overlong_packet(void)
{
    uint8_t cdb[16];

    reset_case();
    cdb_write10(cdb, 1U, 1U);
    begin_command(IMAGE_BLOCK_SIZE, 0x00U, cdb, 16U);
    assert(scsi_process_cmd(&s_device, 0U, cdb) == 0);

    /* 主机多发了一个整块：超出声明长度即协议违例，不得落盘。 */
    s_device.transc_out[RX_INDEX].xfer_count = IMAGE_BLOCK_SIZE * 2U;
    assert(scsi_process_cmd(&s_device, 0U, cdb) < 0);
    assert(s_write_calls == 0U);
    assert(s_csw_count == 0U);
}

/* 短回复必须一次装得下：长度、内容、几何都按 SCSI 规范核对。 */
static void test_short_replies_stay_within_one_transaction(void)
{
    uint8_t cdb[16];

    reset_case();
    (void)memset(cdb, 0, sizeof(cdb));

    cdb[0] = SCSI_READ_CAPACITY10;
    begin_command(IMAGE_BLOCK_SIZE, 0x80U, cdb, 16U);
    assert(scsi_process_cmd(&s_device, 0U, cdb) == 0);
    assert(s_msc.bbb_datalen == 8U);
    assert(s_msc.bbb_data[0] == (uint8_t)((IMAGE_BLOCKS - 1U) >> 24));
    assert(s_msc.bbb_data[3] == (uint8_t)(IMAGE_BLOCKS - 1U));
    assert(s_msc.bbb_data[4] == (uint8_t)(IMAGE_BLOCK_SIZE >> 24));
    assert(s_msc.bbb_data[7] == (uint8_t)IMAGE_BLOCK_SIZE);

    (void)memset(cdb, 0, sizeof(cdb));
    cdb[0] = SCSI_MODE_SENSE6;
    begin_command(IMAGE_BLOCK_SIZE, 0x80U, cdb, 16U);
    assert(scsi_process_cmd(&s_device, 0U, cdb) == 0);
    assert(s_msc.bbb_datalen == 12U);           /* 头 4 + 块描述符 8 */
    assert(s_msc.bbb_data[0] == 3U);
    assert(s_msc.bbb_data[3] == 8U);

    (void)memset(cdb, 0, sizeof(cdb));
    cdb[0] = SCSI_MODE_SENSE6;
    cdb[1] = 0x08U;                              /* DBD：屏蔽块描述符 */
    begin_command(IMAGE_BLOCK_SIZE, 0x80U, cdb, 16U);
    assert(scsi_process_cmd(&s_device, 0U, cdb) == 0);
    assert(s_msc.bbb_datalen == 4U);
    assert(s_msc.bbb_data[3] == 0U);

    (void)memset(cdb, 0, sizeof(cdb));
    cdb[0] = SCSI_TEST_UNIT_READY;
    begin_command(0U, 0x80U, cdb, 16U);
    assert(scsi_process_cmd(&s_device, 0U, cdb) == 0);
    assert(s_msc.bbb_datalen == 0U);

    /* INQUIRY 长度必须正好是标准页，且不超过一个事务。 */
    (void)memset(cdb, 0, sizeof(cdb));
    cdb[0] = SCSI_INQUIRY;
    s_inquiry[0] = 0x00U;
    s_inquiry[2] = 0x02U;
    begin_command(INQUIRY_LENGTH, 0x80U, cdb, INQUIRY_CDB_LENGTH);
    assert(scsi_process_cmd(&s_device, 0U, cdb) == 0);
    assert(s_msc.bbb_datalen == INQUIRY_LENGTH);
    assert(s_msc.bbb_datalen <= (uint32_t)MSC_DATA_PACKET_SIZE);
    assert(s_msc.bbb_data[2] == 0x02U);
}

/* 非法几何/越界/未知命令都必须被拒且不留半截状态。 */
static void test_rejections_leave_no_partial_transfer(void)
{
    uint8_t cdb[16];

    reset_case();
    cdb_read10(cdb, 0U, 2U);
    begin_command(IMAGE_BLOCK_SIZE, 0x80U, cdb, 16U);   /* Hi <> Dn */
    assert(scsi_process_cmd(&s_device, 0U, cdb) < 0);
    assert(s_event_count == 0U);

    reset_case();
    cdb_read10(cdb, IMAGE_BLOCKS, 1U);
    begin_command(IMAGE_BLOCK_SIZE, 0x80U, cdb, 16U);   /* 越过卷尾 */
    assert(scsi_process_cmd(&s_device, 0U, cdb) < 0);
    assert(s_event_count == 0U);
    assert(s_msc.scsi_sense[1].ASC == ADDRESS_OUT_OF_RANGE);

    reset_case();
    (void)memset(cdb, 0, sizeof(cdb));
    cdb[0] = 0x35U;                                      /* SYNCHRONIZE CACHE */
    begin_command(0U, 0x00U, cdb, 16U);
    assert(scsi_process_cmd(&s_device, 0U, cdb) < 0);

    /* 跨命令的半截状态不得泄漏：上一轮被拒后，本轮读仍能完整分片。 */
    reset_case();
    cdb_read10(cdb, 0U, 1U);
    begin_command(IMAGE_BLOCK_SIZE, 0x80U, cdb, 16U);
    assert(scsi_process_cmd(&s_device, 0U, cdb) == 0);
    while (s_msc.bbb_state == BBB_DATA_IN) {
        assert(scsi_process_cmd(&s_device, 0U, cdb) == 0);
    }
    assert(s_msc.bbb_state == BBB_LAST_DATA_IN);
    assert(s_event_count == IMAGE_BLOCK_SIZE / MSC_DATA_PACKET_SIZE);
    assert(s_read_calls == 1U);
}

/* sense 环：最新错误一定可读，取走一次即消费。 */
static void test_sense_ring_reports_latest_and_consumes_once(void)
{
    uint8_t cdb[16];
    uint8_t skey;

    reset_case();
    scsi_sense_code(&s_device, 0U, ILLEGAL_REQUEST, INVALID_CDB);
    scsi_sense_code(&s_device, 0U, NOT_READY, MEDIUM_NOT_PRESENT);

    (void)memset(cdb, 0, sizeof(cdb));
    cdb[0] = SCSI_REQUEST_SENSE;
    begin_command(SENSE_LENGTH, 0x80U, cdb, 16U);
    assert(scsi_process_cmd(&s_device, 0U, cdb) == 0);
    assert(s_msc.bbb_datalen == SENSE_LENGTH);
    skey = s_msc.bbb_data[2];
    assert(skey == ILLEGAL_REQUEST);
    assert(s_msc.bbb_data[12] == INVALID_CDB);

    assert(scsi_process_cmd(&s_device, 0U, cdb) == 0);
    skey = s_msc.bbb_data[2];
    assert(skey == NOT_READY);

    assert(scsi_process_cmd(&s_device, 0U, cdb) == 0);
    assert(s_msc.bbb_data[2] == NO_SENSE);
}

int main(void)
{
    test_read_slices_every_transaction_to_pma_limit();
    test_write_commits_block_only_when_stage_fills();
    test_write_rejects_overlong_packet();
    test_short_replies_stay_within_one_transaction();
    test_rejections_leave_no_partial_transfer();
    test_sense_ring_reports_latest_and_consumes_once();
    return 0;
}
