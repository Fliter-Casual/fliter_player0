#include "fffplay_def.h"

// 这是一个刷新包(空包),常用于SEEK或整个播放器的重置
static AVPacket flush_pkt;

// 在包队列中放入一个包
static int packet_queue_put_private(PacketQueue *q, AVPacket *pkt)
{
    MyAVPacketList *pkt1;

    if (q->abort_request)   //如果停止了，则放入失败
        return -1;
    
    pkt1 = (MyAVPacketList *)av_malloc(sizeof(MyAVPacketList));
    if (!pkt1) //内存不足，申请内存失败
        return -1;
    
    // 此处没有做引用计数，那这里也说明av_read_frame不会替用户释放pkt
    pkt1->pkt = *pkt; // 拷贝AVPacket(浅拷贝，AVPakcet.data等内存并没有拷贝)
    pkt1->next = NULL;
    if (pkt == &flush_pkt) // 如果放入的是flush_pkt，需要增加队列的播放序列号，以区分不连续的两段数据
    {
        q->serial++;
        printf("q->serial = %d\n",q->serial);
    }
    pkt1->serial = q->serial;  //用队列序列号标记节点

    //队列操作：如果last_pkt为空，说明队列是空的，新增节点为队头；
    //否则，队列有数据，则让原队尾的next为新增节点。 最后将队尾指向新增节点
    if(!q->last_pkt)
        q->first_pkt = pkt1;
    else
        q->last_pkt->next = pkt1;
    q->last_pkt = pkt1;

    //队列属性操作：增加节点数、cache大小、cache总时长, 用来控制队列的大小
    q->nb_packets++;
    q->size += pkt1->pkt.size + sizeof(*pkt1);
    q->duration += pkt1->pkt.duration;

    /* XXX: should duplicate packet data in DV case */ 
    //注意：在处理 DV（数字视频）格式时，目前的浅拷贝可能不安全。
    //应该在这里对数据包的实际数据（data 指针指向的内容）进行一次完整的内存复制（深拷贝），以防止数据被后续操作覆盖。

    //发出信号，表明当前队列中有数据了，通知等待中的读线程可以取数据了
    SDL_CondSignal(q->cond);
    return 0;
}

// 内部封装了packet_queue_put_private()
// 标准最佳实践，既保证了安全性，又保持了代码的清晰和可维护性。
int packet_queue_put(PacketQueue *q, AVPacket *pkt)
{
    int ret;
    // 加锁，保证线程安全
    SDL_LockMutex(q->mutex);
    ret = packet_queue_put_private(q, pkt);
    SDL_UnlockMutex(q->mutex);

    // 如果放入失败并且不是flush_pkt，则释放AVPacket
    if(pkt != &flush_pkt && ret <0)
        av_packet_unref(pkt);       
    return ret;
}

// 该函数向队列注入空包以标识特定流的结束
int packet_queue_put_nullpacket(PacketQueue *q, int stream_index)
{
    AVPacket pkt1, *pkt = &pkt1;
    av_init_packet(pkt);//主要做的是初始化内部引用计数和默认值。为了创建一个明确的“空包”（Null Packet），必须手动将关键数据字段置空
    pkt->data = NULL;
    pkt->size = 0;
    pkt->stream_index = stream_index; //必须指定特定流的索引,所以没用全局的flush_pkt
    return packet_queue_put(q, pkt);  //这里是局部创建了一个新的空包，不用手动释放
}

// 初始化包队列
int packet_queue_init(PacketQueue *q)
{
    memset(q, 0, sizeof(PacketQueue));
    q->mutex = SDL_CreateMutex();
    if (!q->mutex) {
        av_log(NULL, AV_LOG_FATAL, "SDL_CreateMutex(): %s\n", SDL_GetError());
        return AVERROR(ENOMEM);
    }
    q->cond = SDL_CreateCond();
    if (!q->cond) {
        av_log(NULL, AV_LOG_FATAL, "SDL_CreateCond(): %s\n", SDL_GetError());
        return AVERROR(ENOMEM);
    }
    q->abort_request = 1;
    return 0;
}

void packet_queue_flush(PacketQueue *q)
{
    MyAVPacketList *pkt, *pkt1;

    SDL_LockMutex(q->mutex);
    for(pkt = q->first_pkt;pkt;pkt = pkt1)
    {
        pkt1 = pkt->next;
        av_packet_unref(&pkt->pkt); //释放AVPacket的资源
        av_freep(&pkt); //释放内存
    }
    q->last_pkt = NULL;
    q->first_pkt = NULL;
    q->nb_packets = 0;
    q->size = 0;
    q->duration = 0;
    SDL_UnlockMutex(q->mutex);
}

// 销毁包队列,调用上面的packet_queue_flush()函数
void packet_queue_destroy(PacketQueue *q)
{
    packet_queue_flush(q); //先清空队列
    SDL_DestroyMutex(q->mutex);
    SDL_DestroyCond(q->cond);
}

// 停止队列
void packet_queue_abort(PacketQueue *q)
{
    SDL_LockMutex(q->mutex);
    q->abort_request = 1; // 请求退出
    SDL_CondSignal(q->cond);//通知一个正在 SDL_CondWait()的线程，让解码线程可以及时检测到共享变量abort_request 的变化。
    SDL_UnlockMutex(q->mutex);
}

// 启动队列
void packet_queue_start(PacketQueue *q)
{
    SDL_LockMutex(q->mutex);
    q->abort_request = 0; // 恢复运行
    packet_queue_put_private(q, &flush_pkt); //这里放入了一个flush_pkt，触发序列号（Serial）更新，丢弃旧数据
    SDL_UnlockMutex(q->mutex);
}

