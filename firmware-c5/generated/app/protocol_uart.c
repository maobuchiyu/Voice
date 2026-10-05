#include "protocol_uart.h"

#include "audio.h"
#include "generator.h"
#include "dsp.h"
#include "measurement.h"
#include "calibration.h"
#include "mx_usart2.h"
#include "protocol_codec.h"
#include "stm32_hal.h"
#include <string.h>
#include <math.h>

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
static uint32_t dataset_id, last_power_ms;
static uint16_t export_offset;
static uint8_t exporting, export_finishing, extended_requested;
static uint16_t read_u16(const uint8_t *p) { return (uint16_t)p[0] | ((uint16_t)p[1]<<8); }
static float read_f32(const uint8_t *p) { uint32_t bits=(uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); float value; memcpy(&value,&bits,4); return value; }

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
        else if(exporting || calibration_loading()) result=REPLY_BUSY;
        else {
            measurement_cancel();
            audio_request_mode((audio_mode_t)command->payload[1]);
        }
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
            else if(measurement_active()) result=REPLY_BUSY;
            else generator_request(&config);
        }
    } else if(command->payload[0]==CMD_SET_FFT) {
        if(command->length!=3) result=REPLY_BAD_LENGTH;
        else if(read_u16(command->payload+1)!=2048 && read_u16(command->payload+1)!=4096) result=REPLY_BAD_ARGUMENT;
        else if(exporting || measurement_active() || calibration_loading()) result=REPLY_BUSY;
        else if(dsp_set_length(read_u16(command->payload+1))) result=REPLY_NOT_READY;
    } else if(command->payload[0]==CMD_SNAPSHOT) {
        if(command->length!=1) result=REPLY_BAD_LENGTH;
        else if(exporting || measurement_active() || calibration_loading()) result=REPLY_BUSY;
        else if(dsp_freeze()) result=REPLY_NOT_READY;
        else { exporting=1; export_finishing=0; export_offset=0; if(++dataset_id==0) ++dataset_id; }
    } else if(command->payload[0]==CMD_EXPORT_ABORT) {
        if(command->length!=1) result=REPLY_BAD_LENGTH;
        else { exporting=export_finishing=0; dsp_release(); }
    } else if(command->payload[0]==CMD_MEASURE) {
        if(command->length!=4) result=REPLY_BAD_LENGTH;
        else if(command->payload[1]!=MEASURE_THD && command->payload[1]!=MEASURE_SWEEP) result=REPLY_BAD_ARGUMENT;
        else if(exporting || measurement_active() || calibration_loading()) result=REPLY_BUSY;
        else if(measurement_start(command->payload[1],(int16_t)read_u16(command->payload+2))) result=REPLY_NOT_READY;
        else audio_request_mode(command->payload[1]==MEASURE_THD ? AUDIO_MODE_THD : AUDIO_MODE_SWEEP);
    } else if(command->payload[0]==CMD_CAL_BEGIN) {
        if(command->length!=12) result=REPLY_BAD_LENGTH;
        else if(!audio_is_idle() || exporting || measurement_active()) result=REPLY_BUSY;
        else if(calibration_begin(read_u32(command->payload+1),command->payload[5],read_u32(command->payload+6),read_u16(command->payload+10))) result=REPLY_BAD_ARGUMENT;
    } else if(command->payload[0]==CMD_CAL_CHUNK) {
        if(command->length<4 || command->length!=4U+12U*command->payload[3] || command->payload[3]>10) result=REPLY_BAD_LENGTH;
        else if(!audio_is_idle()) result=REPLY_BUSY;
        else {
            calibration_point_t points[10];
            for(uint32_t i=0;i<command->payload[3];++i) {
                const uint8_t *p=command->payload+4+12*i;
                points[i]=(calibration_point_t){read_f32(p),read_f32(p+4),read_f32(p+8)};
            }
            if(calibration_append(read_u16(command->payload+1),points,command->payload[3])) result=REPLY_BAD_ARGUMENT;
        }
    } else if(command->payload[0]==CMD_CAL_COMMIT || command->payload[0]==CMD_CAL_ABORT) {
        if(command->length!=1) result=REPLY_BAD_LENGTH;
        else if(!audio_is_idle()) result=REPLY_BUSY;
        else if(command->payload[0]==CMD_CAL_ABORT) calibration_abort();
        else if(calibration_commit()) result=REPLY_NOT_READY;
    } else if(command->payload[0]==CMD_INPUT_PATH) {
        if(command->length!=6) result=REPLY_BAD_LENGTH;
        else if(!audio_is_idle() || exporting || calibration_loading() || measurement_active()) result=REPLY_BUSY;
        else if(command->payload[1]!=CAL_LOOP && command->payload[1]!=CAL_MIC) result=REPLY_BAD_ARGUMENT;
        else calibration_path(command->payload[1],read_u32(command->payload+2));
    } else if(command->payload[0]==CMD_SPL_OFFSET) {
        if(command->length!=9) result=REPLY_BAD_LENGTH;
        else if(!audio_is_idle() || calibration_loading()) result=REPLY_BUSY;
        else if(calibration_set_spl(read_f32(command->payload+1),read_u32(command->payload+5))) result=REPLY_BAD_ARGUMENT;
    } else if(command->payload[0]==CMD_EXTENDED_STATUS) {
        if(command->length!=1) result=REPLY_BAD_LENGTH;
        else extended_requested=1;
    } else result = REPLY_UNSUPPORTED;
    queue_reply(command, result);
    /* 请求立即上报；块边界提交后的周期 STATUS 用于确认配置已生效。 */
    if (result == REPLY_OK) status_requested=1;
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
        write_u16(payload + 2, (uint16_t)((status.flags & ~1U) | 0x1D00U | (tone.enabled ? 0x0200U : 0U)));
        write_u32(payload + 4, AUDIO_SAMPLE_RATE);
        write_u16(payload + 8, dsp_get_length());
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

    if(extended_requested) {
        uint8_t payload[28]={1,3}; float unused;
        write_u16(payload+2,0x0300); write_u32(payload+4,0x3FU);
        write_u16(payload+8,dsp_get_length()); payload[10]=(uint8_t)status.mode; payload[11]=calibration_current_path();
        write_u32(payload+12,calibration_gain_id()); write_u32(payload+16,calibration_id());
        payload[20]=(uint8_t)calibration_loading(); payload[21]=(uint8_t)measurement_active();
        payload[22]=(uint8_t)calibration_spl(0,&unused); payload[23]=exporting;
        write_u32(payload+24,now);
        if(send_frame(MSG_MEASUREMENT,outgoing_seq,status.sample_index,payload,sizeof(payload))) { ++outgoing_seq; extended_requested=0; }
        return;
    }
    measurement_result_t measurement;
    if(measurement_get(&measurement)) {
        uint8_t payload[204]={1,measurement.kind};
        write_u16(payload+2,measurement.reason); write_u32(payload+4,measurement.sequence);
        write_u32(payload+8,measurement.samples); write_u32(payload+12,measurement.calibration_id);
        write_u16(payload+16,measurement.point); write_u16(payload+18,measurement.point_count);
        payload[20]=measurement.harmonic_mask; payload[21]=calibration_current_path();
        write_f32(payload+24,fminf(measurement.kind==MEASURE_THD ? 4000 : 20000,fmaxf(measurement.kind==MEASURE_THD ? 100 : 20,measurement.frequency)));
        write_f32(payload+28,measurement.transfer_db); write_f32(payload+32,measurement.transfer_phase);
        write_f32(payload+36,measurement.thd_percent); write_f32(payload+40,measurement.thd_db);
        for(uint32_t i=0;i<5;++i) {
            const harmonic_result_t *h=&measurement.harmonic[i]; uint8_t *p=payload+44+i*32;
            write_f32(p,h->frequency); write_f32(p+4,h->amplitude); write_f32(p+8,h->dbfs);
            write_f32(p+12,h->dbc); write_f32(p+16,h->percent); write_f32(p+20,h->phase);
            write_f32(p+24,h->corrected_dbfs); write_f32(p+28,h->corrected_phase);
        }
        if(send_frame(MSG_MEASUREMENT,outgoing_seq,measurement.end_index,payload,sizeof(payload))) { ++outgoing_seq; measurement_consume(); }
        return;
    }
    if(export_finishing) {
        exporting=export_finishing=0; dsp_release();
        last_waveform_ms=last_spectrum_ms=now; spectrum_sent=0;
    }
    if (status.mode != AUDIO_MODE_IDLE &&
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

    if(exporting) {
        uint8_t payload[494]={0}; int16_t values[236];
        uint16_t count=dsp_snapshot_read(export_offset,values,236);
        if(!count) { exporting=0; dsp_release(); return; }
        write_u32(payload,dataset_id); payload[4]=1; payload[5]=1;
        write_u16(payload+6,dsp_get_length()); write_u32(payload+8,AUDIO_SAMPLE_RATE);
        write_u16(payload+12,(uint16_t)(dsp_get_length()/2+1)); write_u16(payload+14,export_offset);
        write_u16(payload+16,count); write_u32(payload+18,calibration_id());
        for(uint32_t i=0;i<count;++i) write_u16(payload+22+2*i,(uint16_t)values[i]);
        if(send_frame(MSG_DATA_CHUNK,outgoing_seq,dsp_result_index(),payload,(uint16_t)(22+count*2))) {
            ++outgoing_seq; export_offset+=count;
            if(export_offset==dsp_get_length()/2+1) export_finishing=1;
        }
        return;
    }
    if(status.mode==AUDIO_MODE_RTA && (uint32_t)(now-last_power_ms)>=1000U) {
        dsp_power_t power;
        if(dsp_get_power(&power)) {
            uint8_t payload[56]={1,4}; float spl;
            write_f32(payload+4,power.z_dbfs); write_f32(payload+8,power.a_dbfs); write_f32(payload+12,power.c_dbfs);
            write_u16(payload+16,power.octave_valid); write_u16(payload+18,8);
            for(uint32_t i=0;i<8;++i) write_f32(payload+20+4*i,power.octave_dbfs[i]);
            if(calibration_spl(status.rms_dbfs,&spl)) { payload[2]=1; write_f32(payload+52,spl); }
            if(send_frame(MSG_MEASUREMENT,outgoing_seq,power.index,payload,sizeof(payload))) { ++outgoing_seq; last_power_ms=now; }
            return;
        }
    }
    if (status.mode == AUDIO_MODE_RTA && !exporting &&
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
    if (status.mode == AUDIO_MODE_RTA && !exporting && (uint32_t)(now - last_spectrum_ms) >= 100U) {
        int16_t levels[DSP_PREVIEW_POINTS];
        uint32_t index;
        if (dsp_get_spectrum(levels, &index) && (!spectrum_sent || index != last_spectrum_index)) {
            uint8_t payload[520];
            write_u32(payload, AUDIO_SAMPLE_RATE);
            write_u16(payload + 4, dsp_get_length());
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
