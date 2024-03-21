#include <iostream>
#include <thread>
#include <opencv2/core/core.hpp> 
#include<opencv2/highgui/highgui.hpp> 
#include <opencv2/opencv.hpp>
#include <Windows.h>
using namespace cv;
using namespace std;
int record_video() {
    std::string rtspPath = "rtsp://admin:dmai123456@192.168.1.64";
    VideoCapture cap;
    cap.open(rtspPath);
    if (!cap.isOpened()){
        cout << "cannot open video!" << endl;
        return 0;
    }
    string filename ;
    SYSTEMTIME systm;
    GetLocalTime(&systm);
    char buf[128]={0};
    sprintf(buf, "rtsp_video_%d%02d%02d%-02d%02d%02d.avi", systm.wYear,systm.wMonth,systm.wDay,systm.wHour,systm.wMinute,systm.wSecond);
    filename =buf;
    VideoWriter writer = VideoWriter(filename,//path and filename
        (int)CV_FOURCC('M', 'P', '4', '2'),
        (int)cap.get(CAP_PROP_FPS),
        Size((int)cap.get(CAP_PROP_FRAME_WIDTH),
            (int)cap.get(CAP_PROP_FRAME_HEIGHT)),
        true//colorfull pic
    );
    if (!writer.isOpened()) {
        cout << "create vedio failed!" << endl;
        return 0;
    }
    cv::namedWindow("show", cv::WINDOW_FULLSCREEN);
    int frameId=0;
    while (1){
        cv::Mat frame;
        cap >> frame;
        if (frame.empty()){
            cout << "frame is empty!" << endl;
            break;
        }      
        cv::Mat img = frame;
        //cv::imshow("frame", img);
        writer<<img;
        if(++frameId>12000){
            break;
        }
        cv::imshow("show",frame);
        cout << "frameId:" <<frameId<< endl;
        char c = (char)waitKey(10);
		if (c == 'q') {
            break;
        }
     
    }  
    writer.release();
    return 0;
}
typedef  struct {
    int camera_id;
    char name[24];
}CameraData_t;
static void test_capture(void *arg){
    CameraData_t *camera = (CameraData_t *)arg;
    VideoCapture cap(camera->camera_id);
    if (!cap.isOpened()) {
        cout << "cannot open video! camera->camera_id"<<camera->camera_id << endl;
        return ;
    }
    cv::namedWindow(camera->name, cv::WINDOW_FULLSCREEN);
    int frameId = 0;
    while (1) {
        cv::Mat frame;
        cap >> frame;
        if (frame.empty()) {
            cout << "frame is empty!" << endl;
            break;
        }
        cv::Mat img = frame;
        //cv::imshow("frame", img);
        if (++frameId > 12000) {
            break;
        }
        cv::imshow(camera->name, frame);
        // cout << "frameId:" << frameId << endl;
        char c = (char)waitKey(20);
        if (c == 'q') {
            break;
        }

    }
    delete camera;
}


int test_show() {
    CameraData_t *camera = new CameraData_t();
    camera->camera_id = 0;
    snprintf(camera->name,sizeof(camera->name),"%s%d","camera_",camera->camera_id);
    thread t1 =thread(test_capture,camera);
    t1.detach();
    CameraData_t *camera1 = new CameraData_t();
    camera1->camera_id = 1;
    snprintf(camera1->name,sizeof(camera1->name),"%s%d","camera_",camera1->camera_id);
    thread t2 =thread(test_capture,camera1);
    t2.detach();
    while(1){
        Sleep(3000);
    }
    return 0;
}
