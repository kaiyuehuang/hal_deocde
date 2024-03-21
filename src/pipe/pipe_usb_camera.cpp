#include <stdio.h>
#include<thread>
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
#include "common_utils.h"
#include "image_utils.h"
#include "pipe_sync_service.h"
#include "pipe_usb_camera.h"
using namespace std;

void ShowDshowDeviceOption(){
    avdevice_register_all();
    av_log_set_level(AV_LOG_DEBUG);
    AVFormatContext *pFormatCtx = avformat_alloc_context();
    AVDictionary* options = NULL;
    av_dict_set(&options, "list_devices", "true", 0); //0表示不区分大小写
	AVInputFormat* iformat = av_find_input_format("dshow");
	printf("========Device Info=============\n");
	avformat_open_input(&pFormatCtx, "video=dummy", iformat, &options);
    avformat_free_context(pFormatCtx);
}
static void OutputFrame(AVPacket *inPacket,AVFrame *pFrame,int stream_id,int frame_id,PipeSyncCallback *pipecb,void *user_point){
    void *bgr_data;
    int bgr_size =0;
    // printf("format:%d width:%d height:%d size:%d\n",pFrame->format,pFrame->width,pFrame->height,inPacket->size);
    if(pFrame->format==AV_PIX_FMT_YUVJ420P){
        MJPEGToRGB(inPacket->data, pFrame->pkt_size,pFrame->height,pFrame->width,&bgr_data,&bgr_size);
    }else{
        FrameToBgr(pFrame->format,inPacket->data,pFrame->height,pFrame->width,&bgr_data,&bgr_size);
    }
    frame_msg_t frame;
    frame.data =bgr_data;
    frame.size = bgr_size;
    frame.width = pFrame->width;
    frame.height =pFrame->height;
    frame.camera_id =stream_id;
    frame.format = E_FMT_BGR24;
    frame.frame_id = frame_id;
    pipecb->output_frame_cb(&frame,user_point);
}
int PipeUsbCamera::Capture(){
    int ret=-1;
    AVCodecContext  *pCodecCtx;  //解码器上下文
    AVCodec         *pCodec; //解码器
    AVFormatContext *ifmtCtx;//摄像头输入上下文
    AVDictionary *opt = NULL;//配置参数
    AVInputFormat *ifmt=NULL;
    char video_name [128]={0};
    AVPacket *inPacket;
    AVFrame *pFrame ;
    char video_size[24] = { 0 };
    char fps_buf[12] = { 0 };
    // av_log_set_level(AV_LOG_DEBUG);
    avdevice_register_all();
    ifmtCtx= avformat_alloc_context();
#ifdef _WIN32    
    ifmt=av_find_input_format("dshow"); //windows下dshow
#else
    ifmt=av_find_input_format("video4linux2");  //linux
#endif
    if(ifmt==NULL){
        printf("av_find_input_format failed\n");
        goto err0;
    }
    // opt =setAvDict(getWidth(), getHeight(),getFps());
    av_dict_set_int(&opt, "rtbufsize",  2*1024*1024, 0);  //为摄像头图像采集分配的内存，太小会丢失，太大会导致延时
    av_dict_set(&opt,"start_time_realtime",0,0); 
    snprintf(video_size,sizeof(video_size),"%dx%d",getWidth(),getHeight());
    // printf("video_size=%s\n",video_size);
    av_dict_set(&opt,"video_size",video_size,0); //设置分辨率，要看是否支持
    // av_dict_set(&opt,"video_size","1920x1080",0); //设置分辨率，要看是否支持
    snprintf(fps_buf,sizeof(fps_buf),"%d",getFps());
    av_dict_set(&opt,"framerate",fps_buf,0);

    // snprintf(video_name,sizeof(video_name),"%s","video=Intel(R) RealSense(TM) 515 RGB");
    snprintf(video_name,sizeof(video_name),"video=%s",uvc_name.c_str());
    printf("video_name:=%s\n",video_name);
    if(avformat_open_input(&ifmtCtx,video_name,ifmt,&opt)!=0){
        printf("avformat_open_input failed\n");
        goto err0;
    }
    if(avformat_find_stream_info(ifmtCtx,NULL)<0){
        printf("avformat_find_stream_info failed\n");
        goto err0;
    }
    pCodecCtx = avcodec_alloc_context3(NULL);
    if (pCodecCtx == NULL){
        printf("Could not allocate AVCodecContext\n");
        goto err0;
    }
    avcodec_parameters_to_context(pCodecCtx, ifmtCtx->streams[0]->codecpar);
    pCodec=avcodec_find_decoder(pCodecCtx->codec_id);
    if(pCodec==NULL){
        printf("avcodec_find_decoder failed\n");
        goto err0;
    }
    if(avcodec_open2(pCodecCtx, pCodec,NULL)<0){
        printf("avcodec_open2 failed\n");
        goto err0;
    }
    pipe_cb->start_sync_cb(getStreamId(),getWidth(),getHeight(),user_point);
    inPacket = av_packet_alloc(); 
    pFrame = av_frame_alloc();
    while(1){
        if(run_status==EIsPause){
            sleepMs(100);
            continue;
        } 
        if(run_status==EIsWaitStop){
            printf("user auto stop\n");
            break;
        }
        if(av_read_frame(ifmtCtx, inPacket)>=0){
            if(inPacket->stream_index==0){
                ret = avcodec_send_packet(pCodecCtx, inPacket);
                if (ret < 0) {
                    fprintf(stderr, "Error sending a packet for decoding\n");
                    break;
                }
                while (ret >= 0) {
                    ret = avcodec_receive_frame(pCodecCtx, pFrame);
                    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF){
                        break;
                    }else if (ret < 0) {
                        fprintf(stderr, "Error during decoding\n");
                        break;
                    }
                    OutputFrame(inPacket,pFrame, getStreamId(),frame_id++,pipe_cb,user_point);
                }
                av_packet_unref(inPacket);
            }
        }
    }
    av_packet_unref(inPacket);
    avcodec_close(pCodecCtx);
    avformat_close_input(&ifmtCtx);
    avformat_free_context(ifmtCtx);
    ret=0;
 err0: 
    av_dict_free(&opt);
    pipe_cb->exit_sync_cb(getStreamId(),user_point);
    run_status=EIsStop;  
    return ret;   
}
static int LoopCapture(PipeUsbCamera *context){
    return context->Capture();
}
static void LoopCapturePthread(void *arg){
    PipeUsbCamera *context = (PipeUsbCamera*)arg;
    printf("LoopCapturePthread\n");
    LoopCapture(context);
}

