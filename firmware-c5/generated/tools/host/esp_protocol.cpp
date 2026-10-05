// 直接编译项目内ESP协议解析器，检查真实C5输出，避免两端各自测试各自通过。
// 默认用已核对的v0.3契约快照，确保仅检出C5提交也可复现；可指定实际ESP头。
#ifndef C5_ESP_HEADER
#define C5_ESP_HEADER "esp_v03_contract.h"
#endif
#include C5_ESP_HEADER
extern "C" __declspec(dllexport) int host_esp_valid(const uint8_t* bytes,unsigned size) {
    if(size<14 || size!=Protocol::u16(bytes+6)+14U) return 0;
    if(Protocol::u16(bytes+size-2)!=Protocol::crc(bytes+2,size-4)) return 0;
    return Protocol::validPayload(bytes);
}
