#include "protocol/proto_eipc_bridge.h"
#include <string.h>

/* ========================================================================== */
/*                             Internal Helpers                               */
/* ========================================================================== */

static proto_status_t eipc_to_proto_status(eipc_status_t st) {
    switch (st) {
        case EIPC_OK:                 return PROTO_OK;
        case EIPC_ERR_INVAL:          return PROTO_ERR_INVALID_ARG;
        case EIPC_ERR_NOMEM:          return PROTO_ERR_NO_MEM;
        case EIPC_ERR_BUSY:           return PROTO_ERR_BUSY;
        case EIPC_ERR_TIMEOUT:        return PROTO_ERR_TIMEOUT;
        case EIPC_ERR_DISCONNECTED:   return PROTO_ERR_SERVICE_DOWN;
        case EIPC_ERR_IO:
        case EIPC_ERR_IO_BIND:
        case EIPC_ERR_IO_LISTEN:
        case EIPC_ERR_IO_CONNECT:     return PROTO_ERR_IO;
        case EIPC_ERR_NOT_PERMITTED:  return PROTO_ERR_NOT_PERMITTED;
        default:                      return PROTO_ERR_GENERIC;
    }
}

static eipc_frame_type_t proto_to_eipc_frame_type(proto_msg_type_t type) {
    switch (type) {
        case PROTO_MSG_TYPE_CMD:      return EIPC_FRAME_TYPE_REQUEST;
        case PROTO_MSG_TYPE_RESP:     return EIPC_FRAME_TYPE_RESPONSE;
        case PROTO_MSG_TYPE_EVENT:    return EIPC_FRAME_TYPE_DATA;
        default:                      return EIPC_FRAME_TYPE_DATA;
    }
}

/* ========================================================================== */
/*                          Serialization & Parsing                           */
/* ========================================================================== */

proto_status_t proto_pack_message(eipc_message_t *out_msg,
                                  uint8_t *buffer,
                                  size_t buffer_size,
                                  const proto_header_t *hdr,
                                  const void *payload,
                                  size_t payload_len,
                                  int *fds,
                                  int num_fds) {
    if (!out_msg || !buffer || !hdr) {
        return PROTO_ERR_INVALID_ARG;
    }

    if (payload_len > PROTO_MAX_PAYLOAD_SIZE) {
        return PROTO_ERR_BAD_PAYLOAD;
    }

    size_t total_size = sizeof(proto_header_t) + payload_len;
    if (buffer_size < total_size) {
        return PROTO_ERR_NO_MEM;
    }

    if (payload_len > 0 && !payload) {
        return PROTO_ERR_INVALID_ARG;
    }

    /* 1. Copy 8-byte application header */
    memcpy(buffer, hdr, sizeof(proto_header_t));

    /* 2. Copy payload immediately after header */
    if (payload_len > 0) {
        memcpy(buffer + sizeof(proto_header_t), payload, payload_len);
    }

    /* 3. Populate EIPC message struct */
    eipc_message_init(out_msg);
    eipc_message_set_data(out_msg, buffer, (uint32_t)total_size);
    eipc_message_set_type(out_msg, proto_to_eipc_frame_type((proto_msg_type_t)hdr->msg_type));

    if (fds && num_fds > 0) {
        eipc_message_set_fds(out_msg, fds, num_fds);
    }

    return PROTO_OK;
}

proto_status_t proto_unpack_packet(const eipc_message_t *in_msg,
                                   proto_packet_t *out_pkt) {
    if (!in_msg || !out_pkt) {
        return PROTO_ERR_INVALID_ARG;
    }

    if (!in_msg->data || in_msg->size < sizeof(proto_header_t)) {
        return PROTO_ERR_BAD_PAYLOAD;
    }

    /* 1. Extract and validate header */
    memcpy(&out_pkt->header, in_msg->data, sizeof(proto_header_t));
    if (!proto_header_is_valid(&out_pkt->header)) {
        return PROTO_ERR_BAD_PAYLOAD;
    }

    /* 2. Extract zero-copy payload view */
    out_pkt->payload_len = in_msg->size - sizeof(proto_header_t);
    if (out_pkt->payload_len > 0) {
        out_pkt->payload = in_msg->data + sizeof(proto_header_t);
    } else {
        out_pkt->payload = NULL;
    }

    /* 3. Extract attached file descriptors */
    out_pkt->fds = in_msg->fds;
    out_pkt->num_fds = in_msg->num_fds;

    return PROTO_OK;
}

