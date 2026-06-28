// UI界面和FFplayer之间的桥梁
#include "ijkplayercore.h"
#include <iostream>
#include <string.h>
#include "ffmsg.h"
#include "log/easylogging++.h"


IjkPlayerCore::IjkPlayerCore ()
{
    LOG(INFO) << " IjkPlayerCore()" ;
}

IjkPlayerCore::~IjkPlayerCore()
{
    LOG(INFO) << " ~IjkPlayerCore()" ;
}

// 创建播放器,参数是一个函数指针，指向创建的message_loop，即消息循环函数
int IjkPlayerCore::ijkmp_create(std::function<int (void *)> msg_loop)
{
    int ret = 0;
    _ffplayer = new FFPlayer();
    if(!_ffplayer)
    {
        LOG(ERROR) << " new FFPlayer() failed" ;
        return -1;
    }
    _msg_loop = msg_loop; // 消息循环函数
    ret = _ffplayer->ffplayer_create();

    if(ret < 0)
    {
        return -1;
    }
    return 0;
}

int IjkPlayerCore::ijkmp_destroy()
{
    if(_msg_thread->joinable()) {
        LOG(INFO) <<  "call msg_queue_abort" ;
        msg_queue_abort(&_ffplayer->_msg_queue);
        LOG(INFO) <<  "wait msg loop abort" ;
        _msg_thread->join(); // 等待线程退出
    }

    _ffplayer->ffplayer_destroy();
    return 0;
}
// 这个方法的设计来源于Android mediaplayer, 其本意是
//int IjkPlayerCore::ijkmp_set_data_source(Uri uri)
int IjkPlayerCore::ijkmp_set_data_source(const char *url)
{
    if(!url) {
        return -1;
    }
    _data_source = strdup(url); // 分配内存+ 拷贝字符串
    return 0;
}

int IjkPlayerCore::ijkmp_prepare_async()
{
    // 判断mp的状态
    // 正在准备中
    _mp_state = MP_STATE_ASYNC_PREPARING;
    // 启用消息队列
    msg_queue_start(&_ffplayer->_msg_queue);
    // 创建循环线程
    _msg_thread = new std::thread(&IjkPlayerCore::ijkmp_msg_loop, this, this);
    // 调用ffplayer
    int ret = _ffplayer->ffplayer_prepare_async_1(_data_source);
    if(ret < 0) {
        _mp_state = MP_STATE_ERROR;
        return -1;
    }
    return 0;
}

int IjkPlayerCore::ijkmp_start()
{
    ffp_notify_msg1(_ffplayer, FFP_REQ_START);
    return 0;
}

int IjkPlayerCore::ijkmp_stop()
{
    int retval = _ffplayer->ffplayer_stop_1();
    if (retval < 0) {
        return retval;
    }
    return 0;
}

int IjkPlayerCore::ijkmp_pause()
{
    // 发送暂停的操作命令
    ffp_remove_msg(_ffplayer, FFP_REQ_START);
    ffp_remove_msg(_ffplayer, FFP_REQ_PAUSE);
    ffp_notify_msg1(_ffplayer, FFP_REQ_PAUSE);
    return 0;
}

int IjkPlayerCore::ijkmp_seek_to(long msec)
{
    seek_req = 1;
    seek_msec = msec;
    ffp_remove_msg(_ffplayer, FFP_REQ_SEEK);
    ffp_notify_msg2(_ffplayer, FFP_REQ_SEEK, (int)msec);
    return 0;
}

int IjkPlayerCore::ijkmp_forward_to(long incr)
{
    seek_req = 1;
    ffp_remove_msg(_ffplayer, FFP_REQ_FORWARD);
    ffp_notify_msg2(_ffplayer, FFP_REQ_FORWARD, (int)incr);
    return 0;
}

int IjkPlayerCore::ijkmp_back_to(long incr)
{
    seek_req = 1;
    ffp_remove_msg(_ffplayer, FFP_REQ_FORWARD);
    ffp_notify_msg2(_ffplayer, FFP_REQ_FORWARD, (int)incr);
    return 0;
}


// 请求截屏
int IjkPlayerCore::ijkmp_screenshot(char *file_path)
{
    ffp_remove_msg(_ffplayer, FFP_REQ_SCREENSHOT);
    ffp_notify_msg4(_ffplayer, FFP_REQ_SCREENSHOT, 0, 0, file_path, strlen(file_path) + 1);
    return 0;
}

int IjkPlayerCore::ijkmp_get_state()
{
    return _mp_state;
}

