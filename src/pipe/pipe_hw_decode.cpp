
#include <stdio.h>
#include <thread>
#include "common_utils.h"
#include "image_utils.h"
#include "pipe_hw_decode.h"
using namespace std;
static enum AVPixelFormat get_hw_format(AVCodecContext *decoder_ctx,const enum AVPixelFormat *pix_fmts){
    const enum AVPixelFormat *p;
    for (p = pix_fmts; *p != -1; p++) {
        printf("get_hw_format:%s\n",av_get_pix_fmt_name(decoder_ctx->pix_fmt));
        if (*p == decoder_ctx->pix_fmt){
            return *p;
        }
    }
    printf( "Failed to get HW surface format.\n");
    return AV_PIX_FMT_NONE;
}

int PipeHwDecode::DecodeWrite(AVCodecContext *avctx, AVPacket *packet){
    AVFrame *frame = NULL, *sw_frame = NULL;
    AVFrame *tmp_frame = NULL;
    uint8_t *buffer = NULL;
    int size;
    int ret = 0;
    int bgr_size = 0;
    void* bgr_data;
    ret = avcodec_send_packet(avctx, packet);
    if (ret < 0) {
        fprintf(stderr, "Error during decoding\n");
        return ret;
    }
    while (1) {
        if (!(frame = av_frame_alloc()) || !(sw_frame = av_frame_alloc())) {
            fprintf(stderr, "Can not alloc frame\n");
            ret = AVERROR(ENOMEM);
            goto fail;
        }

        ret = avcodec_receive_frame(avctx, frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            av_frame_free(&frame);
            av_frame_free(&sw_frame);
            return 0;
        } else if (ret < 0) {
            fprintf(stderr, "Error while decoding\n");
            goto fail;
        }

        if (frame->format == hw_pix_fmt) {
            /* retrieve data from GPU to CPU */
            if ((ret = av_hwframe_transfer_data(sw_frame, frame, 0)) < 0) {
                fprintf(stderr, "Error transferring the data to system memory\n");
                goto fail;
            }
            tmp_frame = sw_frame;
        } else
            tmp_frame = frame;

        size = av_image_get_buffer_size((enum AVPixelFormat)tmp_frame->format, tmp_frame->width,
                                        tmp_frame->height, 1);
        buffer = (uint8_t*)av_malloc(size);
        if (!buffer) {
            fprintf(stderr, "Can not alloc buffer\n");
            ret = AVERROR(ENOMEM);
            goto fail;
        }
        ret = av_image_copy_to_buffer(buffer, size,
                                      (const uint8_t * const *)tmp_frame->data,
                                      (const int *)tmp_frame->linesize, (enum AVPixelFormat)tmp_frame->format,
                                      tmp_frame->width, tmp_frame->height, 1);
        if (ret < 0) {
            fprintf(stderr, "Can not copy image to buffer\n");
            goto fail;
        }
        FrameNv12Bgr((void*)buffer,tmp_frame->height,tmp_frame->width,&bgr_data,&bgr_size);
        frame_msg_t frame_data;
        frame_data.camera_id = getStreamId();
        frame_data.data = bgr_data;
        frame_data.size = bgr_size;
        frame_data.format = E_FMT_BGR24;
        frame_data.width =tmp_frame->width;
        frame_data.height =tmp_frame->height;
        frame_data.frame_id = ++frame_id;
        pipe_cb->output_frame_cb(&frame_data,user_point);
    fail:
        av_frame_free(&frame);
        av_frame_free(&sw_frame);
        av_freep(&buffer);
        if (ret < 0)
            return ret;
    }
}

