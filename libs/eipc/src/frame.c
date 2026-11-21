#include "frame.h"


/**
 * @brief Hàm băm CRC-16-CCITT (Polynomial 0x1021, Init 0xFFFF)
 */
static uint16_t crc16_ccitt(const uint8_t *data, uint32_t size) {
    uint16_t crc = 0xFFFF;
    if (!data || size == 0) {
        return 0;
    }
    for (uint32_t i = 0; i < size; ++i) {
        crc ^= (uint16_t)data[i] << 8;
        for (int j = 0; j < 8; ++j) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ 0x1021;
            } else {
                crc = crc << 1;
            }
        }
    }
    return crc;
}

void eipc_frame_header_init(eipc_frame_header_t* header){
    if(header == NULL){
        return;
    }

    memset(header, 0, sizeof(eipc_frame_header_t));
    
    header->magic[0] = (uint8_t)((EIPC_MAGIC_NUMBER >> 8) & 0xFF); // 0x45 ('E')
    header->magic[1] = (uint8_t)(EIPC_MAGIC_NUMBER & 0xFF);        // 0x50 ('P')
    
    header->version = EIPC_VERSION_MAJOR;
    
    header->type = EIPC_FRAME_TYPE_DATA;
    header->flags = EIPC_FLAG_NONE;
    header->crc = 0;
    header->sequence = 0;
    header->payload_size = 0;
    header->_reserved_1 = 0;
}

bool eipc_frame_is_magic_valid(const eipc_frame_header_t* header){
    if(header == NULL){
        return false;
    }

    return ((header->magic[0] == (uint8_t)((EIPC_MAGIC_NUMBER >> 8) & 0xFF)) && 
             header->magic[1] == (uint8_t)(EIPC_MAGIC_NUMBER & 0xFF));
}

void eipc_frame_header_add_crc(eipc_frame_header_t* header, const uint8_t* payload) {
    if (header == NULL) {
        return;
    }
    header->crc = crc16_ccitt(payload, header->payload_size);
    eipc_frame_flags_set(header, EIPC_FLAG_ENABLE_CRC_MASK);
}

bool eipc_frame_header_check_crc(const eipc_frame_header_t* header, const uint8_t* payload) {
    if (header == NULL) {
        return false;
    }
    uint16_t calculated_crc = crc16_ccitt(payload, header->payload_size);
    return (header->crc == calculated_crc);
}

bool eipc_frame_is_valid(const eipc_frame_header_t* header, const uint8_t* payload){
    if(header == NULL){
        return false;
    }

    if (header->payload_size > 0 && payload == NULL) {
        return false;
    }

    if (header->payload_size > EIPC_MAX_PAYLOAD_SIZE) {
        return false;
    }

    if(!eipc_frame_is_magic_valid(header)){
        return false;
    }

    if (header->version != EIPC_VERSION_MAJOR) {
        return false;
    }

    if (eipc_frame_flags_has_crc(header)) {
        if (!eipc_frame_header_check_crc(header, payload)) {
            return false;
        }
    }

    return true;
}