// 从队列中取出一个包
int packet_queue_get(PacketQueue *q, AVPacket *pkt, int block, int *serial) // 获取一个包
{ 
    MyAVPacketList *pkt1;
    int ret;

    SDL_LockMutex(q->mutex);
    for (;;) 
    {
        if(q->abort_request)
        {
            ret = -1;
            break;
        }

        pkt1 = q->first_pkt; // 获取队列头节点
        if (pkt1) // 队列不为空
        {
            q->first_pkt = pkt1->next; // 删除队列头节点
            if (!q->first_pkt)
                q->last_pkt = NULL;
            q->nb_packets--; // 节点数减1
            q->size -= pkt1->pkt.size + sizeof(*pkt1); // 缓存大小减去节点大小
            q->duration -= pkt1->pkt.duration; // 缓存总时长减去节点时长
            *pkt = pkt1->pkt; // 将节点数据浅拷贝给pkt(输出参数),这里发生一次AVPacket结构体拷贝，AVPacket的data只拷贝了指针
            // 两个AVPacket指向同一块资源
            if(serial)  // 检测序号指针是否为空
                *serial = pkt1->serial;
            av_free(pkt1); // 释放节点内存，而非AVPacket结构体内存
            ret = 1;
            break;
        }
        else if (!block) // 队列为空且非阻塞模式
        {
            ret = 0;
            break;
        }
        else
        {
            // 队列为空且阻塞模式
            SDL_CondWait(q->cond, q->mutex); // 等待信号
        }
    }
    SDL_UnlockMutex(q->mutex);
    return ret;
}



static void frame_queue_unref_item(Frame *vp)
{
    av_frame_unref(vp->frame);
}



/* 初始化FrameQueue，视频和音频keep_last设置为1，字幕设置为0 */
/**
 * 初始化帧队列结构体
 *
 * @param f       指向待初始化的 FrameQueue 结构体的指针
 * @param pktq    指向关联的 PacketQueue 结构体的指针
 * @param max_size 请求的最大队列大小，实际大小将受限于 FRAME_QUEUE_SIZE
 * @return        成功返回 0，失败返回负的错误码（如 AVERROR(ENOMEM)）
 */
int frame_queue_init(FrameQueue *f, PacketQueue *pktq, int max_size)
{
    int i;
    memset(f, 0, sizeof(FrameQueue));

    // 创建互斥锁用于线程同步
    if (!(f->mutex = SDL_CreateMutex())) {
        av_log(NULL, AV_LOG_FATAL, "SDL_CreateMutex(): %s\n", SDL_GetError());
        return AVERROR(ENOMEM);
    }

    // 创建条件变量用于线程间通信
    if (!(f->cond = SDL_CreateCond())) {
        av_log(NULL, AV_LOG_FATAL, "SDL_CreateCond(): %s\n", SDL_GetError());
        return AVERROR(ENOMEM);
    }

    f->pktq = pktq;
    f->max_size = FFMIN(max_size, FRAME_QUEUE_SIZE);

    // 预分配队列中所有帧所需的 AVFrame 内存
    for (i = 0; i < f->max_size; i++)
        if (!(f->queue[i].frame = av_frame_alloc()))
            return AVERROR(ENOMEM);
    return 0;
}


// 销毁帧队列，释放所有预分配的AVFrame资源及同步原语(即锁和条件变量)
void frame_queue_destory(FrameQueue *f)
{
    int i;
    /* 遍历队列，释放所有预分配的 AVFrame 资源 */
    for (i = 0; i < f->max_size; i++) {
        Frame *vp = &f->queue[i];
        // 释放对vp->frame中的数据缓冲区的引用，注意不是释放frame对象本身
        frame_queue_unref_item(vp);
        // 释放vp->frame对象
        av_frame_free(&vp->frame);
    }
    /* 销毁同步原语：互斥锁和条件变量 */
    SDL_DestroyMutex(f->mutex);
    SDL_DestroyCond(f->cond);
}

// 发送条件信号，通知等待的线程
void frame_queue_signal(FrameQueue *f)
{
    SDL_LockMutex(f->mutex);
    SDL_CondSignal(f->cond);
    SDL_UnlockMutex(f->mutex);
}

/* 获取帧队列中当前可读位置的帧指针（不移动读索引）, 在调用该函数前先调用frame_queue_nb_remaining确保有frame可读 */
// 不会实际消费该帧,不把它从队列中删除(只读不取)
Frame *frame_queue_peek(FrameQueue *f)
{
    return &f->queue[(f->rindex) % f->max_size];
}

/* 获取当前Frame的下一Frame, 此时要确保queue里面至少有2个Frame */
// 不管你什么时候调用，返回来肯定不是 NULL
/**
 * @brief 获取帧队列中下一个待读取的帧指针（预读下一个位置，不移动读索引）
 *
 * 该函数用于查看当前读索引之后的下一帧数据，常用于需要预先检查下一帧内容的场景。
 * 注意：此操作不会改变队列的读索引状态，仅返回指向下一帧位置的指针。
 *
 * @param f 指向帧队列结构体的指针，不能为空
 * @return Frame* 指向队列中下一个待读取帧的指针。如果队列为空或无效，行为取决于调用者对队列状态的校验
 */
Frame *frame_queue_peek_next(FrameQueue *f)
{
    return &f->queue[(f->rindex  + 1) % f->max_size];
}

/* 获取last Frame* ，即当前读索引位置的帧指针：
 */
Frame *frame_queue_peek_last(FrameQueue *f)
{
    return &f->queue[f->rindex];
}

