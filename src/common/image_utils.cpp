#include <stdio.h>
extern "C"{
    #include <libavcodec/avcodec.h>
    #include <libavdevice/avdevice.h>
    #include <libavformat/avformat.h>
    #include <libavutil/pixdesc.h>
    #include <libavutil/hwcontext.h>
    #include <libavutil/opt.h>
    #include <libavutil/avassert.h>
    #include <libavutil/imgutils.h>
    #include <libswscale/swscale.h>
}

static int FrameConverter(void*src_data,int height,int width,void **dest_data,int *dest_size,enum AVPixelFormat src_pix_fmt,enum AVPixelFormat dest_pix_fmt){
    AVFrame* dst_frame = av_frame_alloc();
    AVFrame* frame = av_frame_alloc();
    void* yuv_data = NULL;
    int ret = 0;
    int dst_frame_size = av_image_get_buffer_size(dest_pix_fmt, width, height, 1);
    unsigned char* outBuff = (unsigned char*)calloc(1,dst_frame_size);
    struct SwsContext* pSwsCtx = sws_getContext(width, height, src_pix_fmt, width, height, dest_pix_fmt, SWS_BICUBIC, NULL, NULL, NULL);
    av_image_fill_arrays(frame->data, frame->linesize, (const uint8_t*)src_data, src_pix_fmt, width, height, 1);
    av_image_fill_arrays(dst_frame->data, dst_frame->linesize, outBuff, dest_pix_fmt, width, height, 1);
    if (pSwsCtx==NULL){
        printf("pSwsCtx sws_getContext failed\n");
        av_free(dst_frame);
        av_free(frame);
        return -1;
    }
    sws_scale(pSwsCtx, frame->data, frame->linesize, 0, height, dst_frame->data, dst_frame->linesize);
    *dest_data = outBuff;
    *dest_size = dst_frame_size;
    sws_freeContext(pSwsCtx);
    av_free(dst_frame);
    av_free(frame);
    return 0;
}
int FrameToBgr(int src_fmt,void*yuv,int height,int width,void **bgr_data,int *bgr_size) {
    enum AVPixelFormat src_pix_fmt=(enum AVPixelFormat)src_fmt;
    enum AVPixelFormat dest_pix_fmt =AV_PIX_FMT_BGR24;
    return FrameConverter(yuv,height,width, bgr_data,bgr_size,src_pix_fmt,dest_pix_fmt);
}
int FrameNv12Bgr(void*nv12,int height,int width,void **bgr_data,int *bgr_size) {
    enum AVPixelFormat src_pix_fmt=(enum AVPixelFormat)AV_PIX_FMT_NV12;
    enum AVPixelFormat dest_pix_fmt =AV_PIX_FMT_BGR24;
    return FrameConverter(nv12,height,width, bgr_data,bgr_size,src_pix_fmt,dest_pix_fmt);
}
void MJPEGToRGB(unsigned char *data, unsigned int dataSize,int height,int width, void **outBuffer,int *bgr_size){   
    // 1. 将元数据装填到packet
    AVPacket *avPkt = av_packet_alloc();
    avPkt->size = dataSize;
    avPkt->data = data;
    int dst_frame_size = av_image_get_buffer_size(AV_PIX_FMT_BGR24, width, height, 1);
    unsigned char* outBuff = (unsigned char*)calloc(1,dst_frame_size);
    // 2. 创建并配置codecContext
    AVCodec *mjpegCodec = avcodec_find_decoder(AV_CODEC_ID_MJPEG);
    AVCodecContext* codecCtx = avcodec_alloc_context3(mjpegCodec);
    avcodec_get_context_defaults3(codecCtx, mjpegCodec);
    avcodec_open2(codecCtx, mjpegCodec, nullptr);

    // 3. 解码
    auto ret = avcodec_send_packet(codecCtx, avPkt);
    if (ret >=0) {
        AVFrame* YUVFrame = av_frame_alloc();
        ret = avcodec_receive_frame(codecCtx, YUVFrame);
        if (ret >= 0) { 

            // 4.YUV转RGB24
            AVFrame* RGB24Frame = av_frame_alloc();
            struct SwsContext* convertCxt = sws_getContext(
                YUVFrame->width, YUVFrame->height, AV_PIX_FMT_YUV420P,
                YUVFrame->width, YUVFrame->height, AV_PIX_FMT_BGR24,
                SWS_POINT, NULL, NULL, NULL
            );

            // outBuffer将会分配给RGB24Frame->data,AV_PIX_FMT_RGB24格式只分配到RGB24Frame->data[0]
            av_image_fill_arrays(
                RGB24Frame->data, RGB24Frame->linesize,(unsigned char *) outBuff,  
                AV_PIX_FMT_BGR24, YUVFrame->width, YUVFrame->height,
                1
            );
            sws_scale(convertCxt, YUVFrame->data, YUVFrame->linesize, 0, YUVFrame->height, RGB24Frame->data, RGB24Frame->linesize);

            // 5.清除各对象/context -> 释放内存
            // free context and avFrame
            sws_freeContext(convertCxt);
            av_frame_free(&RGB24Frame);
            // RGB24Frame.
        }
        // free context and avFrame
        av_frame_free(&YUVFrame);
    }
    // free context and avFrame
    av_packet_unref(avPkt);
    av_packet_free(&avPkt);
    avcodec_free_context(&codecCtx);
    *outBuffer = outBuff;
    *bgr_size = dst_frame_size;
}