#ifndef ACOUSTICLAB_PROTOCOL_CODEC_H
#define ACOUSTICLAB_PROTOCOL_CODEC_H

#include <stdint.h>

/* 帧格式：同步头(2) + 版本/类型/序号/长度/采样序号(10) + 负载 + CRC(2)。 */
enum {
    PROTOCOL_VERSION = 1,
    PROTOCOL_HEADER_BYTES = 12,
    PROTOCOL_OVERHEAD_BYTES = 14,
    PROTOCOL_COMMAND_MAX_BYTES = 128,
    PROTOCOL_TX_MAX_BYTES = 1024
};

typedef enum {
    MSG_STATUS = 1,           /* 周期状态。 */
    MSG_LEVEL = 2,            /* 实时电平。 */
    MSG_SPECTRUM_PREVIEW = 3,
    MSG_WAVEFORM = 4,
    MSG_MEASUREMENT = 5,
    MSG_DATA_CHUNK = 6,
    MSG_COMMAND = 7,          /* 上位机命令。 */
    MSG_REPLY = 8             /* 命令应答。 */
} protocol_type_t;

typedef enum {
    CMD_GET_STATUS = 1,       /* 请求立即发送状态。 */
    CMD_SET_MODE = 2,         /* 请求切换运行模式。 */
    CMD_SET_TONE = 3          /* 独立测试音：使能、Hz、峰值 centidB。 */
} command_id_t;

typedef enum {
    REPLY_OK = 0,
    REPLY_BAD_LENGTH = 1,
    REPLY_BAD_ARGUMENT = 2,
    REPLY_UNSUPPORTED = 3
} reply_code_t;

/* 已解析的命令帧；负载上限只针对接收命令。 */
typedef struct {
    uint8_t type;
    uint16_t seq;
    uint32_t sample_index;
    uint16_t length;
    uint8_t payload[PROTOCOL_COMMAND_MAX_BYTES];
} protocol_packet_t;

/* 逐字节解析状态；bad_frames 统计版本、长度和 CRC 错误。 */
typedef struct {
    uint8_t bytes[PROTOCOL_HEADER_BYTES + PROTOCOL_COMMAND_MAX_BYTES + 2];
    uint16_t used;
    uint16_t expected;
    uint32_t bad_frames;
} protocol_parser_t;

/* CRC16/CCITT-FALSE：初值 0xFFFF，多项式 0x1021。 */
uint16_t protocol_crc16(const uint8_t *data, uint16_t length);
/* 输出缓冲区须至少有 PROTOCOL_TX_MAX_BYTES 字节；返回完整帧长度。 */
uint16_t protocol_encode(uint8_t *out, uint8_t type, uint16_t seq,
                         uint32_t sample_index, const uint8_t *payload,
                         uint16_t payload_length);
/* 每输入一个字节调用一次；返回 1 表示 packet 中已有完整、校验通过的帧。 */
int protocol_parser_push(protocol_parser_t *parser, uint8_t byte,
                         protocol_packet_t *packet);

#endif
