#include <fcntl.h>
#include <stdio.h>
#include <string>
#include <sys/types.h>
#include <sys/stat.h>
#include<iostream>
#if _WIN32
#include <Windows.h>
#include "dshow.h"
#include "shlwapi.h"
#else
#include <dirent.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>
#endif
#include "camera_detect.h"
using namespace std;
#if _WIN32
static void dup_wchar_to_utf8(wchar_t *w,char unique_name[256],int len){
    int l = WideCharToMultiByte(CP_UTF8, 0, w, -1, 0, 0, 0, 0);
    if(l>len){
        l = len-1;
    }
    if (unique_name)
        WideCharToMultiByte(CP_UTF8, 0, w, -1, unique_name, l, 0, 0);

}
static int _get_id(char unique_name[256],const char *tar_id){
    char *p= strstr(unique_name,tar_id);
    if(p==NULL){
        return -1;
    }
    int pos = strlen(tar_id);
    char id[8]={0};

    for (size_t i = 0; i < 8; i++){
        if(p[pos+i]=='&'){
            break;
        }
        id[i] = p[pos+i];
    }
    
    // printf("id=%s\n",id);
    return atoi(id);
}
static int get_vid(char unique_name[256]){
    return _get_id(unique_name,"vid_");
}
static int get_pid(char unique_name[256]){
    return _get_id(unique_name,"pid_");  
}
/**
 * 获取usb设备显示唯一ID
 * pMoniker 设备句柄
 * unique_name:获取usb唯一名字,对于多个设备(ffmpeg/opencv等接口)，可以通过unique_name区分打开
 * len：数组长度
*/
static void GetDisplayName(IMoniker *pMoniker,char unique_name[256],int len){
    LPMALLOC co_malloc = NULL;
    IBindCtx *bind_ctx = NULL;
    int r = CreateBindCtx(0, &bind_ctx);
    if (r != S_OK){
        return ;
    }
    LPOLESTR olestr = NULL;
    r = CoGetMalloc(1, &co_malloc);
    r =pMoniker->GetDisplayName(bind_ctx, NULL, &olestr);
    if(r!=S_OK){
        printf("GetDisplayName failed\n");
        return ;
    }
    dup_wchar_to_utf8(olestr,unique_name,len);
    int i=0,j=0;
    /* replace ':' with '_' since we use : to delineate between sources */
    for (i = 0; i < strlen(unique_name); i++) {
        if (unique_name[i] == ':')
            unique_name[i] = '_';
    }
    // printf("GetDisplayName unique_name=%s\n",unique_name);
    co_malloc->Free(olestr);
    bind_ctx->Release();
    bind_ctx=NULL;
}

static void GetFriendlyName(IPropertyBag *pPropBag,char friendlyName[256],int len){
    VARIANT varName;
    VariantInit(&varName);
    int count = 0;
    HRESULT hr = pPropBag->Read(L"FriendlyName", &varName, 0);
    if (FAILED(hr)){   
        printf("get FriendlyName name failed\n");
        return ;
    }
    while (varName.bstrVal[count] != 0x00 && count < len){
        friendlyName[count] = (char)varName.bstrVal[count];
        count++;
    }
    // printf("FriendlyName name=%s\n",friendlyName);
}
void CameraDetect::GetCameraList(vector<CameraInfo>& list) {
    ICreateDevEnum *pDevEnum = NULL;
    IEnumMoniker *pEnum = NULL;
    int deviceCounter = 0;
    CoInitialize(NULL);
    HRESULT hr = CoCreateInstance(CLSID_SystemDeviceEnum, NULL,CLSCTX_INPROC_SERVER, IID_ICreateDevEnum,
        reinterpret_cast<void**>(&pDevEnum));
    if (FAILED(hr)){
        printf("CoCreateInstance failed\n");
        return ;
    }
    // Create an enumerator for the video capture category.
    hr = pDevEnum->CreateClassEnumerator(CLSID_VideoInputDeviceCategory,&pEnum, 0);
    if (hr != S_OK) {
        printf("CreateClassEnumerator failed\n");
        return ;
    }
    printf("SETUP: Looking For Capture Devices\n");
    IMoniker *pMoniker = NULL;        
    while (pEnum->Next(1, &pMoniker, NULL) == S_OK) {
        IPropertyBag *pPropBag;
        hr = pMoniker->BindToStorage(0, 0, IID_IPropertyBag,(void**)(&pPropBag));
        if (FAILED(hr)) {
            pMoniker->Release();
            continue;  // Skip this one, maybe the next one will work.
        }
        CameraInfo info;
        GetDisplayName(pMoniker,info.unique_name,sizeof(info.unique_name));
        GetFriendlyName(pPropBag,info.camera_type,sizeof(info.camera_type));
        info.pid =get_pid(info.unique_name);
        info.vid =get_vid(info.unique_name);
        list.push_back(info);
        pPropBag->Release();
        pPropBag = NULL;
        pMoniker->Release();
        pMoniker = NULL;
        deviceCounter++;
    }
    pDevEnum->Release();
    pEnum->Release();
    CoUninitialize();
}

