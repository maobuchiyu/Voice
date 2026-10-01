#include "protocol_uart.h"

#include "audio.h"
#include "generator.h"
#include "dsp.h"
#include "mx_usart2.h"
#include "protocol_codec.h"
#include "stm32_hal.h"
#include <string.h>

enum { RX_RING_SIZE = 512, STATUS_PERIOD_MS = 1000, LEVEL_PERIOD_MS = 100,
       WAVEFORM_PERIOD_MS = 100 };

/* RX 环形缓冲由中断写入、主循环读取；TX 缓冲在 DMA 完成前不可修改。 */
static uint8_t rx_ring[RX_RING_SIZE];
static uint8_t tx_frame[PROTOCOL_TX_MAX_BYTES];
static uint8_t rx_byte;
static volatile uint16_t rx_head;
static volatile uint16_t rx_tail;
static volatile uint8_t rx_armed;
static volatile uint8_t tx_busy;
static volatile uint32_t rx_overflow;
static volatile uint8_t uart_fault;
static volatile uint8_t rx_desynchronized;
static uint32_t tx_started_ms;
static audio_mode_t preview_mode;
static protocol_parser_t parser;
static protocol_packet_t packet;
static uint16_t outgoing_seq;
static uint32_t last_status_ms;
static uint32_t last_level_ms;
static uint32_t last_waveform_ms;
static uint32_t last_spectrum_ms;
static uint32_t last_spectrum_index;
static uint8_t spectrum_sent;
static uint32_t preview_skips;
static uint8_t status_requested;
static uint8_t reply_pending;
static uint16_t reply_seq;
static uint8_t reply_payload[4];

static void write_u16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static void write_u32(uint8_t *p, uint32_t value)
{
    for (uint32_t i = 0; i < 4U; ++i) p[i] = (uint8_t)(value >> (8U * i));
}
static uint32_t read_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void write_f32(uint8_t *p, float value)
{
    /* 按 IEEE-754 单精度位型写入小端序，避免直接依赖内存字节序。 */
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    write_u32(p, bits);
}

static void arm_receive(void)
{
    /* 每次只接收一个字节，回调中立即重新挂接下一次接收。 */
    if (HAL_UART_Receive_IT(mx_usart2_uart_gethandle(), &rx_byte, 1U) == HAL_OK) {
        rx_armed = 1U;
    }
}

/* 中断中只入环形缓冲并重新挂接接收；完整帧解析留给主循环。 */
void HAL_UART_RxCpltCallback(hal_uart_handle_t *huart, uint32_t size_byte,
                             hal_uart_rx_event_types_t rx_event)
{
    (void)huart;
    (void)rx_event;
    rx_armed = 0U;
    if (size_byte == 1U) {
        const uint16_t next = (uint16_t)((rx_head + 1U) & (RX_RING_SIZE - 1U));
        /* 缓冲满时丢弃新字节并计数，保留尚未消费的数据。 */
        if (next == rx_tail) {
            ++rx_overflow;
            rx_desynchronized = 1U;
        }
        else {
            rx_ring[rx_head] = rx_byte;
            rx_head = next;
        }
    }
    arm_receive();
}

void HAL_UART_ErrorCallback(hal_uart_handle_t *huart)
{
    /* 接收出错后由主循环尝试重新挂接接收。 */
    (void)huart;
    rx_armed = 0U;
    ++rx_overflow;
    uart_fault = 1U;
}

void HAL_UART_TxCpltCallback(hal_uart_handle_t *huart)
{
    /* DMA 发送完成后才能复用 tx_frame。 */
    (void)huart;
    tx_busy = 0U;
}

static int send_frame(uint8_t type, uint16_t seq, uint32_t sample_index,
                      const uint8_t *payload, uint16_t length)
{
    /* 波形帧使用首样本序号；其他帧使用当前已处理的样本序号。 */
    uint16_t frame_length = protocol_encode(tx_frame, type, seq,
                                            sample_index, payload, length);
    if (frame_length == 0U) return 0;
    tx_busy = 1U;
    tx_started_ms = HAL_GetTick();
    if (HAL_UART_Transmit_DMA(mx_usart2_uart_gethandle(), tx_frame,
                              frame_length) != HAL_OK) {
        tx_busy = 0U;
        return 0;
    }
    return 1;
}