int PipeHwDecode::InitHwDecode(const char *input_filename){
    int ret;
    AVStream *video = NULL;
    AVCodec *decoder = NULL;
    enum AVHWDeviceType type;
    int i;    
#if _WIN32
    type = av_hwdevice_find_type_by_name("d3d11va");
#else
    type = av_hwdevice_find_type_by_name("h264_decoder");
#endif    
    if (type == AV_HWDEVICE_TYPE_NONE) {
        fprintf(stderr, "Device type d3d11va is not supported.\n");
        fprintf(stderr, "Available device types:");
        while((type = av_hwdevice_iterate_types(type)) != AV_HWDEVICE_TYPE_NONE)
            fprintf(stderr, " %s", av_hwdevice_get_type_name(type));
        fprintf(stderr, "\n");
        return -1;
    }
    avformat_network_init();
	AVDictionary * opts =NULL;
    av_dict_set(&opts, "rtsp_transport", "tcp", 0);
    av_dict_set(&opts, "stimeout", "2000000", 0);
	av_dict_set(&opts, "rtbufsize", "51200", 0);

    /* open the input file */
    if (avformat_open_input(&input_ctx, input_filename, NULL,  &opts) != 0) {
        fprintf(stderr, "Cannot open input file '%s'\n", input_filename);
        return -1;
    }

    if (avformat_find_stream_info(input_ctx, NULL) < 0) {
        fprintf(stderr, "Cannot find input stream information.\n");
        return -1;
    }
    for(int i=0; i < input_ctx->nb_streams; i++){
        if (input_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO){
            AVStream *in_video_stream =input_ctx->streams[i];
            printf("in_video_stream %1.0f/fps\n",av_q2d(in_video_stream->avg_frame_rate));
            setFps((int)av_q2d(in_video_stream->avg_frame_rate));
            // printf("in_video_stream->codecpar->codec_id=%d\n",in_video_stream->codecpar->codec_id);
            // printf(" in_video_stream->time_base.num =%d\n", in_video_stream->time_base.num);
            // printf(" in_video_stream->time_base.den =%d\n", in_video_stream->time_base.den);
            // printf(" in_video_stream->codec->width =%d,height=%d\n", in_video_stream->codecpar->width, in_video_stream->codecpar->height);
            pipe_cb->start_sync_cb(getStreamId(),in_video_stream->codecpar->width, in_video_stream->codecpar->height,user_point);
        }
    }
    /* find the video stream information */
    ret = av_find_best_stream(input_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, &decoder, 0);
    if (ret < 0) {
        fprintf(stderr, "Cannot find a video stream in the input file\n");
        return -1;
    }
    video_stream = ret;

    for (i = 0;; i++) {
        const AVCodecHWConfig *config = avcodec_get_hw_config(decoder, i);
        if (!config) {
            fprintf(stderr, "Decoder %s does not support device type %s.\n",
                    decoder->name, av_hwdevice_get_type_name(type));
            return -1;
        }
        if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX &&
            config->device_type == type) {
            hw_pix_fmt = config->pix_fmt;
            break;
        }
    }

    if (!(decoder_ctx = avcodec_alloc_context3(decoder)))
        return AVERROR(ENOMEM);

    video = input_ctx->streams[video_stream];
    if (avcodec_parameters_to_context(decoder_ctx, video->codecpar) < 0){
        printf("avcodec_parameters_to_context failed\n");
        return -1;
    }
       
    decoder_ctx->pix_fmt =hw_pix_fmt;
    decoder_ctx->get_format  = get_hw_format;
    if(getDecodeGpuId()==0){
        if (av_hwdevice_ctx_create(&hw_device_ctx, type,NULL, NULL, 0) < 0) {
            fprintf(stderr, "Failed to create specified HW device.\n");
            return -1;
        }
    }else{
        char devices[12]={0};
        snprintf(devices,sizeof(devices),"%d",getDecodeGpuId());
        if (av_hwdevice_ctx_create(&hw_device_ctx, type,devices,NULL , 0) < 0) {
            fprintf(stderr, "Failed to create specified HW device.\n");
            return -1;
        } 
    }

    decoder_ctx->hw_device_ctx = av_buffer_ref(hw_device_ctx);

    if ((ret = avcodec_open2(decoder_ctx, decoder, NULL)) < 0) {
        fprintf(stderr, "Failed to open codec for stream #%u\n", video_stream);
        return -1;
    }
    return 0;
}
void PipeHwDecode::seek(){
    double sec = (frame_id+seek_ofst)/getFps();
    // int64_t timestamp = sec * AV_TIME_BASE;
    int64_t pts = sec / av_q2d(input_ctx->streams[video_stream]->time_base);
     printf("seek seek_ofst=%lld,sec=%f,pts=%lld\n",seek_ofst,sec,pts);
    av_seek_frame(input_ctx,video_stream,pts,AVSEEK_FLAG_BACKWARD);
    if(seek_pause){
        run_status=EIsPause;
    }else{
        run_status=EIsStart;
    }   
    seek_ofst=0; 
    seek_pause=false;
}
static void _LoopDecode(PipeHwDecode *context){
    int ret=0;
    AVPacket packet;
    while (1) {
        if(context->run_status==EIsPause){
            sleepMs(100);
            continue;
        } else if(context->run_status==EIsWaitStop){
            printf("user auto stop\n");
            break;
        }else if(context->run_status==EIsSeek){
            context->seek();
        }else if(context->run_status==EIsForWard){
            av_seek_frame(context->input_ctx,-1,context->seek_ofst,AVSEEK_FLAG_FRAME);
        }        
        if ((ret = av_read_frame(context->input_ctx, &packet)) < 0){
            printf("av_read_frame end\n");
            break;
        }        
        if (context->video_stream == packet.stream_index){
            if(context->pipe_cb->output_raw_frame){
                frame_msg_t msg ;
                msg.data = packet.data;
                msg.size = packet.size;
                msg.format = E_FMT_H264;
                context->pipe_cb->output_raw_frame(&msg,context->user_point);
            }
            printf("packet.pts=%lld\n",packet.pts);
            ret = context->DecodeWrite(context->decoder_ctx, &packet);
        }
        av_packet_unref(&packet);
        sleepMs(context->getDelayTime());
    }
    /* flush the decoder */
    packet.data = NULL;
    packet.size = 0;
    ret = context->DecodeWrite(context->decoder_ctx, &packet);
    av_packet_unref(&packet);

    avcodec_free_context(&context->decoder_ctx);
    avformat_close_input(&context->input_ctx);
    av_buffer_unref(&context->hw_device_ctx);
    context->run_status =EIsStop;
    context->pipe_cb->exit_sync_cb(context->getStreamId(), context->user_point);
}