#else
static void GetCameraType(char *card,int src_len,char *camera_type,int dest_len){
    int j=0;
    for (size_t i = 0; i < src_len; i++){
        if(card[i]==':'){   //去掉冒号
            continue;
        }
        if(j>=dest_len){
            break;
        }
        camera_type[j++]=card[i];
    }
}
static int GetCameraInfo(const char *devname,CameraInfo *cam_info){
    int fd = open(devname, O_RDWR);
    if(fd<0){
        return -1;
    }
    // printf("devname:%s\n", devname);// 摄像头路径
    struct v4l2_capability cap = {0};
    if(ioctl(fd,VIDIOC_QUERYCAP, &cap)<0){
        return -1;
    }
    //输出设备信息
    // printf("cap.driver = %s \n",cap.driver);
    // printf("cap.card = %s \n",cap.card);
    GetCameraType((char *)cap.card,strlen((const char *)cap.card),cam_info->camera_type,sizeof(cam_info->camera_type));
    // printf("cap.bus_info = %s \n",cap.bus_info);
    // printf("cap.version = %d \n",cap.version);
    // printf("cap.capabilities = %x \n",cap.capabilities);
    // printf("cap.device_caps = %x \n",cap.device_caps);
    // printf("cap.reserved = %x \n",cap.reserved);  

    if (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE){
        // printf("the video device support capture\n");
    }

    struct v4l2_fmtdesc fmt;
    //从第一个输出格式开始查询
    fmt.index = 0;
    //查询照片的输出格式，所以type选择V4L2_BUF_TYPE_VIDEO_CAPTURE
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	//输出当前设备的输出格式
    char fmt_type[5]={0};
    int support_index =0;
    while (ioctl(fd, VIDIOC_ENUM_FMT, &fmt) != -1){
        printf("pixelformat = %c%c%c%c, description = %s \n",
                  fmt.pixelformat & 0xFF, (fmt.pixelformat >> 8) & 0xFF, (fmt.pixelformat >> 16) & 0xFF,
                  (fmt.pixelformat >> 24) & 0xFF, fmt.description);
        snprintf(fmt_type,5,"%c%c%c%c",fmt.pixelformat & 0xFF, (fmt.pixelformat >> 8) & 0xFF, (fmt.pixelformat >> 16) & 0xFF,(fmt.pixelformat >> 24) & 0xFF);
        fmt.index ++;
        if(!strncmp(fmt_type,"MJPG",4)||!strncmp(fmt_type,"YUYV",4)){//暂时只支持这两种格式摄像头采集
            support_index++;
        }
        //CNF4,Z16等格式走另外驱动
    }

    close(fd);    
    return support_index;
}
void CameraDetect::GetCameraList(vector<CameraInfo>& list) {
    struct dirent *ent;
    char *path= "/dev";
    DIR *dir = opendir(path);
    if (dir == NULL) {
        perror("opendir error");
        return ;
    }
    CameraInfo cam_info;
    // 在路径/dev下遍历所以文件
    while ((ent = readdir(dir)) != NULL){
        // 字符串比较，并获取完整的摄像头路径，/dev/video*
        if (memcmp(ent->d_name, "video",5) == 0) {
            //printf("%s\n", ent->d_name);
            std::string camPath;
            camPath.append("/dev/");
            camPath.append(ent->d_name);
            memset(&cam_info,0,sizeof(cam_info));
            if (GetCameraInfo(camPath.c_str(), &cam_info)>0) {
                snprintf(cam_info.unique_name,sizeof(cam_info.unique_name),"%s",camPath.c_str());
                infos.push_back(cam_info);
            }
        }
    }
    closedir(dir); 
}
#endif