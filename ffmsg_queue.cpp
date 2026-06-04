#include "ffmsg_queue.h"
extern "C" {
#include "libavutil/avstring.h"
#include "libavutil/eval.h"
#include "libavutil/mathematics.h"
#include "libavutil/pixdesc.h"
#include "libavutil/imgutils.h"
#include "libavutil/dict.h"
#include "libavutil/parseutils.h"
#include "libavutil/samplefmt.h"
#include "libavutil/avassert.h"
#include "libavutil/time.h"
#include "libavformat/avformat.h"
#include "libavdevice/avdevice.h"
#include "libswscale/swscale.h"
#include "libavutil/opt.h"
#include "libavcodec/avfft.h"
#include "libswresample/swresample.h"
}
// 消息队列头文件
#include "ffmsg.h"

// 释放消息携带的资源
void msg_free_res(AVMessage *msg)
{
    if(!msg || !msg->obj)
        return;
    msg->free_l(msg->obj);//这种消息类中的资源的释放函数是自己定义的
    msg->obj = nullptr;
}

// 消息队列内部重新去构建 AVMessage (重新申请AVMessage,或资源回收利用recycle_msg )
// 优先复用回收的消息对象，不够再 malloc
int msg_queue_put_private(MessageQueue *q, AVMessage *msg)
{
     // 入队,生产者
     AVMessage *msgl;
     if(q->abort_request)//q->abort_request值为1说明队列已终止
         return -1;

     msgl = q->recycle_msg;//尝试从回收池拿消息（对象池)
     if(msgl)
     {
         // 从回收池（recycle_msg 链表）中取出头节点
         q->recycle_msg = msgl->next;
         q->recycle_count++;
     }
     else
     {
         // 回收池空了，重新分配
         q->alloc_count++;
         msgl = (AVMessage *)av_malloc(sizeof(AVMessage));
     }

     // 拷贝消息内容（浅拷贝）
     *msgl = *msg;
     msgl->next = nullptr;

     if(!q->first_msg)//第一次插入
     {
          q->first_msg = msgl;
     }
     else//插入链表尾部（FIFO先进先出,链表实现的队列）
     {
          q->last_msg->next = msgl;
     }

     q->last_msg = msgl; // 新的尾部
     q->nb_messages++; // 插入了一个消息
     SDL_CondSignal(q->cond);// 发送一个信号，通知有消息了（通知等待的消费者线程）
     return 0;
}




// 从头部first_msg取消息,出队(消费者)   msg:输出参数
int msg_queue_get(MessageQueue *q, AVMessage *msg, int block)
{
    AVMessage *msgl;
    int ret;
    SDL_LockMutex(q->mutex); //加锁(线程安全)

    for(;;)//防止虚假唤醒
    {
        if(q->abort_request) //判断队列是否结束,这个字段被置1消息队列就被强制终止了
        {
            ret = -1;
            break;
        }
        // 有消息 获取消息(取出消息队列的头) 出队
        msgl = q->first_msg;
        if(msgl)
        {
            q->first_msg = msgl->next; //出了一个头，头结点变成了原来节点的下一个节点
            if(!q->first_msg)
                q->last_msg = nullptr;
            q->nb_messages--;// 出去了一个消息
            *msg = *msgl;//把消息内容拷贝出去(浅拷贝)

            //回收消息结构体（重点）,obj 不 free（由调用者负责）
            msgl->obj = nullptr;
            // 插入回收池的队头(头插)
            msgl->next = q->recycle_msg;
            q->recycle_msg = msgl;

            ret = 1;
            break; //别把break忘掉了
        }
        else if(!block) //block = 0,没消息&&非阻塞，立刻返回
        {
            ret = 0;
            break;
        }
        else //没消息&&阻塞 → 等待
        {
            SDL_CondWait(q->cond,q->mutex);//这个函数内部自动释放锁了，被signal唤醒后再申请锁(抢锁)
        }
    }
    SDL_UnlockMutex(q->mutex);//解锁，让别人可以拿到
    return ret;
}

// 插入消息,线程安全的,外部接口
int msg_queue_put(MessageQueue *q, AVMessage *msg)
{
    int ret;
    SDL_LockMutex(q->mutex);
    ret = msg_queue_put_private(q,msg);
    SDL_UnlockMutex(q->mutex);
    return ret;
}

// 消息初始化
void msg_init_msg(AVMessage *msg)
{
    memset(msg,0,sizeof(AVMessage));
}

// 插入简单消息，只带消息类型，不带参数
void msg_queue_put_simple1(MessageQueue *q, int what)
{
    //作用: 向队列发送一个“简单消息”（只需指定消息类型,如:退出，暂停，播放）
    AVMessage msg;
    msg_init_msg(&msg);
    msg.what = what;
    msg_queue_put(q,&msg);
}

// 插入简单消息，只带消息类型，只带1个参数
void msg_queue_put_simple2(MessageQueue *q, int what, int arg1)
{

    AVMessage msg;
    msg_init_msg(&msg);
    msg.what = what;
    msg.arg1 = arg1;
    msg_queue_put(q, &msg);
}

