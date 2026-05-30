#include "ijkplayercore.h"
#include <iostream>
#include <string.h>
#include "ffmsg.h"

IjkPlayerCore::IjkPlayerCore ()
{

}

int IjkPlayerCore::ijkmp_create(std::function<int (void *)> msg_loop)
{
    int ret = 0;
    _ffplayer = new FFPlayer();
    if(!_ffplayer)
    {
        std::cout << " new FFPlayer() failed" << std::endl;
        return -1;
    }
    _msg_loop = msg_loop;
    ret = _ffplayer->ffplayer_create();

    if(ret < 0)
    {
        return -1;
    }
    return ret;
}

int IjkPlayerCore::ijkmp_destroy()
{
    return 0;
}

// 设置要播放的url,如果之前已经设置过url了，那么就先释放之前的url再设置新的url
int IjkPlayerCore::ijkmp_set_data_source(const char *url)
{
    if(!url)
    {
        return -1;
    }
    // 1. 先释放旧的内存
    if (_data_source)
    {
        free(_data_source);
        _data_source = NULL;
    }

    // 2. 再复制新的
    //用 strdup 复制了一份到堆上，安全
    _data_source = strdup(url); // 分配内存，同时对url进行拷贝
    return 0;
}

// 异步准备，准备完成后会发送消息通知UI线程,UI线程收到消息后可以调用ijkmp_start()开始播放
int IjkPlayerCore::ijkmp_prepare_async()
{
    //判断mp状态，正在异步准备
    _mp_state = MP_STATE_ASYNC_PREPARING;

    //启用消息队列
    msg_queue_start(&_ffplayer->_msg_queue);
    // 创建循环线程,this 是 IjkPlayerCore*,thread函数第一个参数是成员函数指针，第二个是执行这个成员函数的对象指针，第三个是传入这个函数的参数
    _msg_thread = new std::thread(&IjkPlayerCore::ijkmp_msg_loop,this,this);
    // 调用ffplayer,准备播放
    int ret = _ffplayer->ffplayer_prepare_async_1(_data_source);
    if(ret < 0)
    {
        _mp_state = MP_STATE_ERROR;
        return -1;
    }
    return 0;
}

// 触发播放,这个方法的设计来源于Android mediaplayer,IJKPlayer也刻意模仿了这个接口
int IjkPlayerCore::ijkmp_start()
{
    ffplayer_notify_msg1(_ffplayer,FFP_REQ_START);
    return 0;
}

// 读取消息,本质上是 ijkplayer 的“消息分发中心”，负责从消息队列里取消息(输出参数)，并根据消息类型决定要不要直接消费掉，还是继续等下一条
int IjkPlayerCore::ijkmp_get_msg(AVMessage *msg, int block)// block 1 没消息就阻塞,block 0 没消息直接返回
{
    while(1)
    {
        int continue_wait_next_msg = 0;// 是否继续等待下一条消息，默认不继续等待
        // 取消息,如果没有消息，就阻塞
        int retval = msg_queue_get(&_ffplayer->_msg_queue,msg,block);
        if(retval <= 0) //-1 abort(消息被终止了), 0 没有消息, >0 有消息
            return retval;
        switch(msg->what) // 看看是什么消息类型
        {

        //播放器已准备完成
        case FFP_MSG_PREPARED:
            //__FUNCTION__ 编译器预定义的宏，代表当前函数的函数名
            std::cout << __FUNCTION__ << " FFP_MSG_PREPARED" <<std::endl;
            break;

        //这是一个 “请求消息”,上层想让播放器 start，但播放器内部还没真正 start
        //REQ消息不应该交给上层，配合while(1) continue在播放器内部消化掉，等真正状态变化（如 PREPARED / STARTED）再通知上层
        case FFP_REQ_START:
            std::cout << __FUNCTION__ << " FFP_REQ_START" << std::endl;
            continue_wait_next_msg = 1;
            // ffplayer_->start();
            break;
        // 其他情况，不做特殊处理直接返回
        default:
            std::cout << __FUNCTION__ << " default " << msg->what << std::endl;
            break;
        }
        if (continue_wait_next_msg)
        {
            msg_free_res(msg);
            continue;
        }
        return retval; // 正常返回消息
//        只有非 REQ 消息才会走到这里
//        调用者（通常是 Java / UI 层）拿到消息并更新界面
    }
    return -1;
}

// 消息循环线程函数
int IjkPlayerCore::ijkmp_msg_loop(void *arg)
{
    _msg_loop(arg);
    return 0;
}






















