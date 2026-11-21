#ifndef __EIPC_FRAME_H
#define __EIPC_FRAME_H

#include "eipc/types.h"
#include <stdbool.h>
#include <string.h>

enum {
    EIPC_FLAG_NONE            = 0x0000,
    EIPC_FLAG_HAS_FD_MASK     = 0x0001,
    EIPC_FLAG_NEED_ACK_MASK   = 0x0002,
    EIPC_FLAG_URGENT_MASK     = 0x0004,
    EIPC_FLAG_ENABLE_CRC_MASK     = 0x0008,
};
typedef uint32_t eipc_frame_flag_t;

#pragma pack(push, 1)
typedef struct {
    uint8_t magic[2];
    uint8_t version;
    eipc_frame_type_t type;
    uint8_t _reserved_1;

    uint16_t flags;
    uint16_t crc; //optional

    uint32_t sequence;
    uint32_t payload_size;
} eipc_frame_header_t;
#pragma pack(pop)

#define EIPC_FRAME_HEADER_SIZE (sizeof(eipc_frame_header_t))
#define EIPC_FRAME_MAX_SIZE (EIPC_MAX_PAYLOAD_SIZE + EIPC_FRAME_HEADER_SIZE)

void eipc_frame_header_init(eipc_frame_header_t* header);
bool eipc_frame_is_magic_valid(const eipc_frame_header_t* header);

void eipc_frame_header_add_crc(eipc_frame_header_t* header, const uint8_t* payload);
bool eipc_frame_header_check_crc(const eipc_frame_header_t* header, const uint8_t* payload);

bool eipc_frame_is_valid(const eipc_frame_header_t* header, const uint8_t* payload);

static inline bool eipc_frame_flags_has_fd(const eipc_frame_header_t* header) {
    return (header != NULL) && ((header->flags & EIPC_FLAG_HAS_FD_MASK) != 0);
}

static inline bool eipc_frame_flags_need_ack(const eipc_frame_header_t* header) {
    return (header != NULL) && ((header->flags & EIPC_FLAG_NEED_ACK_MASK) != 0);
}

static inline bool eipc_frame_flags_is_urgent(const eipc_frame_header_t* header) {
    return (header != NULL) && ((header->flags & EIPC_FLAG_URGENT_MASK) != 0);
}

static inline bool eipc_frame_flags_has_crc(const eipc_frame_header_t* header) {
    return (header != NULL) && ((header->flags & EIPC_FLAG_ENABLE_CRC_MASK) != 0);
}

static inline bool eipc_frame_flags_check(const eipc_frame_header_t* header, uint16_t mask) {
    return (header != NULL) && ((header->flags & mask) == mask);
}

static inline void eipc_frame_flags_set(eipc_frame_header_t* header, uint16_t mask) {
    if (header != NULL) {
        header->flags |= mask;
    }
}

static inline void eipc_frame_flags_clear(eipc_frame_header_t* header, uint16_t mask) {
    if (header != NULL) {
        header->flags &= ~mask;
    }
}

#endif // __EIPC_FRAME_H