// 插入简单消息，只带消息类型，带2个参数
void msg_queue_put_simple3(MessageQueue *q, int what, int arg1, int arg2)
{
    AVMessage msg;
    msg_init_msg(&msg);
    msg.what = what;
    msg.arg1 = arg1;
    msg.arg2 = arg2;
    msg_queue_put(q, &msg);
}

// 释放msg的obj资源
void msg_obj_free_l(void *obj)
{
    av_free(obj);
}

//插入消息，带消息类型，带2个参数，带obj
void msg_queue_put_simple4(MessageQueue *q, int what, int arg1, int arg2, void *obj, int obj_len)
{
    AVMessage msg;
    msg_init_msg(&msg);
    msg.what = what;
    msg.arg1 = arg1;
    msg.arg2 = arg2;
    msg.obj = av_malloc(obj_len);
    //目标内存地址（拷贝到哪里）源内存地址（从哪里拷贝）要拷贝的字节数
    memcpy(msg.obj, obj, obj_len);//内存拷贝
    msg.free_l = msg_obj_free_l;
    msg_queue_put(q, &msg);// 插入消息队列
}

// 消息队列初始化
void msg_queue_init(MessageQueue *q)
{
    memset(q, 0, sizeof(MessageQueue));
    q->mutex = SDL_CreateMutex();
    q->cond = SDL_CreateCond();
    q->abort_request = 1; //该字段置1说明队列未启用
}

// 消息队列flush，清空所有的消息
void msg_queue_flush(MessageQueue *q)
{
   AVMessage *msg;
   AVMessage *next;
   SDL_LockMutex(q->mutex);
   for (msg = q->first_msg; msg != nullptr; msg = next)
   { // 这个时候的obj没有清空？那会导致内存泄漏，即使是把消息对象暂存到了recycle_msg,复用这个节点的时候会覆盖旧的obj,旧的obj丢失泄露
       next = msg->next;// 下把msg的下一个节点保存下来

       msg_free_res(msg);
       msg->obj = nullptr;

       // 再把它插入回收队列(头插)
       msg->next = q->recycle_msg;
       q->recycle_msg = msg;
   }
   q->last_msg = nullptr;
   q->first_msg = nullptr;
   q->nb_messages = 0;
   SDL_UnlockMutex(q->mutex);
}

// 消息销毁,再把回收池中的对象(节点)一个一个释放
void msg_queue_destroy(MessageQueue *q)
{
   msg_queue_flush(q); // 函数的复用，先清空所有的消息

   SDL_LockMutex(q->mutex);
   while(q->recycle_msg)
   {
       AVMessage *msg = q->recycle_msg;
       if (msg)
           q->recycle_msg = msg->next;
       msg_free_res(msg);// 释放消息体内部动态分配的资源（如 data 指针指向的额外内存），防止内存泄漏
       av_freep(&msg);// 释放消息结构体本身（AVMessage* 指针）,释放节点并置空
   }
   SDL_UnlockMutex(q->mutex);

   SDL_DestroyMutex(q->mutex);
   SDL_DestroyCond(q->cond);
}

// 消息队列(msg_queue_get()这个函数)终止：这个函数的作用是通知消息队列强制终止，让正在等待消息的线程(取消息线程)能够立即退出阻塞状态
void msg_queue_abort(MessageQueue *q)
{
    SDL_LockMutex(q->mutex);
    q->abort_request = 1;
    SDL_CondSignal(q->cond);//发送条件信号，唤醒等待线程
    SDL_UnlockMutex(q->mutex);
}

// 启用消息队列
void msg_queue_start(MessageQueue *q)
{
    SDL_LockMutex(q->mutex);
    q->abort_request = 0;
    // 插入一个消息
    AVMessage msg;
    msg_init_msg(&msg);
    msg.what = FFP_MSG_FLUSH;
    msg_queue_put_private(q, &msg);
    SDL_UnlockMutex(q->mutex);
}

//从消息队列中删除所有指定类型（what）的消息，并将被删除的消息节点回收到空闲节点池中；可用于清空特定命令,队列中可能有多个重复的FF_REFRESH事件,批量删除
void msg_queue_remove(MessageQueue *q, int what)
{
    AVMessage **p_msg, *msg, *last_msg;//p_msg:指向消息指针的指针，msg:当前消息，last_msg:上一个消息
    SDL_LockMutex(q->mutex);

    last_msg = q->first_msg;

    if (!q->abort_request && q->first_msg)//如果消息队列没有被终止，并且消息队列里有消息
    {
        p_msg = &q->first_msg;//从消息队列的头部开始遍历
        while (*p_msg)
        {
            msg = *p_msg;
            if (msg->what == what)
            { // 同类型的消息全部删除
                *p_msg = msg->next;
                msg_free_res(msg);

                msg->next = q->recycle_msg; // 消息体回收(头插进回收池)
                q->recycle_msg = msg;

                q->nb_messages--;
            }
            else//如果不是要删除的消息，那么last_msg就更新为当前消息，p_msg更新为下一个消息的地址
            {
                last_msg = msg;
                p_msg = &msg->next;
            }
        }

        if (q->first_msg)//如果消息队列里还有消息，那么last_msg就是最后一个消息
        {
            q->last_msg = last_msg;
        }
        else
        {
            q->last_msg = nullptr;
        }
    }

    SDL_UnlockMutex(q->mutex);
}



