static void queue_reply(const protocol_packet_t *command, reply_code_t code)
{
    audio_status_t status;
    audio_get_status(&status);
    reply_seq = command->seq;
    reply_payload[0] = command->length ? command->payload[0] : 0U;
    reply_payload[1] = (uint8_t)code;
    reply_payload[2] = (uint8_t)status.mode;
    reply_payload[3] = 0U;
    reply_pending = 1U;
}

static void handle_command(const protocol_packet_t *command)
{
    /* 先验证命令长度和参数；模式切换由音频模块在块边界执行。 */
    reply_code_t result = REPLY_OK;
    if (command->length == 0U) result = REPLY_BAD_LENGTH;
    else if (command->payload[0] == CMD_GET_STATUS) {
        if (command->length != 1U) result = REPLY_BAD_LENGTH;
        else status_requested = 1U;
    } else if (command->payload[0] == CMD_SET_MODE) {
        if (command->length != 2U) result = REPLY_BAD_LENGTH;
        else if (command->payload[1] > AUDIO_MODE_RTA) result = REPLY_BAD_ARGUMENT;
        else audio_request_mode((audio_mode_t)command->payload[1]);
    } else if (command->payload[0] == CMD_SET_TONE) {
        if (command->length != 8U) result = REPLY_BAD_LENGTH;
        else {
            generator_config_t config;
            config.enabled = command->payload[1];
            config.frequency_hz = read_u32(command->payload + 2);
            config.peak_centidb = (int16_t)((uint16_t)command->payload[6] |
                                        ((uint16_t)command->payload[7] << 8));
            if (config.enabled > 1U || config.frequency_hz < 20U || config.frequency_hz > 20000U ||
                config.peak_centidb < -4000 || config.peak_centidb > -600) result = REPLY_BAD_ARGUMENT;
            else generator_request(&config);
        }
    } else result = REPLY_UNSUPPORTED;
    queue_reply(command, result);
    /* 请求立即上报；块边界提交后的周期 STATUS 用于确认配置已生效。 */
    if (result == REPLY_OK) status_requested = 1U;
}

int protocol_uart_start(void)
{
    arm_receive();
    return rx_armed ? 0 : -1;
}

