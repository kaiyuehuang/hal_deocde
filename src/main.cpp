#include <stdio.h>
#include <iostream>
#include <string>
#include <vector>
#include "common_utils.h"
#include <opencv2/core.hpp>
#include "opencv2/opencv.hpp"
#include "pipe_decode_impl.h"
using namespace std;

#define EXIT_OK	0
#define IS_RUN	1
static PipeSyncCallback mPipecb;
static int is_exit=IS_RUN;
static PipeDecodeImpl* pipeDecodeImpl=NULL;
/*
 * 启动拉流之后主动回调接口 
*/
static void start_sync_cb(int id,int width,int height,void *user_point){
	char *data = (char *)user_point;
	printf("start_sync_cb data=%s\n",data);
	printf("id:%d,width=%d,height=%d\n",id,width,height);
}
/*
 * 输出同步之后帧信息
 sync_frame 同步之后帧信息结构体，包含每一路数据根据 frame->camera区分ID号
*/
static void output_multi_frame_cb(frame_sync_t *sync_frame,void *user_point){
	cv::Mat canvas = cv::Mat::zeros(cv::Size(3200, 960), CV_8UC3);
	for (int i=0 ; i<sync_frame->frame_s.size();i++){    
		frame_msg_t * frame = sync_frame->frame_s[i];
		// printf("frame->camera=%d,bgr_size=%d\n",frame->camera,frame->bgr_size);
		cv::Mat img_src = cv::Mat(frame->height, frame->width, CV_8UC3,frame->data);
		cv::Mat show_img;
		if(frame->camera_id<3){
			show_img=canvas(cv::Rect(frame->camera_id * 640, 0, 640, 480));
		}else{
			show_img=canvas(cv::Rect((frame->camera_id -3) * 640, 480, 640, 480));
		}
		cv::resize(img_src, show_img, cv::Size(640, 480));
		if(frame->data){
			free(frame->data);
		}
		free(frame);
	}
	
	cv::imshow("async",canvas);
	cv::waitKey(1);
	printf("sync_frame->sync_frame_id=%d\n",sync_frame->sync_frame_id);
	if(sync_frame->sync_frame_id>300){		//for test auto exit
		pipeDecodeImpl->stop();
	}
}
/*
 * 输出单路数据帧信息
*/
static void output_frame_cb(frame_msg_t *frame,void *user_point){
	char *data = (char *)user_point;
	printf("frame->frameId=%d\n",frame->frame_id);
	cv::Mat img_src = cv::Mat(frame->height, frame->width, CV_8UC3,frame->data);
	cv::imshow("async",img_src);
	cv::waitKey(1);
	free(frame->data);
	if(frame->frame_id==10){
		pipeDecodeImpl->seek(200,false);
	}
}
static void output_raw_frame_cb(frame_msg_t *frame,void *user_point){
	// printf("raw_size=%d\n",frame->size);	
}
/**
 * 拉流结束
*/
static void exit_sync_cb(int stream_id,void *user_point){
	printf("exit_sync_cb \n");
	is_exit=EXIT_OK;
}

void test_capture_callback(int width,int height,void *bgr_data,int bgr_size){
	cv::Mat img_src = cv::Mat(height, width, CV_8UC3,bgr_data);
	cv::imshow("async",img_src);
	cv::waitKey(1);	
}
/**
 * 多路视频同步测试接口
*/
void testPipeSyncStream(std::vector<TInputStream> input_stream) {

    mPipecb.exit_sync_cb = exit_sync_cb;
    mPipecb.start_sync_cb = start_sync_cb;
    mPipecb.output_multi_frame_cb = output_multi_frame_cb;
	mPipecb.output_frame_cb = output_frame_cb;
	mPipecb.output_raw_frame = output_raw_frame_cb;
	char *data = new char[24];
	snprintf(data,24,"test,user_point");
	if(input_stream.size()==1){
		pipeDecodeImpl = new PipeDecodeImpl(input_stream[0],&mPipecb,(void *)data);
	}else{
		pipeDecodeImpl = new PipeDecodeImpl(input_stream,&mPipecb,(void *)data);
	}
	pipeDecodeImpl->setVideoSize(ESize_1080p);
}

int record_video();
//rtsp://admin:dmai123456@192.168.4.234
int main(int argc, char** argv) {
	if(argc<1){
		return -1;
	}
	std::vector<TInputStream> input_stream;
	for (size_t i = 1; i < argc; i++){
		TInputStream stream_info;
		stream_info.stream = argv[i];
		stream_info.gpu_numbers = 10;
		input_stream.push_back(stream_info);	//传多个rtsp流
	}
	testPipeSyncStream(input_stream);	

	char cmder[128]={0};
	while (1) {
		cin >> cmder;
		printf("cmder=%s\n",cmder);
		if(!strncmp(cmder,"start",strlen("start"))){
			pipeDecodeImpl->start();
		}else if(!strncmp(cmder,"stop",strlen("stop"))){
			pipeDecodeImpl->stop();
		}else if(!strncmp(cmder,"pause",strlen("pause"))){
			pipeDecodeImpl->pause();
		}else if(!strncmp(cmder,"resume",strlen("resume"))){
			pipeDecodeImpl->resume();
		}else if(!strncmp(cmder,"seek",strlen("seek"))){
			pipeDecodeImpl->seek(0,0);
		}else if(!strncmp(cmder,"forward",strlen("forward"))){
			pipeDecodeImpl->forward(1);
		}
    }
	delete pipeDecodeImpl;
	return 0;
}