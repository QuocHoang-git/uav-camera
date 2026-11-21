#include "protocol/protocol.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

int main(void) {
    printf("=== Testing libs/protocol ===\n");

    /* 1. Test proto_pack_message & proto_unpack_packet */
    uint8_t buffer[PROTO_MAX_PACKET_SIZE];
    proto_header_t hdr;
    proto_header_init(&hdr, PROTO_MSG_TYPE_CMD, PROTO_SVC_MANAGER, PROTO_SVC_CAMERA, PROTO_CMD_CAM_SET_CONFIG, 0x1234);

    proto_cam_config_t cfg_in = {
        .width = 1920,
        .height = 1080,
        .fps = 30,
        .bitrate_kbps = 4000,
        .codec = PROTO_CODEC_H265,
        .pixel_format = PROTO_PIX_FMT_NV12,
        .gop_size = 30,
        .rc_mode = 0,
    };

    eipc_message_t eipc_msg;
    proto_status_t status = proto_pack_message(&eipc_msg, buffer, sizeof(buffer), &hdr, &cfg_in, sizeof(cfg_in), NULL, 0);
    assert(status == PROTO_OK);
    assert(eipc_msg.size == sizeof(proto_header_t) + sizeof(proto_cam_config_t));
    assert(eipc_msg.type == EIPC_FRAME_TYPE_REQUEST);

    /* Unpack */
    proto_packet_t pkt;
    status = proto_unpack_packet(&eipc_msg, &pkt);
    assert(status == PROTO_OK);
    assert(pkt.header.version == PROTO_VERSION_CURRENT);
    assert(pkt.header.msg_type == PROTO_MSG_TYPE_CMD);
    assert(pkt.header.src_id == PROTO_SVC_MANAGER);
    assert(pkt.header.dst_id == PROTO_SVC_CAMERA);
    assert(pkt.header.msg_id == PROTO_CMD_CAM_SET_CONFIG);
    assert(pkt.header.trans_id == 0x1234);
    assert(pkt.payload_len == sizeof(proto_cam_config_t));

    const proto_cam_config_t *cfg_out = (const proto_cam_config_t*)pkt.payload;
    assert(cfg_out->width == 1920);
    assert(cfg_out->height == 1080);
    assert(cfg_out->codec == PROTO_CODEC_H265);
    printf("[PASS] Camera Config Command pack/unpack\n");

    /* 2. Test Zero-Copy Video Frame metadata with simulated DMA-BUF FD */
    proto_video_frame_meta_t frame_in = {
        .width = 1920,
        .height = 1080,
        .stride_w = 1920,
        .stride_h = 1080,
        .format = PROTO_PIX_FMT_NV12,
        .size_bytes = 1920 * 1080 * 3 / 2,
        .buf_index = 2,
        .sequence = 105,
        .timestamp_us = 123456789ULL,
        .frame_type = PROTO_FRAME_TYPE_RAW,
        .is_keyframe = 1,
        .plane_count = 2,
        .reserved = 0,
    };

    proto_header_t frame_hdr;
    proto_header_init(&frame_hdr, PROTO_MSG_TYPE_EVENT, PROTO_SVC_CAMERA, PROTO_SVC_STREAMING, PROTO_EVENT_FRAME_RAW_AVAILABLE, 105);

    int fake_dma_fd = 42;
    int fds_in[1] = { fake_dma_fd };

    status = proto_pack_message(&eipc_msg, buffer, sizeof(buffer), &frame_hdr, &frame_in, sizeof(frame_in), fds_in, 1);
    assert(status == PROTO_OK);
    assert(eipc_msg.type == EIPC_FRAME_TYPE_DATA);
    assert(eipc_msg.num_fds == 1);
    assert(eipc_msg.fds[0] == 42);

    /* Unpack Video Frame */
    status = proto_unpack_packet(&eipc_msg, &pkt);
    assert(status == PROTO_OK);
    assert(pkt.num_fds == 1);
    assert(pkt.fds[0] == 42);
    const proto_video_frame_meta_t *frame_out = (const proto_video_frame_meta_t*)pkt.payload;
    assert(frame_out->buf_index == 2);
    assert(frame_out->sequence == 105);
    assert(frame_out->size_bytes == 1920 * 1080 * 3 / 2);
    printf("[PASS] Zero-Copy Video Frame with DMA-BUF FD pack/unpack\n");

    /* 3. Test Strings */
    assert(strcmp(proto_status_str(PROTO_OK), "OK") == 0);
    assert(strcmp(proto_service_name(PROTO_SVC_CAMERA), "Camera") == 0);
    assert(strcmp(proto_codec_str(PROTO_CODEC_H265), "H.265") == 0);
    printf("[PASS] String conversion utilities\n");

    printf("\n>>> ALL PROTOCOL TESTS PASSED! <<<\n");
    return 0;
}