long IjkPlayerCore::ijkmp_get_current_position()
{
    return _ffplayer->ffp_get_current_position_l();
}

long IjkPlayerCore::ijkmp_get_duration()
{
    return _ffplayer->ffp_get_duration_l();
}

int IjkPlayerCore::ijkmp_get_msg(AVMessage *msg, int block)
{
    int pause_ret = 0;
    while (1) {
        int continue_wait_next_msg = 0;
        //取消息，没有消息则根据block值 =1阻塞，=0不阻塞。
        int retval = msg_queue_get(&_ffplayer->_msg_queue, msg, block);
        if (retval <= 0) {      // -1 abort, 0 没有消息
            return retval;
        }
        switch (msg->what) {
            case FFP_MSG_PREPARED:
                LOG(INFO) <<  " FFP_MSG_PREPARED" ;
                //            ijkmp_change_state_l(MP_STATE_PREPARED);
                break;
            case FFP_REQ_START:
                LOG(INFO) <<  " FFP_REQ_START" ;
                continue_wait_next_msg = 1;
                retval = _ffplayer->ffplayer_start_1();
                if (retval == 0) {
                    ijkmp_change_state_l(MP_STATE_STARTED);
                }
                break;
            case FFP_REQ_PAUSE:
                continue_wait_next_msg = 1;
                pause_ret = _ffplayer->ffplayer_stop_1();
                if(pause_ret == 0) {
                    //设置为暂停暂停
                    ijkmp_change_state_l(MP_STATE_PAUSED);  // 暂停后怎么恢复？
                }
                break;
            case FFP_MSG_SEEK_COMPLETE:
                LOG(INFO) << "ijkmp_get_msg: FFP_MSG_SEEK_COMPLETE\n";
                seek_req = 0;
                seek_msec = 0;
                break;
            case FFP_REQ_SEEK:
                LOG(INFO) << "ijkmp_get_msg: FFP_REQ_SEEK\n";
                continue_wait_next_msg = 1;
                _ffplayer->ffp_seek_to_l(msg->arg1);
                break;
            case FFP_REQ_FORWARD:
                LOG(INFO) << "ijkmp_get_msg: FFP_REQ_FORWARD\n";
                continue_wait_next_msg = 1;
                _ffplayer->ffp_forward_to_l(msg->arg1);
                break;
            case FFP_REQ_BACK:
                LOG(INFO) << "ijkmp_get_msg: FFP_REQ_BACK\n";
                continue_wait_next_msg = 1;
                _ffplayer->ffp_back_to_l(msg->arg1);
                break;
            case FFP_REQ_SCREENSHOT:
                LOG(INFO) << "ijkmp_get_msg: FFP_REQ_SCREENSHOT: " << (char *)msg->obj ;
                continue_wait_next_msg = 1;
                _ffplayer->ffp_screenshot_l((char *)msg->obj);
                break;
            default:
                LOG(INFO) <<  " default " << msg->what ;
                break;
        }
        if (continue_wait_next_msg) {
            msg_free_res(msg);
            continue;
        }
        return retval;
    }
    return -1;
}

void IjkPlayerCore::ijkmp_set_playback_volume(int volume)
{
    _ffplayer->ffp_set_playback_volume(volume);
}

int IjkPlayerCore::ijkmp_msg_loop(void *arg)
{
    _msg_loop(arg);
    return 0;
}

void IjkPlayerCore::ijkmp_set_playback_rate(float rate)
{
    _ffplayer->ffp_set_playback_rate(rate);
}

float IjkPlayerCore::ijkmp_get_playback_rate()
{
    return _ffplayer->ffp_get_playback_rate();
}

void IjkPlayerCore::AddVideoRefreshCallback(
    std::function<int (const Frame *)> callback)
{
    _ffplayer->AddVideoRefreshCallback(callback);
}

int64_t IjkPlayerCore::ijkmp_get_property_int64(int id, int64_t default_value)
{
    _ffplayer->ffp_get_property_int64(id, default_value);
    return 0;
}

void IjkPlayerCore::ijkmp_change_state_l(int new_state)
{
    _mp_state = new_state;
    ffp_notify_msg1(_ffplayer, FFP_MSG_PLAYBACK_STATE_CHANGED);
}

//int IjkPlayerCore::ijkmp_destroy()
//{
//    _ffplayer->ffplayer_destroy();
//    return 0;
//}