/* ========================================================================== */
/*                         High-Level Sender Helpers                          */
/* ========================================================================== */

proto_status_t proto_send_cmd(eipc_context_t *ctx,
                              int peer_id,
                              proto_service_id_t src,
                              proto_service_id_t dst,
                              uint16_t msg_id,
                              uint16_t trans_id,
                              const void *payload,
                              size_t payload_len) {
    if (!ctx) return PROTO_ERR_INVALID_ARG;

    uint8_t buffer[PROTO_MAX_PACKET_SIZE];
    proto_header_t hdr;
    proto_header_init(&hdr, PROTO_MSG_TYPE_CMD, src, dst, msg_id, trans_id);

    eipc_message_t msg;
    proto_status_t ret = proto_pack_message(&msg, buffer, sizeof(buffer), &hdr, payload, payload_len, NULL, 0);
    if (ret != PROTO_OK) return ret;

    eipc_status_t e_ret = eipc_send_message(ctx, peer_id, &msg);
    return eipc_to_proto_status(e_ret);
}

proto_status_t proto_send_resp(eipc_context_t *ctx,
                               int peer_id,
                               proto_service_id_t src,
                               proto_service_id_t dst,
                               uint16_t msg_id,
                               uint16_t trans_id,
                               const void *payload,
                               size_t payload_len) {
    if (!ctx) return PROTO_ERR_INVALID_ARG;

    uint8_t buffer[PROTO_MAX_PACKET_SIZE];
    proto_header_t hdr;
    proto_header_init(&hdr, PROTO_MSG_TYPE_RESP, src, dst, msg_id, trans_id);

    eipc_message_t msg;
    proto_status_t ret = proto_pack_message(&msg, buffer, sizeof(buffer), &hdr, payload, payload_len, NULL, 0);
    if (ret != PROTO_OK) return ret;

    eipc_status_t e_ret = eipc_send_message(ctx, peer_id, &msg);
    return eipc_to_proto_status(e_ret);
}

proto_status_t proto_send_resp_status(eipc_context_t *ctx,
                                      int peer_id,
                                      proto_service_id_t src,
                                      proto_service_id_t dst,
                                      uint16_t msg_id,
                                      uint16_t trans_id,
                                      proto_status_t status) {
    proto_resp_status_t resp_payload;
    resp_payload.status = (int32_t)status;
    return proto_send_resp(ctx, peer_id, src, dst, msg_id, trans_id, &resp_payload, sizeof(resp_payload));
}

proto_status_t proto_send_event(eipc_context_t *ctx,
                                int peer_id,
                                proto_service_id_t src,
                                proto_service_id_t dst,
                                uint16_t msg_id,
                                const void *payload,
                                size_t payload_len) {
    if (!ctx) return PROTO_ERR_INVALID_ARG;

    uint8_t buffer[PROTO_MAX_PACKET_SIZE];
    proto_header_t hdr;
    proto_header_init(&hdr, PROTO_MSG_TYPE_EVENT, src, dst, msg_id, 0);

    eipc_message_t msg;
    proto_status_t ret = proto_pack_message(&msg, buffer, sizeof(buffer), &hdr, payload, payload_len, NULL, 0);
    if (ret != PROTO_OK) return ret;

    eipc_status_t e_ret = eipc_send_message(ctx, peer_id, &msg);
    return eipc_to_proto_status(e_ret);
}

proto_status_t proto_send_video_frame(eipc_context_t *ctx,
                                      int peer_id,
                                      proto_service_id_t src,
                                      proto_service_id_t dst,
                                      uint16_t event_id,
                                      const proto_video_frame_meta_t *meta,
                                      int dma_fd) {
    if (!ctx || !meta) return PROTO_ERR_INVALID_ARG;

    uint8_t buffer[PROTO_MAX_PACKET_SIZE];
    proto_header_t hdr;
    proto_header_init(&hdr, PROTO_MSG_TYPE_EVENT, src, dst, event_id, (uint16_t)(meta->sequence & 0xFFFF));

    int fds[1];
    int num_fds = 0;
    if (dma_fd >= 0) {
        fds[0] = dma_fd;
        num_fds = 1;
    }

    eipc_message_t msg;
    proto_status_t ret = proto_pack_message(&msg, buffer, sizeof(buffer), &hdr, meta, sizeof(proto_video_frame_meta_t), fds, num_fds);
    if (ret != PROTO_OK) return ret;

    eipc_status_t e_ret = eipc_send_message(ctx, peer_id, &msg);
    return eipc_to_proto_status(e_ret);
}
