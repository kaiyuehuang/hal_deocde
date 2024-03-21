#include <string>
#include <vector>
#include "gpu_detect.h"
#include "pipe_decode_impl.h"
#include "pipe_sync_service.h"

using namespace std;
static PipeSyncService* pipeSyncService=nullptr;

PipeDecodeImpl::PipeDecodeImpl(vector<TInputStream>input_stream,PipeSyncCallback *mPipecb,void *user_point){
    if(pipeSyncService){
        printf("PipeDecodeImpl already init\n");
        return;
    }
    GpuDetect gpu_info;
    vector<GpuInfos> list;
    gpu_info.GetGpuList(list);
    for (size_t i = 0; i < input_stream.size(); i++){
        printf("gpu[%d]:%s",input_stream[i].gpu_numbers,input_stream[i].stream.c_str());
        if(input_stream[i].gpu_numbers>list.size()){
            input_stream[i].gpu_numbers = list.size()-1;
        }
    }
    
    pipeSyncService = new PipeSyncService(input_stream,mPipecb,user_point);
}
PipeDecodeImpl::PipeDecodeImpl(TInputStream input_stream,PipeSyncCallback *mPipecb,void *user_point){
    if(pipeSyncService){
        printf("PipeDecodeImpl already init\n");
        return;
    }    
    GpuDetect gpu_info;
    vector<GpuInfos> list;
    gpu_info.GetGpuList(list);
    if(input_stream.gpu_numbers>list.size()){
        input_stream.gpu_numbers = list.size()-1;
    }    
    pipeSyncService = new PipeSyncService(input_stream,mPipecb,user_point);
}
PipeDecodeImpl::~PipeDecodeImpl(){
    if(pipeSyncService){
        delete pipeSyncService;
        pipeSyncService =nullptr;
        printf("PipeDecodeImpl destory\n");
    }
}
/*
* 启动解码
*/
void PipeDecodeImpl::start(){
    pipeSyncService->start();
}
/*
* 主动结束解码
*/
void PipeDecodeImpl::stop(){
    pipeSyncService->stop();
}
/*
* 暂停解码
*/
void PipeDecodeImpl::pause(){
    pipeSyncService->pause();
}
/*
* 恢复解码
*/
void PipeDecodeImpl::resume(){
    pipeSyncService->resume();
}
/*
* 快进(针对本地mp4文件)
*/
int PipeDecodeImpl::seek( int64_t ofst, bool is_pause){
    return pipeSyncService->seek(ofst,is_pause);
}
/*
* 向前跳多少帧
*/
int PipeDecodeImpl::forward(uint64_t frame_num){
    return pipeSyncService->forward(frame_num);
}
/*
* 设置输出帧率
*/        
int PipeDecodeImpl::setFps(int fps){
    return pipeSyncService->setFps(fps);
}
/*
* 设置分辨率
*/        
void PipeDecodeImpl::setVideoSize(EnumVideoSize video_size){
    int width=0,height=0;
    switch (video_size){
        case ESize_640p:
            width = 640;
            height = 480;
            break;
        case ESize_720p:
            width = 1280;
            height = 720;        
            break;
        case ESize_1080p:
            width = 1920;
            height = 1080;        
            break;
        case ESize_2K:
            width = 2560;
            height = 1440;        
            break;            
        default:
            width =1280;
            height =720;
            break;
    }
    pipeSyncService->setWidth(width);
    pipeSyncService->setHeight(height);
}