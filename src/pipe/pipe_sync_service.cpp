#include <thread>
#include "common_utils.h"
#include "camera_detect.h"
#include "pipe_hw_decode.h"
#include "pipe_sync_service.h"
#include "pipe_usb_camera.h"
using namespace std;
/**
 * 消费消息队列
*/
void PipeSyncService::DequeueFrame(){
	unique_lock<mutex> locker_consumer(queue_lock);
	if (frame_queue->empty()){
		cond_consumer.wait(locker_consumer);
	}
	// printf("frame_sync_dequeue end=%d\n",frame_queue->size());
    frame_sync_t *deque_sync_frame=NULL;
    deque_sync_frame = (frame_sync_t *)frame_queue->front();
    frame_queue->pop();
	cond_producer.notify_one();
	WakeupAllPthread();
    if(deque_sync_frame->sync_exit==-1){
        producer_finish=true;
        printf("frame_sync_dequeue is exit sync\n");
        return;
    }
	if(sync_frame_id%100==0){
		long long current_time = getTimestamp();
		float current_fps = (float)sync_frame_id*1000/((current_time-start_time)/1000);
		printf("current_fps=%f,use time=%lld\n",current_fps,current_time-start_time);
	}
	if(user_pipecb){
		user_pipecb->output_multi_frame_cb(deque_sync_frame,user_point);
	}
	deque_sync_frame->frame_s.clear();
	delete(deque_sync_frame);
}

