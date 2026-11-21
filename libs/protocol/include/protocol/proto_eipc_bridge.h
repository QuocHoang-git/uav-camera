#ifndef __PROTO_EIPC_BRIDGE_H
#define __PROTO_EIPC_BRIDGE_H

#include "protocol/proto_types.h"
#include "protocol/proto_video_frame.h"
#include "eipc/eipc.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/*                             Decoded Packet View                            */
/* ========================================================================== */

/**
 * @brief Zero-copy parsed view of a received EIPC message
 */
typedef struct {
    proto_header_t      header;         /**< Parsed 8-byte application header */
    const uint8_t      *payload;        /**< Pointer into EIPC message buffer (after header) */
    size_t              payload_len;    /**< Length of payload data */
    const int          *fds;            /**< Array of received file descriptors (if any) */
    int                 num_fds;        /**< Number of received file descriptors */
} proto_packet_t;

/* ========================================================================== */
/*                          Serialization & Parsing                           */
/* ========================================================================== */

/**
 * @brief Pack an application header and optional payload into an eipc_message_t
 *
 * @param[out] out_msg     Target EIPC message to populate
 * @param[in]  buffer      Memory buffer to store header + payload (must be >= 8 + payload_len)
 * @param[in]  buffer_size Total capacity of buffer
 * @param[in]  hdr         Application header (8 bytes)
 * @param[in]  payload     Optional payload pointer (can be NULL if payload_len == 0)
 * @param[in]  payload_len Length of payload in bytes
 * @param[in]  fds         Optional array of file descriptors to pass (can be NULL)
 * @param[in]  num_fds     Number of FDs in array (0 if none)
 * @return proto_status_t  PROTO_OK on success, error code otherwise
 */
proto_status_t proto_pack_message(eipc_message_t *out_msg,
                                  uint8_t *buffer,
                                  size_t buffer_size,
                                  const proto_header_t *hdr,
                                  const void *payload,
                                  size_t payload_len,
                                  int *fds,
                                  int num_fds);

/**
 * @brief Parse and validate a received eipc_message_t into a proto_packet_t view
 *
 * @param[in]  in_msg      Raw incoming EIPC message
 * @param[out] out_pkt     Decoded packet view
 * @return proto_status_t  PROTO_OK if packet is valid, error code otherwise
 */
proto_status_t proto_unpack_packet(const eipc_message_t *in_msg,
                                   proto_packet_t *out_pkt);

/* ========================================================================== */
/*                         High-Level Sender Helpers                          */
/* ========================================================================== */

/**
 * @brief Send a Command / Request to a target peer
 *
 * @param ctx          EIPC context
 * @param peer_id      EIPC peer identifier
 * @param src          Source service ID
 * @param dst          Destination service ID
 * @param msg_id       Command ID
 * @param trans_id     Transaction ID for response correlation
 * @param payload      Optional command payload
 * @param payload_len  Payload length
 * @return proto_status_t
 */
proto_status_t proto_send_cmd(eipc_context_t *ctx,
                              int peer_id,
                              proto_service_id_t src,
                              proto_service_id_t dst,
                              uint16_t msg_id,
                              uint16_t trans_id,
                              const void *payload,
                              size_t payload_len);

/**
 * @brief Send a Response to a previously received Command
 *
 * @param ctx          EIPC context
 * @param peer_id      EIPC peer identifier
 * @param src          Source service ID
 * @param dst          Destination service ID
 * @param msg_id       Response ID matching the command
 * @param trans_id     Transaction ID matching the request
 * @param payload      Response payload
 * @param payload_len  Payload length
 * @return proto_status_t
 */
proto_status_t proto_send_resp(eipc_context_t *ctx,
                               int peer_id,
                               proto_service_id_t src,
                               proto_service_id_t dst,
                               uint16_t msg_id,
                               uint16_t trans_id,
                               const void *payload,
                               size_t payload_len);

/**
 * @brief Send a simple status-only Response
 *
 * Automatically packs proto_resp_status_t as payload.
 */
proto_status_t proto_send_resp_status(eipc_context_t *ctx,
                                      int peer_id,
                                      proto_service_id_t src,
                                      proto_service_id_t dst,
                                      uint16_t msg_id,
                                      uint16_t trans_id,
                                      proto_status_t status);

/**
 * @brief Publish an asynchronous Event
 *
 * @param ctx          EIPC context
 * @param peer_id      EIPC peer identifier (or broker for broadcast)
 * @param src          Source service ID
 * @param dst          Destination service ID or PROTO_SVC_BROADCAST
 * @param msg_id       Event ID
 * @param payload      Event payload
 * @param payload_len  Payload length
 * @return proto_status_t
 */
proto_status_t proto_send_event(eipc_context_t *ctx,
                                int peer_id,
                                proto_service_id_t src,
                                proto_service_id_t dst,
                                uint16_t msg_id,
                                const void *payload,
                                size_t payload_len);

/**
 * @brief Send a Zero-Copy Video Frame with DMA-BUF file descriptor
 *
 * Transfers proto_video_frame_meta_t as payload and dma_fd via SCM_RIGHTS.
 *
 * @param ctx          EIPC context
 * @param peer_id      Target peer ID (e.g. streaming or AI service)
 * @param src          Source service ID (usually PROTO_SVC_CAMERA)
 * @param dst          Destination service ID
 * @param event_id     PROTO_EVENT_FRAME_RAW_AVAILABLE or PROTO_EVENT_FRAME_ENCODED_AVAILABLE
 * @param meta         Video frame metadata
 * @param dma_fd       DMA-BUF Linux file descriptor
 * @return proto_status_t
 */
proto_status_t proto_send_video_frame(eipc_context_t *ctx,
                                      int peer_id,
                                      proto_service_id_t src,
                                      proto_service_id_t dst,
                                      uint16_t event_id,
                                      const proto_video_frame_meta_t *meta,
                                      int dma_fd);

#ifdef __cplusplus
}
#endif

#endif /* __PROTO_EIPC_BRIDGE_H */
