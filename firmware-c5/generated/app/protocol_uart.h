#ifndef ACOUSTICLAB_PROTOCOL_UART_H
#define ACOUSTICLAB_PROTOCOL_UART_H

/* 启动 USART2 单字节中断接收；成功返回 0。 */
int protocol_uart_start(void);
/* 在主循环中解析接收环形缓冲，并按优先级发送应答、状态和电平。 */
void protocol_uart_poll(void);

#endif