static void _LoopSyncService(PipeSyncService* context) {
	while (1) {
        if( context->producer_finish){
            printf("_loop_sync_service customer ThreadId[%ld] exit\n",this_thread::get_id());
            break;
        }

		context->DequeueFrame();
	}
	context->user_pipecb->exit_sync_cb(0,context->user_point);
}
static void loopStreamSync(void* arg) {
	PipeSyncService* context = (PipeSyncService*)arg;
	_LoopSyncService(context);
}
/**
 * 单线程回调解码之后bgr数据
 * frame:帧信息
 * user_point:用户指针(为this指针,获取类成员数据)
 * 
*/
static void SingleOutputPipeFrameCb(frame_msg_t *frame,void *user_point){
	// printf("SingleOutputPipeFrame camera_id:%d\n",frame->camera_id);
	PipeSyncService* context = (PipeSyncService*)user_point;
	std::unique_lock <std::mutex> lck(context->single_lock);
    if(context->CameraframeIsExist(frame->camera_id,frame->data,frame->size,frame->width,frame->height)==0){
        // printf("cameraframeIsExist this frame is exist queue:%d\n",frame->camera_id);
        return ;
    }
	if(context->sync_frame==NULL){
        context->sync_frame = new frame_sync_t();
		context->sync_frame->frame_s.clear();
    }
    frame_msg_t *frame_data = (frame_msg_t *)calloc(1,sizeof(frame_msg_t));
    frame_data->data = frame->data;
    frame_data->size = frame->size;
	frame_data->width =frame->width;
	frame_data->height = frame->height;
    frame_data->camera_id=frame->camera_id;
    context->sync_frame->frame_s.push_back(frame_data);
	context->sync_numbers++;
	frame_data->timestamp = getTimestamp();
	if(context->sync_numbers==context->pipes.size()){  //帧接收完整，添加到syncPthread 线程写入数据给到算法
		context->QueueFrame();
    }
	while (!context->single_ready){
		context->single_cv.wait(lck);
	}
}
/*
* 单线程启动解码回调
*/
static void  SingleIsStartCb(int id,int width,int height,void *user_point){	
	printf("SingleIsStart IsStart[%d] width=%d,height=%d\n",id,width,height);
	PipeSyncService *context = (PipeSyncService *)user_point;
	context->user_pipecb->start_sync_cb(id,width,height,context->user_point);
}
/*
* 单线程解码结束回调
*/
static void SingleStopCb(int stream_id,void *user_point){
	PipeSyncService *context = (PipeSyncService *)user_point;
	unique_lock<mutex> lockerProducer(context->queue_lock);
	for(std::vector<BasePipe *>::iterator iter= context->pipes.begin(); iter!= context->pipes.end(); iter++){
		BasePipe *pipe = *iter ;
		// printf("IsStoped getStreamId=%d\n",pipe->getStreamId());
		if(pipe->getStreamId()== stream_id){
			context->pipes.erase(iter);
			delete pipe;
			break;
		}
	}
	// printf("pipes.size()=%d\n",pipes.size());
	if(context->pipes.size()==0){
		frame_sync_t *exit_frame = new frame_sync_t();      
		exit_frame->sync_exit=-1;
		context->frame_queue->push(exit_frame);
	}
}
/**
 * 获取输入的视频流类型
*/
static EnumStreamType GetInputStreamType(string input_stream){
	if(strstr(input_stream.c_str(),"rtsp://")){
		printf("rtsp file\n");
		return ERtspStream;
	}
	if(isFileExistsIfstream(input_stream)){
		printf("local file\n");
		return ELocalStream;
	}
	if(IsStringNumbers(input_stream.c_str())){
		printf("usb camera id=%d\n",atoi(input_stream.c_str()));
		return ECameraStream;
	}
	return EUnkownStream;
}
/**
 * 获取输入的uvc 摄像头在电脑唯一名字
*/
static string getUsbUniqueName(string input_stream,vector<CameraInfo > cameraList){
	int id =atoi(input_stream.c_str());
	if(id>cameraList.size()){
		return "";
	}
	return cameraList[id].unique_name;
}
/**
 * 获取当前电脑usb摄像头信息
*/
static void GetCameraList(vector<CameraInfo > &cameraList){	
	CameraDetect detect;
	detect.GetCameraList(cameraList);
	for (size_t i = 0; i < cameraList.size(); i++){
		printf("camera_type:[%s],unique_name:%s,vid=%d,pid=%d\n",cameraList[i].camera_type,cameraList[i].unique_name,cameraList[i].vid,cameraList[i].vid);
	}
}
/**
 * 初始化单个流信息
 * filename:输入的流名字
 * stream_id:流标记的编号
 * pipecb:回调解码接口
 * user_point:用户指针
*/
void PipeSyncService::InitSingleStream(string filename,int stream_id,int decode_gpu,PipeSyncCallback *pipecb,void *user_point){
	BasePipe *pipe=nullptr;
	EnumStreamType stream_type= GetInputStreamType(filename);
	string unique_name = "";
	switch (stream_type){
		case ERtspStream:
			pipe =new PipeHwDecode(filename,pipecb,user_point);
			break;
		case ELocalStream:
			pipe =new PipeHwDecode(filename,pipecb, user_point);
			break;
		case ECameraStream:
			unique_name = getUsbUniqueName(filename,cameraList);
			if(unique_name==""){
				printf("Unsupported this stream:%s",filename.c_str());
				return;
			}
			printf("uvc_name:%s\n",unique_name.c_str());
			pipe = new PipeUsbCamera(unique_name,pipecb, user_point);
			break;				
		default:
			printf("Unsupported this stream:%s",filename.c_str());
			break;;
	}	
	pipe->setStreamType(stream_type);
	pipe->setStreamId(stream_id);
	pipe->setDecodeGpuId(decode_gpu);
	AddStream(pipe);
}
/**
 * 多路视频解码同步初始化接口
*/
PipeSyncService::PipeSyncService(std::vector<TInputStream>input,PipeSyncCallback *mPipecb,void *user_point) {
	producer_finish=0;
	sync_frame_id=0;
	this->user_point =user_point;
	this->user_pipecb = mPipecb;
	this->private_pipecb = new PipeSyncCallback();
	private_pipecb->output_frame_cb = SingleOutputPipeFrameCb;
	private_pipecb->start_sync_cb = SingleIsStartCb;
	private_pipecb->exit_sync_cb = SingleStopCb;
	frame_queue = new std::queue<frame_sync_t *>(); 
	GetCameraList(cameraList);
	printf("input_stream->size()=%d\n",input.size());
	for (int i = 0; i < input.size(); i++) {
		InitSingleStream(input[i].stream,i,input[i].gpu_numbers,private_pipecb,this);
	}
	single_ready = false;
	thread t1 =thread(loopStreamSync,this);
	t1.detach();
	printf("PipeSyncService success\n");
}
/**
 * 单路视频解码初始化接口
*/
PipeSyncService::PipeSyncService(TInputStream input,PipeSyncCallback *mPipecb,void *user_point){
	GetCameraList(cameraList);
	printf(" input_stream.gpu_numbers=%d\n",input.gpu_numbers);
	InitSingleStream(input.stream,0,input.gpu_numbers,mPipecb,user_point);
	printf("PipeSyncService single success\n");
}
PipeSyncService::~PipeSyncService() {
	cameraList.clear();
	if(frame_queue){
		delete frame_queue;
	}
	if(private_pipecb){
		delete private_pipecb;
	}
}
/**
 * 帧同步完成,添加到队列里面
*/
void PipeSyncService::QueueFrame(){
	unique_lock<mutex> lockerProducer(queue_lock);
	sync_frame_id++;
    sync_frame->sync_timestamp = getTimestamp();
	sync_frame->sync_frame_id = sync_frame_id;
	frame_queue->push(sync_frame);
    sync_numbers=0;
	sync_frame=NULL;
	while(frame_queue->size()>MAX_QUEUE_SIZE){	//阻塞所有生产者,不加入队列
		cond_producer.wait(lockerProducer);
	}
	cond_consumer.notify_one();
	// printf("QueueFrame:%d\n",sync_frame_id);
}
/**
 * 检测帧是否已经入队
*/
int PipeSyncService::CameraframeIsExist(int id,void *data,int size,int width,int height){
    if(sync_frame==NULL){
        return -1;
    }
    int ret=-1;
    for (int i=0; i<sync_frame->frame_s.size();i++){    
         frame_msg_t * frame = sync_frame->frame_s[i];
        if(frame->camera_id==id){
            ret=0;
            free(frame->data);
            frame->data = data;
            printf("cameraframeIsExist cameraId=%d\n",id);
            break;
        }
    }
    return ret;
} 