PipeUsbCamera::PipeUsbCamera(std::string filename,PipeSyncCallback *mpipe,void *user_point){
    this->uvc_name = filename;
    run_status=EIsStop;
    this->pipe_cb = mpipe;
    this->user_point = user_point;
}

PipeUsbCamera::~PipeUsbCamera(){
}
/*
* 启动解码
*/
void PipeUsbCamera::start(){
    if(run_status!=EIsStop){
        printf("loopStream is runing \n");
        return ;
    }
    run_status=EIsStart;
    thread t1=thread(LoopCapturePthread,this);
    t1.detach();
    printf("PipeUsbCamera start ok \n");
}
/*
* 主动结束解码
*/
void PipeUsbCamera::stop(){
    if(run_status==EIsStop){
        printf("is already stop\n");
        return ;
    }
    run_status=EIsWaitStop;
}
/*
* 暂停解码
*/
void PipeUsbCamera::pause(){
    if( run_status!=EIsStart){
        return ;
    }
    printf("PipeUsbCamera set pause\n");
    run_status = EIsPause;
}
/*
* 恢复解码
*/
void PipeUsbCamera::resume(){
    if(run_status!=EIsPause){
        return ;
    }
    run_status = EIsStart;
}
/*
* 快进(只支持本地mp4文件)
*/
int PipeUsbCamera::seek( int64_t ofst, bool is_pause){
    printf("usb camera not supported seek\n");
    return -1;
}
/*
* 向前跳多少帧(只支持本地mp4文件)
*/
int PipeUsbCamera::forward(uint64_t frame_num){
    printf("usb camera not supported forward\n");
    return -1;
}