void protocol_uart_poll(void)
{
    /* UART 错误和超时恢复放主循环，ISR 不等待 DMA 中止。 */
    if (uart_fault || (tx_busy && (uint32_t)(HAL_GetTick() - tx_started_ms) > 250U)) {
        uart_fault = 0U;
        (void)HAL_UART_AbortTransmit(mx_usart2_uart_gethandle());
        (void)HAL_UART_AbortReceive(mx_usart2_uart_gethandle());
        tx_busy = rx_armed = 0U;
        rx_desynchronized = 1U;
    }
    if (rx_desynchronized) {
        uint32_t irq_state = __get_PRIMASK();
        __disable_irq();
        rx_tail = rx_head;
        rx_desynchronized = 0U;
        __set_PRIMASK(irq_state);
        parser.used = parser.expected = 0U;
    }
    /* UART 错误后持续尝试恢复接收，并消费中断已收下的字节。 */
    if (!rx_armed) arm_receive();
    while (!reply_pending && rx_tail != rx_head) {
        uint8_t byte = rx_ring[rx_tail];
        rx_tail = (uint16_t)((rx_tail + 1U) & (RX_RING_SIZE - 1U));
        if (protocol_parser_push(&parser, byte, &packet) &&
            packet.type == MSG_COMMAND) {
            handle_command(&packet);
        }
    }

    /* 单个 TX DMA 缓冲只能容纳一帧；应答优先于周期上报。 */
    if (tx_busy) return;
    if (reply_pending) {
        audio_status_t reply_status;
        audio_get_status(&reply_status);
        if (send_frame(MSG_REPLY, reply_seq, reply_status.sample_index,
                       reply_payload, sizeof(reply_payload))) {
            reply_pending = 0U;
        }
        return;
    }

    const uint32_t now = HAL_GetTick();
    audio_status_t status;
    audio_get_status(&status);
    if (preview_mode != status.mode) {
        preview_mode = status.mode;
        last_level_ms = last_waveform_ms = last_spectrum_ms = now;
        spectrum_sent = 0U;
    }
    if (status_requested || (uint32_t)(now - last_status_ms) >= STATUS_PERIOD_MS) {
        uint8_t payload[40] = {0};
        generator_config_t tone;
        generator_get_config(&tone);
        payload[0] = (uint8_t)status.mode;
        payload[1] = (uint8_t)audio_is_running();
        write_u16(payload + 2, (uint16_t)((status.flags & ~1U) | 0x0100U | (tone.enabled ? 0x0200U : 0U)));
        write_u32(payload + 4, AUDIO_SAMPLE_RATE);
        write_u16(payload + 8, DSP_FFT_LENGTH);
        write_u16(payload + 10, (uint16_t)preview_skips); /* 预览调度跳过计数低 16 位。 */
        write_u32(payload + 12, status.completed_blocks);
        write_u32(payload + 16, status.lost_blocks);
        write_u32(payload + 20, status.dac_errors);
        write_u32(payload + 24, parser.bad_frames + rx_overflow);
        write_u32(payload + 28, now);
        write_u32(payload + 32, tone.frequency_hz);
        write_u16(payload + 36, (uint16_t)tone.peak_centidb);
        if (send_frame(MSG_STATUS, outgoing_seq, status.sample_index,
                       payload, sizeof(payload))) {
            ++outgoing_seq;
            last_status_ms = now;
            status_requested = 0U;
        }
        return;
    }

    if (status.mode == AUDIO_MODE_RTA &&
        (uint32_t)(now - last_level_ms) >= LEVEL_PERIOD_MS) {
        uint8_t payload[32];
        write_f32(payload, status.rms_dbfs);
        write_f32(payload + 4, status.peak_dbfs);
        write_f32(payload + 8, status.main_frequency_hz);
        write_u32(payload + 12, status.clipped_samples);
        write_u32(payload + 16, status.flags);
        write_u32(payload + 20, status.lost_blocks);
        write_u32(payload + 24, status.worst_block_us);
        write_u32(payload + 28, now);
        if (send_frame(MSG_LEVEL, outgoing_seq, status.sample_index,
                       payload, sizeof(payload))) {
            ++outgoing_seq;
            preview_skips += (uint32_t)(now - last_level_ms) / LEVEL_PERIOD_MS - 1U;
            last_level_ms = now;
        }
        return;
    }

    if (status.mode == AUDIO_MODE_RTA &&
        (uint32_t)(now - last_waveform_ms) >= WAVEFORM_PERIOD_MS) {
        uint16_t pairs[AUDIO_WAVEFORM_GROUPS * 2U];
        uint32_t first_sample_index;
        if (audio_get_waveform_preview(pairs, &first_sample_index)) {
            /* 负载前 4 字节为源样本数和组数，随后是去中点后的 i16 min/max。 */
            uint8_t payload[4U + AUDIO_WAVEFORM_GROUPS * 4U];
            write_u16(payload, AUDIO_HALF_SAMPLES);
            write_u16(payload + 2, AUDIO_WAVEFORM_GROUPS);
            for (uint32_t i = 0; i < AUDIO_WAVEFORM_GROUPS * 2U; ++i) {
                write_u16(payload + 4U + i * 2U,
                          (uint16_t)((int32_t)pairs[i] - 2048));
            }
            if (send_frame(MSG_WAVEFORM, outgoing_seq, first_sample_index,
                           payload, sizeof(payload))) {
                ++outgoing_seq;
                preview_skips += (uint32_t)(now - last_waveform_ms) / WAVEFORM_PERIOD_MS - 1U;
                last_waveform_ms = now;
            }
        }
        return;
    }
    if (status.mode == AUDIO_MODE_RTA && (uint32_t)(now - last_spectrum_ms) >= 100U) {
        int16_t levels[DSP_PREVIEW_POINTS];
        uint32_t index;
        if (dsp_get_spectrum(levels, &index) && (!spectrum_sent || index != last_spectrum_index)) {
            uint8_t payload[520];
            write_u32(payload, AUDIO_SAMPLE_RATE);
            write_u16(payload + 4, DSP_FFT_LENGTH);
            write_u16(payload + 6, DSP_PREVIEW_POINTS);
            for (uint32_t i = 0; i < DSP_PREVIEW_POINTS; ++i) write_u16(payload + 8 + i * 2U, (uint16_t)levels[i]);
            if (send_frame(MSG_SPECTRUM_PREVIEW, outgoing_seq, index, payload, sizeof(payload))) {
                ++outgoing_seq;
                preview_skips += (uint32_t)(now - last_spectrum_ms) / 100U - 1U;
                last_spectrum_ms = now;
                last_spectrum_index = index;
                spectrum_sent = 1U;
            }
        }
    }
}
