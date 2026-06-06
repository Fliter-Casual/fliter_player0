// UI界面和FFplayer之间的桥梁
#ifndef PLAYERCORE_H
#define PLAYERCORE_H

#include <mutex>
#include <thread>
#include <functional>
#include "fffplay.h"  //真正意义上的播放器FFPlayer ffplayer在这里
#include "ffmsg_queue.h"

/*-
 MPST_CHECK_NOT_RET(mp->mp_state, MP_STATE_IDLE);
 MPST_CHECK_NOT_RET(mp->mp_state, MP_STATE_INITIALIZED);
 MPST_CHECK_NOT_RET(mp->mp_state, MP_STATE_ASYNC_PREPARING);
 MPST_CHECK_NOT_RET(mp->mp_state, MP_STATE_PREPARED);
 MPST_CHECK_NOT_RET(mp->mp_state, MP_STATE_STARTED);
 MPST_CHECK_NOT_RET(mp->mp_state, MP_STATE_PAUSED);
 MPST_CHECK_NOT_RET(mp->mp_state, MP_STATE_COMPLETED);
 MPST_CHECK_NOT_RET(mp->mp_state, MP_STATE_STOPPED);
 MPST_CHECK_NOT_RET(mp->mp_state, MP_STATE_ERROR);
 MPST_CHECK_NOT_RET(mp->mp_state, MP_STATE_END);
 */

/*-
 * ijkmp_set_data_source()  -> MP_STATE_INITIALIZED
 *
 * ijkmp_reset              -> self
 * ijkmp_release            -> MP_STATE_END
 */
#define MP_STATE_IDLE               0 //初始状态

/*-
 * ijkmp_prepare_async()    -> MP_STATE_ASYNC_PREPARING
 *
 * ijkmp_reset              -> MP_STATE_IDLE
 * ijkmp_release            -> MP_STATE_END
 */
#define MP_STATE_INITIALIZED        1 //已设置数据源

/*-
 *                   ...    -> MP_STATE_PREPARED
 *                   ...    -> MP_STATE_ERROR
 *
 * ijkmp_reset              -> MP_STATE_IDLE
 * ijkmp_release            -> MP_STATE_END
 */
#define MP_STATE_ASYNC_PREPARING    2 //正在准备（异步）

/*-
 * ijkmp_seek_to()          -> self
 * ijkmp_start()            -> MP_STATE_STARTED
 *
 * ijkmp_reset              -> MP_STATE_IDLE
 * ijkmp_release            -> MP_STATE_END
 */
#define MP_STATE_PREPARED           3 //准备完成，可以播放

/*-
 * ijkmp_seek_to()          -> self,表示状态不变
 * ijkmp_start()            -> self
 * ijkmp_pause()            -> MP_STATE_PAUSED
 * ijkmp_stop()             -> MP_STATE_STOPPED
 *                   ...    -> MP_STATE_COMPLETED
 *                   ...    -> MP_STATE_ERROR
 *
 * ijkmp_reset              -> MP_STATE_IDLE
 * ijkmp_release            -> MP_STATE_END
 */
#define MP_STATE_STARTED            4 //正在播放

/*-
 * ijkmp_seek_to()          -> self
 * ijkmp_start()            -> MP_STATE_STARTED
 * ijkmp_pause()            -> self
 * ijkmp_stop()             -> MP_STATE_STOPPED
 *
 * ijkmp_reset              -> MP_STATE_IDLE
 * ijkmp_release            -> MP_STATE_END
 */
#define MP_STATE_PAUSED             5 //已暂停

/*-
 * ijkmp_seek_to()          -> self
 * ijkmp_start()            -> MP_STATE_STARTED (from beginning)
 * ijkmp_pause()            -> self
 * ijkmp_stop()             -> MP_STATE_STOPPED
 *
 * ijkmp_reset              -> MP_STATE_IDLE
 * ijkmp_release            -> MP_STATE_END
 */
#define MP_STATE_COMPLETED          6 //播放完成（自然结束）

/*-
 * ijkmp_stop()             -> self
 * ijkmp_prepare_async()    -> MP_STATE_ASYNC_PREPARING
 *
 * ijkmp_reset              -> MP_STATE_IDLE
 * ijkmp_release            -> MP_STATE_END
 */
#define MP_STATE_STOPPED            7 //已停止

/*-
 * ijkmp_reset              -> MP_STATE_IDLE
 * ijkmp_release            -> MP_STATE_END
 */
#define MP_STATE_ERROR              8 //错误状态

/*-
 * ijkmp_release            -> self
 */
#define MP_STATE_END                9 //已销毁 (终态)



class IjkPlayerCore
{
public:
    IjkPlayerCore();
    ~IjkPlayerCore();
    int ijkmp_create(std::function<int(void *)> msg_loop);
    int ijkmp_destroy();
    // 设置要播放的url
    int ijkmp_set_data_source(const char *url);
    // 准备播放
    int ijkmp_prepare_async();
    // 触发播放
    int ijkmp_start();
    // 停止
    int ijkmp_stop();
    // 暂停
    int ijkmp_pause();
    // seek到指定位置
    int ijkmp_seek_to(long msec);
    // 获取播放状态
    int ijkmp_get_state();
    // 是不是播放中
    bool ijkmp_is_playing();
    // 当前播放位置
    long ijkmp_get_current_position();
    // 总长度
    long ijkmp_get_duration();
    // 已经播放的长度
    long ijkmp_get_playable_duration();
    // 设置循环播放
    void ijkmp_set_loop(int loop);
    // 获取是否循环播放
    int  ijkmp_get_loop();
    // 读取消息
    int ijkmp_get_msg(AVMessage *msg, int block);
    // 设置音量
    void ijkmp_set_playback_volume(float volume);

    int ijkmp_msg_loop(void *arg);

private:
    // 互斥量
    std::mutex _mutex;
    // 真正的播放器
    FFPlayer *_ffplayer = nullptr;
    //函数指针, 指向创建的message_loop，即消息循环函数
    //    int (*msg_loop)(void*);
    std::function<int(void *)> _msg_loop = nullptr; // ui处理消息的循环,这个循环还没写
    //消息机制线程
    std::thread *_msg_thread = nullptr; // 执行msg_loop
    //    SDL_Thread _msg_thread;
    //字符串，就是一个播放url
    char *_data_source = nullptr;
    //播放器状态，例如prepared,resumed,error,completed等
    int _mp_state;  // 播放状态
};

#endif // PLAYERCORE_H