/*
* 唤醒所有线程解码
*/
void PipeSyncService::WakeupAllPthread(){
    // printf("WakeupAllPthread\n");
	std::unique_lock <std::mutex> lck(single_lock);
    single_ready = true; // 设置全局标志位为 true.
	single_cv.notify_all();
}
/*
* 用户主动启动解码
*/
void PipeSyncService::start(){
	printf("%s:start size=%d\n",__func__,pipes.size());
	start_time = getTimestamp();
	for (size_t i = 0; i < pipes.size(); i++){
		pipes[i]->start();
	}
}
/*
* 用户主动结束接口
*/
void PipeSyncService::stop(){
	printf("PipeSyncService stop:%d\n",pipes.size());
	queue_lock.lock();
	for (size_t i = 0; i < pipes.size(); i++){
		pipes[i]->stop();
	}
	queue_lock.unlock(); 
	printf("PipeSyncService set stop ok\n");
}
/*
* 暂停解码
*/
void PipeSyncService::pause(){
	queue_lock.lock();
	for (size_t i = 0; i < pipes.size(); i++){
		pipes[i]->pause();
	}
	queue_lock.unlock();
}
/*
* 恢复解码
*/
void PipeSyncService::resume(){
	queue_lock.lock();
	for (size_t i = 0; i < pipes.size(); i++){
		pipes[i]->resume();
	}
	queue_lock.unlock();
}
/*
* 快进(针对本地mp4文件)
*/
int PipeSyncService::seek( int64_t ofst, bool is_pause){
	queue_lock.lock();
	for (size_t i = 0; i < pipes.size(); i++){
		pipes[i]->seek(ofst,is_pause);
	}
	queue_lock.unlock();
	return 0;
}
/*
* 向前跳多少帧
*/
int PipeSyncService::forward(uint64_t frame_num){
	queue_lock.lock();
	for (size_t i = 0; i < pipes.size(); i++){
		pipes[i]->forward(frame_num);
	}	
	queue_lock.unlock();
	return 0;
}
/*
* 设置输出帧率
*/        
int PipeSyncService::setFps(int fps){
	queue_lock.lock();
	for (size_t i = 0; i < pipes.size(); i++){
		pipes[i]->setFps(fps);
	}	
	queue_lock.unlock();
	return 0;
}
int PipeSyncService::setWidth(int width){
	for (size_t i = 0; i < pipes.size(); i++){
		pipes[i]->setWidth(width);
	}	
    return 0;
}
int PipeSyncService::setHeight(int height){
	for (size_t i = 0; i < pipes.size(); i++){
		pipes[i]->setHeight(height);
	}
	return 0;
}