
#include <stdio.h>
#include "gpu_detect.h"
#ifdef _WIN32
#include <Windows.h>
#pragma comment(lib, "shell32.lib")
#endif
/**
 * 获取当前电脑gpu 信息
*/
void GpuDetect::GetGpuList(std::vector<GpuInfos> &list){
    char gpu_cmd[128]={0};
#ifdef _WIN32    
    snprintf(gpu_cmd,sizeof(gpu_cmd),"%s","wmic path win32_VideoController get name");
	FILE *fp =_popen(gpu_cmd,"r");
#else
    snprintf(gpu_cmd,sizeof(gpu_cmd),"%s","lspci |grep -i vga");
	FILE *fp =popen(gpu_cmd,"r");
#endif  
    if(fp==NULL){
        printf("_popen failed \n");
        return ;
    }
    char buffer[256]={0};
    GpuInfos info;
    int gpu_nums=0;
    while(1){
        if(fgets(buffer,sizeof(buffer),fp)==NULL){
            break;
        }
        if(strstr(buffer,"NVIDIA")){
            // printf("find RTX 2070 \n");
            info.type = E_Nvidia;
            info.id = gpu_nums++;
            list.push_back(info);
            
        }else if(strstr(buffer,"3070")&&strstr(buffer,"NVIDIA")){
            printf("find RTX 3070 \n");
            info.type = E_Nvidia;
            info.id = gpu_nums++;
            list.push_back(info);
        }
        // else if(strstr(buffer,"Intel")&&strstr(buffer,"UHD")){
        //     printf("find uhd graphics  \n");
        //     info.type = E_Intel_HUD;
        // }
        memset(buffer,0,sizeof(buffer));
    }
#ifdef _WIN32	
    _pclose(fp);
#else
	pclose(fp);
#endif	
}

GpuDetect::GpuDetect(/* args */){
}
GpuDetect::~GpuDetect(){
    printf("~GpuDetect exit\n");
}