static void LoopDecode(void *arg){
    PipeHwDecode *context = (PipeHwDecode*)arg;
    _LoopDecode(context);
}

void PipeHwDecode::start(){
    if(run_status!=EIsStop){
        printf("loopStream is runing \n");
        return ;
    }
    run_status=EIsStart;
    if(InitHwDecode(filename.c_str())<0){
        printf("start failed ,init hw decode failed \n");
        run_status=EIsStop;
        return ;
    }
    thread t1 = thread(LoopDecode,this);
    t1.detach();
}
void PipeHwDecode::stop(){
    printf("PipeHwDecode set stop \n");
    run_status=EIsWaitStop;
}
/*
* 暂停解码 override
*/
void PipeHwDecode::pause(){
    if( run_status!=EIsStart){
        return ;
    }
    run_status=EIsPause;
}
/*
* 恢复解码 override
*/
void PipeHwDecode::resume(){
    if( run_status!=EIsPause){
        return ;
    }
    run_status=EIsStart;
}
/*
* 快进(只支持本地mp4文件) override
*/
int PipeHwDecode::seek( int64_t ofst, bool is_pause){
    if(getStreamType()!=ELocalStream){
        printf("this stream not supported seek:%d\n",getStreamType());
        return -1;
    }
    if(run_status!=EIsStart){
        return -1;
    }
    seek_ofst = ofst;
    seek_pause = is_pause;
    run_status =EIsSeek;
    return 0;
}
/*
* 向前跳多少帧(只支持本地mp4文件) override
*/
int PipeHwDecode::forward(uint64_t frame_num){
    if(getStreamType()!=ELocalStream){
         printf("this stream not supported forward:%d\n",getStreamType());
        return -1;
    }
    if(run_status!=EIsStart){
        return -1;
    }   
    run_status=EIsForWard; 
    
    return 0;
}

PipeHwDecode::PipeHwDecode(std::string filename,PipeSyncCallback *mpipe,void *user_point){
    this->pipe_cb = mpipe;
    run_status=EIsStop;
    this->filename = filename;
    this->user_point = user_point;
}

PipeHwDecode::~PipeHwDecode(){
}