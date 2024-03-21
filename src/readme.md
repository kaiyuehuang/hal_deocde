# 一、接口设计
class PipeDecodeImpl{
    public:
        /*
        * @input_stream:支持单路或多路视频输入
        * @mPipecb:回调输出结果
        * @user_point:用户指针
        */
        PipeDecodeImpl(std::vector<TInputStream>input_stream,PipeSyncCallback *mPipecb,void *user_point);
        PipeDecodeImpl(TInputStream input_stream,PipeSyncCallback *mPipecb,void *user_point);
        ~PipeDecodeImpl();
        /*
        * 启动解码
        */
        void start();
        /*
        * 主动结束解码
        */
        void stop();
        /*
        * 暂停解码
        */
        void pause();
        /*
        * 恢复解码
        */
        void resume();
        /*
        * 快进(针对本地mp4文件)
        */
        int seek( int64_t ofst, bool is_pause);
        /*
        * 向前跳多少帧
        */
        int forward(uint64_t frame_num);
        /*
        * 设置输出帧率
        */        
        int setFps(int fps);
        void setVideoSize(EnumVideoSize video_size);
};