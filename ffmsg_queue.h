#ifndef FFMSG_QUEUE_H
#define FFMSG_QUEUE_H


#include "SDL.h"

typedef struct AVMessage {  //消息类
    int what;           // 消息类型
    int arg1;           // 参数1
    int arg2;           // 参数2
    void *obj;          // obj可能是：AVPacket* AVFrame* malloc出来的buffer,如果arg1 arg2还不够存储消息则使用该参数
    void (*free_l)(void *obj);  // obj的对象是分配的，这里要自定义释放函数,不同消息资源释放方式不同 → 用函数指针
    struct AVMessage *next; // 下一个消息,消息用链表的形式组织起来
} AVMessage;


typedef struct MessageQueue {   // 消息队列
    AVMessage *first_msg;   // 消息头
    AVMessage *last_msg;    // 消息尾部
    AVMessage *recycle_msg; // 回收池 (之前申请的消息类资源循环使用),和消息是两个独立的链表
    int nb_messages;    // 有多少个消息
    int abort_request;  // 请求终止消息队列
    SDL_mutex *mutex;   // 互斥量(锁),加锁和解锁操作的参数
    SDL_cond *cond;     // 条件变量

    //验证“对象池”是否有效--内存稳定、性能更好
    //消息结构体 一直在复用
    //没有频繁 malloc/free,CPU缓存命中率高
    int recycle_count;  // 复用已有 AVMessage 的次数(利于局部性原理:程序在执行时，倾向于频繁访问最近访问过的数据或附近的数据)
    int alloc_count;    // 真正 av_malloc 的次数
} MessageQueue;


// 释放msg的obj资源
void msg_free_res(AVMessage *msg);
// 私有插入消息,入队(核心),生产者
int msg_queue_put_private(MessageQueue *q, AVMessage *msg);
// 插入消息,暴露给外部的生产者接口(入队)
int msg_queue_put(MessageQueue *q, AVMessage *msg);
// 初始化消息
void msg_init_msg(AVMessage *msg);
// 插入简单消息，只带消息类型，不带参数
void msg_queue_put_simple1(MessageQueue *q, int what);
// 插入简单消息，只带消息类型，只带1个参数
void msg_queue_put_simple2(MessageQueue *q, int what, int arg1);
// 插入简单消息，只带消息类型，带2个参数
void msg_queue_put_simple3(MessageQueue *q, int what, int arg1, int arg2);
// 释放msg的obj资源
void msg_obj_free_l(void *obj);
//插入消息，带消息类型，带2个参数，带obj
void msg_queue_put_simple4(MessageQueue *q, int what, int arg1, int arg2, void *obj, int obj_len);
// 消息队列初始化
void msg_queue_init(MessageQueue *q);
 // 消息队列flush，清空所有的消息
void msg_queue_flush(MessageQueue *q);
// 消息销毁
void msg_queue_destroy(MessageQueue *q);
// 消息队列终止
void msg_queue_abort(MessageQueue *q);
// 启用消息队列
void msg_queue_start(MessageQueue *q);
// 读取消息
/* return < 0 if aborted, 0 if no msg and > 0 if msg.  */
int msg_queue_get(MessageQueue *q, AVMessage *msg, int block);
// 消息删除 把队列里同一消息类型的消息全删除掉
void msg_queue_remove(MessageQueue *q, int what);

#endif // FFMSG_QUEUE_H