//// 设置要播放的url,如果之前已经设置过url了，那么就先释放之前的url再设置新的url
//int IjkPlayerCore::ijkmp_set_data_source(const char *url)
//{
//    if(!url)
//    {
//        return -1;
//    }
//    // 1. 先释放旧的内存
//    if (_data_source)
//    {
//        free(_data_source);
//        _data_source = nullptr;
//    }

//    // 2. 再复制新的
//    //用 strdup 复制了一份到堆上，安全
//    _data_source = strdup(url); // 分配内存，同时对url进行拷贝
//    return 0;
//}

//// 异步准备，准备完成后会发送消息通知UI线程,UI线程收到消息后可以调用ijkmp_start()开始播放
//int IjkPlayerCore::ijkmp_prepare_async()
//{
//    //判断mp状态，正在异步准备
//    _mp_state = MP_STATE_ASYNC_PREPARING;

//    //启用消息队列
//    msg_queue_start(&_ffplayer->_msg_queue);
//    // 创建循环线程,this 是 IjkPlayerCore*,thread函数第一个参数是成员函数指针(线程函数)，第二个是执行这个成员函数的对象指针(因为成员函数不能脱离对象存在)，第三个是传入这个函数的参数
//    _msg_thread = new std::thread(&IjkPlayerCore::ijkmp_msg_loop,this,this);// 消息循环函数给线程去执行
//    // 调用ffplayer,准备播放
//    int ret = _ffplayer->ffplayer_prepare_async_1(_data_source);
//    if(ret < 0)
//    {
//        _mp_state = MP_STATE_ERROR;
//        return -1;
//    }
//    return 0;
//}

//// 触发播放,这个方法的设计来源于Android mediaplayer,IJKPlayer也刻意模仿了这个接口
//int IjkPlayerCore::ijkmp_start()
//{
//    ffp_notify_msg1(_ffplayer,FFP_REQ_START);
//    return 0;
//}

//int IjkPlayerCore::ijkmp_stop()
//{
//    int ret = _ffplayer->ffplayer_stop_1();
//    if(ret < 0)
//    {
//        _mp_state = MP_STATE_ERROR;
//        return ret;
//    }
//    return 0;
//}

//// 读取消息,本质上是 ijkplayer 的“消息分发中心”，负责从消息队列里取消息(输出参数)，并根据消息类型决定要不要直接消费掉，还是继续等下一条
//int IjkPlayerCore::ijkmp_get_msg(AVMessage *msg, int block)// block 1 没消息就阻塞,block 0 没消息直接返回
//{
//    while(1)
//    {
//        int continue_wait_next_msg = 0;// 是否继续等待下一条消息，默认不继续等待
//        // 取消息,如果没有消息，就阻塞
//        int retval = msg_queue_get(&_ffplayer->_msg_queue,msg,block);
//        if(retval <= 0) //-1 abort(消息被终止了), 0 没有消息, >0 有消息
//            return retval;
//        switch(msg->what) // 看看是什么消息类型
//        {

//        //播放器已准备完成
//        case FFP_MSG_PREPARED:
//            //__FUNCTION__ 编译器预定义的宏，代表当前函数的函数名
//            LOG(INFO) << __FUNCTION__ << " FFP_MSG_PREPARED" ;
//            break;

//        //这是一个 “请求消息”,上层想让播放器 start，但播放器内部还没真正 start
//        //REQ消息不应该交给上层，配合while(1) continue在播放器内部消化掉，等真正状态变化（如 PREPARED / STARTED）再通知上层
//        case FFP_REQ_START:
//            LOG(INFO) << __FUNCTION__ << " FFP_REQ_START" ;
//            continue_wait_next_msg = 1;
//            // _ffplayer->start();
//            break;
//        // 其他情况，不做特殊处理直接返回
//        default:
//            LOG(INFO) << __FUNCTION__ << " default " << msg->what ;
//            break;
//        }
//        if (continue_wait_next_msg)
//        {
//            msg_free_res(msg);
//            continue;
//        }
//        return retval; // 正常返回消息
////        只有非 REQ 消息才会走到这里
////        调用者（通常是 Java / UI 层）拿到消息并更新界面
//    }
//    return -1;
//}

//// 消息循环线程函数
//int IjkPlayerCore::ijkmp_msg_loop(void *arg)
//{
//    _msg_loop(arg);
//    return 0;
//}

//void IjkPlayerCore::AddVideoRefreshCallback(std::function<int(const Frame *)> callback)
//{
//    _ffplayer->AddVideoRefreshCallback(callback);
//}






















