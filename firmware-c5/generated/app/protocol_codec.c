#include "protocol_codec.h"
#include <string.h>

/* 协议中的多字节整数统一采用小端序，与主机 CPU 字节序无关。 */
static uint16_t read_u16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t read_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void write_u16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static void write_u32(uint8_t *p, uint32_t value)
{
    for (uint32_t i = 0; i < 4U; ++i) p[i] = (uint8_t)(value >> (8U * i));
}

uint16_t protocol_crc16(const uint8_t *data, uint16_t length)
{
    /* 不反射、不异或输出；编码和解析使用相同的 CRC 规则。 */
    uint16_t crc = 0xFFFFU;
    for (uint16_t i = 0; i < length; ++i) {
        crc ^= (uint16_t)data[i] << 8;
        for (uint32_t bit = 0; bit < 8U; ++bit) {
            crc = (crc & 0x8000U) ? (uint16_t)((crc << 1) ^ 0x1021U)
                                  : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

uint16_t protocol_encode(uint8_t *out, uint8_t type, uint16_t seq,
                         uint32_t sample_index, const uint8_t *payload,
                         uint16_t payload_length)
{
    /* CRC 从版本字段开始，覆盖负载；不包含同步头和 CRC 自身。 */
    if (payload_length > PROTOCOL_TX_MAX_BYTES - PROTOCOL_OVERHEAD_BYTES) return 0;
    out[0] = 0xA5U;
    out[1] = 0x5AU;
    out[2] = PROTOCOL_VERSION;
    out[3] = type;
    write_u16(out + 4, seq);
    write_u16(out + 6, payload_length);
    write_u32(out + 8, sample_index);
    if (payload_length != 0U) memcpy(out + PROTOCOL_HEADER_BYTES, payload, payload_length);
    const uint16_t crc = protocol_crc16(out + 2, (uint16_t)(10U + payload_length));
    write_u16(out + PROTOCOL_HEADER_BYTES + payload_length, crc);
    return (uint16_t)(payload_length + PROTOCOL_OVERHEAD_BYTES);
}

int protocol_parser_push(protocol_parser_t *parser, uint8_t byte,
                         protocol_packet_t *packet)
{
    /* 先找 A5 5A 同步头；连续 A5 时保留第二个 A5 作为新起点。 */
    if (parser->used == 0U) {
        if (byte == 0xA5U) parser->bytes[parser->used++] = byte;
        return 0;
    }
    if (parser->used == 1U) {
        if (byte == 0x5AU) parser->bytes[parser->used++] = byte;
        else parser->used = (byte == 0xA5U) ? 1U : 0U;
        return 0;
    }

    parser->bytes[parser->used++] = byte;
    if (parser->used == 8U) {
        /* 收齐长度字段后立即拒绝超长帧，防止写出接收数组。 */
        uint16_t length = read_u16(parser->bytes + 6);
        if (parser->bytes[2] != PROTOCOL_VERSION || length > PROTOCOL_COMMAND_MAX_BYTES) {
            ++parser->bad_frames;
            parser->used = 0;
            return 0;
        }
        parser->expected = (uint16_t)(PROTOCOL_OVERHEAD_BYTES + length);
    }
    if (parser->used != parser->expected) return 0;

    const uint16_t length = read_u16(parser->bytes + 6);
    const uint16_t received_crc = read_u16(parser->bytes + PROTOCOL_HEADER_BYTES + length);
    const uint16_t computed_crc = protocol_crc16(parser->bytes + 2, (uint16_t)(10U + length));
    parser->used = 0;
    /* 校验失败直接丢弃此帧，等待下一个同步头。 */
    if (received_crc != computed_crc) {
        ++parser->bad_frames;
        return 0;
    }
    packet->type = parser->bytes[3];
    packet->seq = read_u16(parser->bytes + 4);
    packet->length = length;
    packet->sample_index = read_u32(parser->bytes + 8);
    if (length != 0U) memcpy(packet->payload, parser->bytes + PROTOCOL_HEADER_BYTES, length);
    return 1;
}
