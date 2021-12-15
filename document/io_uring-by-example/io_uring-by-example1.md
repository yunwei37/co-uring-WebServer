# io_uring 动手实践 part1: 使用系统调用接口实现 cat 程序

## 原文

感觉目前看到介绍 io_uring 的文章还是比较少，大部分都集中在对其原理性的介绍和简单的对官方文档的翻译，真正结合实际的例子还是比较少。本文翻译整理自一篇博客：

[io-uring-by-example-part-1-introduction](https://unixism.net/2020/04/io-uring-by-example-part-1-introduction/)

同时也增加了一些自己的理解; 另外，在 2020 年，C++ 也正式将协程 coroutine 加入标准，在这之后, 我尝试使用 io_uring 和 c++ 协程实现了一个高性能web服务器，并进行了一些性能测试，具体代码会放在这个仓库里面，同时也包含了这篇文档以及所需的代码：

[]()

<!-- TOC -->

- [io_uring 动手实践 part1: 使用系统调用接口实现 cat 程序](#io_uring-动手实践-part1-使用系统调用接口实现-cat-程序)
  - [原文](#原文)
  - [介绍](#介绍)
  - [一个简单的 cat 程序](#一个简单的-cat-程序)
  - [Cat io_uring](#cat-io_uring)
    - [io_uring 接口](#io_uring-接口)
      - [完成队列条目 （Completion Queue Entry）](#完成队列条目-completion-queue-entry)
      - [顺序](#顺序)
      - [提交队列条目（SQE)](#提交队列条目sqe)
    - [io_uring 版本的 cat](#io_uring-版本的-cat)
      - [初始设置](#初始设置)
      - [处理共享的环形缓冲区](#处理共享的环形缓冲区)
      - [读取完成队列条目](#读取完成队列条目)
      - [提交](#提交)

<!-- /TOC -->

## 介绍

仔细想想，只有 I/O 和计算是计算机真正做的两件事。在 Linux 下，对于计算，您可以在进程或线程之间进行选择；说到 I/O，Linux 既有同步 I/O，也称为阻塞 I/O， 和异步 I/O。尽管异步 I/O（aio系统调用系列）已经成为 Linux 的一部分有一段历史了，但它们仅适用于直接 I/O 而不适用于缓冲 I/O。对于以缓冲模式打开的文件，aio就像常规的阻塞系统调用一样。这不是一个令人愉快的限制。除此之外，Linux 当前的aio 接口还有很多系统调用开销。

考虑到项目的复杂性，提出一个提供高性能异步 I/O 的 Linux 子系统并不容易，因此对 io_uring 的大肆宣传是绝对合理的。不仅io_uring提供了一个优雅的内核/用户空间接口，它还通过允许一种特殊的轮询模式，完全取消从内核到用户空间获取数据的系统调用，从而提供了卓越的性能。

然而，对于大多数的异步编程完全是另一回事。如果你已经试过在像 C 这样的低级语言中用select/ poll/epoll 异步编程，你会明白我的意思。我们不太擅长异步思考，换句话说，使用线程。线程有一个“从这里开始”、“做 1-2-3 件事”和“从这里结束”的进展。尽管它们被操作系统多次阻塞和启动，但这种错觉对程序员来说是隐藏的，因此它是一个相对简单的心理模型，可以吸收和适应您的需求。但这并不意味着异步编程很难：它通常是程序中的最低层。一旦你编写了一个抽象层出来，你就会很舒服并忙于做你的应用程序真正打算做的事情，你的用户主要关心的事情。

说到抽象，io_uring 确实提供了一个更高级的库 liburing，它实现并隐藏了很多io_uring 需要的模板代码，同时提供了一个更简单的接口供您处理。但是，如果不先了解 io_uring 底层是如何工作的，那么使用 liburing 的乐趣何在？知道了这一点，您也可以更好地使用 liburing：您会了解极端情况，并且可以更好地了解其背后工作的原理。这是一件好事。为此，我们将使用 liburing 构建大多数示例，但我们同时也会使用系统调用接口构建它们。

## 一个简单的 cat 程序

让我们以同步或阻塞的方式使用 readv() 系统调用，构建一个简单的 cat 等效命令。这将使您熟悉 readv()，它是启用分散/聚集 I/O 的系统调用集的一部分，也称为向量 I/O。如果您熟悉 readv() 工作方式，则可以跳到下一节。

比起 read() 和 write() 将文件描述符、缓冲区及其长度作为参数，readv() 和 writev() 将文件描述符、指向struct iovec结构数组的指针和最后一个表示该数组长度的参数作为参数。现在让我们来看看struct iovec。

```c
struct iovec {
     void  *iov_base;    /* 起始地址 */
     size_t iov_len;     /* 要传输的字节数 */
};
```
> 函数原型：
> ssize_t readv(int fd, const struct iovec *iov, int iovcnt);
> ssize_t writev(int fd, const struct iovec *iov, int iovcnt);
> 
> 关于 readv/writev 的性能分析，可以参考 https://zhuanlan.zhihu.com/p/341366946

每个结构简单地指向一个缓冲区。一个基地址和一个长度。

您可能会问，比起常规 read() 和 write()，使用矢量或分散/收集 I/O 有什么意义。答案是使用 readv() 和 writev() 更自然。例如，使用readv()，您可以填充一个 struct 的许多成员，而无需求助于复制缓冲区或多次调用read()，这两种方法的效率都相对较低。同样的优势适用于writev(). 此外，这些调用是原子的，而多次调用read()和write()不是，如果您出于某种原因碰巧关心它。

虽然主要用于将文件的内容打印到控制台，但 cat 命令 concatenates（意味着连接在一起）并打印作为命令参数传入的文件的内容。在我们的cat示例中，我们将使用 readv() 从文件中读取数据以打印到控制台。我们将逐块读取文件，并且每个块都将由一个iovec 结构指向。readv() 被阻塞，当它返回时，假设没有错误，这些 struct iovec 结构指向一组包含文件数据的缓冲区。然后我们将它们打印到控制台。这足够简单。

```c
#include <stdio.h>
#include <sys/uio.h>
#include <sys/stat.h>
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <stdlib.h>

#define BLOCK_SZ    4096

/*
 * 返回传入文件描述符的文件的大小。
 * 正确处理常规文件和块设备。
 * */

off_t get_file_size(int fd) {
    struct stat st;

    if(fstat(fd, &st) < 0) {
        perror("fstat");
        return -1;
    }
    if (S_ISBLK(st.st_mode)) {
        unsigned long long bytes;
        if (ioctl(fd, BLKGETSIZE64, &bytes) != 0) {
            perror("ioctl");
            return -1;
        }
        return bytes;
    } else if (S_ISREG(st.st_mode))
        return st.st_size;

    return -1;
}

/*
 * 输出长度为 len 的字符串到 stdout
 * 我们在这里使用缓冲输出以提高效率，
 * 因为我们需要逐个字符地输出。
 * */
void output_to_console(char *buf, int len) {
    while (len--) {
        fputc(*buf++, stdout);
    }
}

int read_and_print_file(char *file_name) {
    struct iovec *iovecs;
    int file_fd = open(file_name, O_RDONLY);
    if (file_fd < 0) {
        perror("open");
        return 1;
    }

    off_t file_sz = get_file_size(file_fd);
    off_t bytes_remaining = file_sz;
    int blocks = (int) file_sz / BLOCK_SZ;
    if (file_sz % BLOCK_SZ) blocks++;
    iovecs = malloc(sizeof(struct iovec) * blocks);

    int current_block = 0;

    /*
     * 对于我们正在读取的文件，分配足够的块来容纳
     * 文件数据。每个块都在一个 iovec 结构中描述，
     * 它作为 iovecs 数组的一部分传递给 readv。
     * */
    while (bytes_remaining) {
        off_t bytes_to_read = bytes_remaining;
        if (bytes_to_read > BLOCK_SZ)
            bytes_to_read = BLOCK_SZ;


        void *buf;
        if( posix_memalign(&buf, BLOCK_SZ, BLOCK_SZ)) {
            perror("posix_memalign");
            return 1;
        }
        iovecs[current_block].iov_base = buf;
        iovecs[current_block].iov_len = bytes_to_read;
        current_block++;
        bytes_remaining -= bytes_to_read;
    }

    /*
     * readv() 调用将阻塞，直到所有 iovec 缓冲区被填满
     * 文件数据。一旦它返回，我们应该能够从 iovecs 访问文件数据
     * 并在控制台上打印它们。
     * */
    int ret = readv(file_fd, iovecs, blocks);
    if (ret < 0) {
        perror("readv");
        return 1;
    }

    for (int i = 0; i < blocks; i++)
        output_to_console(iovecs[i].iov_base, iovecs[i].iov_len);

    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <filename1> [<filename2> ...]\n",
                argv[0]);
        return 1;
    }

    for (int i = 1; i < argc; i++) {
        if(read_and_print_file(argv[i])) {
            fprintf(stderr, "Error reading file\n");
            return 1;
        }
    }

    return 0;
}
```

这是一个足够简单的程序。我们现在讨论这个，以便我们可以将它与我们接下来将采用的 io_uring 方法进行比较和对比。这个程序的核心是一个循环，它通过首先找到文件的大小，来计算保存我们正在读取的文件数据所需的块数，为所有需要的iovec结构分配内存。我们迭代等于文件大小的块数的计数，分配块大小的内存来保存实际数据，最后调用 readv() 读取数据。就像我们之前讨论过的，readv() 这里是同步的。这意味着它会阻塞，直到它满足了它被调用的请求。当它返回时，我们分配并指向的内存块iovec结构用文件数据填充。然后我们通过调用该 output_to_console() 函数将文件数据打印到控制台。

## Cat io_uring

现在让我们使用 io_uring 编写一个功能等效的程序. 我们将在 io_uring 使用的操作将是readv。

### io_uring 接口

io_uring 的接口很简单。有一个提交队列，有一个完成队列。在提交队列中，您提交有关要完成的各种操作的信息，例如，对于我们当前的程序，我们想要用 readv() 读取文件，因此我们将提交队列请求，作为提交队列条目 (SQE) 的一部分。由于它是一个队列，您可以发出许多请求。这些操作可以是读、写等的混合。然后，我们称之为 io_uring_enter() 的系统调用告诉内核，我们已经向提交队列添加了请求。内核然后执行它的任务，一旦它完成了这些请求的处理，它就会将结果作为完成队列条目 （CQE）的一部分，放入完成队列，或者说每个对应 SQE 的完成队列条目中。这些 CQE 可以从用户空间访问。

精明的读者会注意到，这种用多个 I/O 请求填充队列然后进行单个系统调用的接口，而不是对每个 I/O 请求进行一次系统调用，已经更有效了。为了进一步提高效率，io_uring 支持一种模式，在这种模式下，内核轮询您进入提交队列的条目，而您甚至不必调用 io_uring_enter() 通知内核有关更新提交队列条目的信息。另一点需要注意的是，在 Spectre 和 Meltdown 硬件漏洞被发现，并且操作系统为其创建解决方法之后，系统调用比以往任何时候都更加昂贵。因此，对于高性能应用程序来说，减少系统调用的数量确实是一件大事。

在您执行任何这些操作之前，您需要设置队列，它们实际上是具有特定深度/长度的环形缓冲区。您调用 io_uring_setup() 系统调用来完成此操作。我们通过将提交队列条目添加到环形缓冲区，并从完成队列环形缓冲区，读取完成的队列条目来完成实际工作。这是对 io_uring 接口的概述。

#### 完成队列条目 （Completion Queue Entry）

现在我们有了一个关于事物如何运作的心智模型，让我们更详细地看看这是如何完成的。与提交队列条目（SQE）相比，完成队列条目（CQE）非常简单,所以，让我们先来看看。SQE 是一个结构体，您可以使用它来提交请求, 将其添加到提交环形缓冲区。CQE 是一个结构体的实例，内核对添加到提交队列的每个 SQE 结构体实例进行响应。这包含您通过 SQE 实例请求的操作的结果:

```c
    struct io_uring_cqe {
  __u64  user_data;  /* sqe->user_data submission passed back */
  __s32  res;        /* result code for this event */
  __u32  flags;
    };
```

如代码注释中所述， user_data 字段是按原样从 SQE 传递到 CQE 实例的内容, 假设您在提交队列中提交了一堆请求，它们并不一定以相同的顺序完成并到达完成队列。以以下场景为例：您的机器上有两个磁盘，一个是旋转速度较慢的硬盘驱动器，另一个是超快的 SSD。您在提交队列中提交了 2 个请求，第一个在较慢的旋转硬盘上读取 100kB 文件，第二个在较快的 SSD 上读取相同大小的文件。如果要保持顺序，即使 SSD 上文件中的数据预计会更快到达，内核是否也应该等待旋转硬盘驱动器上文件中的数据可用？这是一个坏主意，因为这会阻止我们尽可能快地完成所有的任务。所以，当 CQE 可用时，它们可以按任何顺序到达，无论哪个操作快速完成，它都会立即可用。但

由于没有指定 CQE 到达的顺序，您如何识别特定 CQE 对应于哪个 SQE 请求？一种方法是使用该user_data字段来识别它。并不是说你会设置一个唯一的 ID 或其他东西，而是你通常会传递一个指针进去。如果这令人困惑，请等到稍后在这里看到一个清晰的示例。

完成队列条目很简单，因为它主要关注系统调用的返回值，该值在其res字段中返回。例如，如果您将 read 操作加入队列，成功完成后，它将包含读取的字节数。如果有错误，它将包含-errno. 基本上就是 read() 系统调用本身会返回的东西。

#### 顺序

虽然我确实提到， CQE 可以按任何顺序到达，但您可以使用 SQE 排序强制对某些操作进行排序，实际上是将它们链接起来。我不会在本系列文章中讨论排序，但您可以阅读当前的 io_uring 规范参考，以了解如何执行此操作。

> [https://kernel.dk/io_uring.pdf](io_uring-by-example1.md)

#### 提交队列条目（SQE)


提交队列条目比完成队列条目稍微复杂一些，因为它需要足够通用，以表示和处理当今 Linux 可能的各种 I/O 操作。

```c
struct io_uring_sqe {
  __u8  opcode;    /* type of operation for this sqe */
  __u8  flags;    /* IOSQE_ flags */
  __u16  ioprio;    /* ioprio for the request */
  __s32  fd;    /* file descriptor to do IO on */
  __u64  off;    /* offset into file */
  __u64  addr;    /* pointer to buffer or iovecs */
  __u32  len;    /* buffer size or number of iovecs */
  union {
    __kernel_rwf_t  rw_flags;
    __u32    fsync_flags;
    __u16    poll_events;
    __u32    sync_range_flags;
    __u32    msg_flags;
  };
  __u64  user_data;  /* data to be passed back at completion time */
  union {
    __u16  buf_index;  /* index into fixed buffers, if used */
    __u64  __pad2[3];
  };
};
```

我知道这个结构看起来很大。更常用的字段只有几个，这很容易用一个简单的例子来解释，比如我们正在处理的那个：cat。您将使用readv()系统调用读取文件：

- opcode用于指定操作，在我们的例子中，readv() 使用 IORING_OP_READV 常量。
- fd 用于指定我们要读取的文件。
- addr 用于指向 iovec 保存我们为 I/O 分配的缓冲区的地址和长度的结构数组。
- 最后，len用于保存 iovecs 的数组的长度iovecs。


现在这并不太难。您填写这些值，让 io_uring 知道该做什么。您可以将多个 SQE 加入队列，并在您希望内核开始处理您的请求时最终调用 io_uring_enter()。

### io_uring 版本的 cat

让我们看看如何在我们cat程序的 io_uring 版本中实际完成这项工作：

```c
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include <linux/fs.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

/* 如果你的编译失败是因为缺少下面的头文件，
 * 您的内核可能太旧，无法支持 io_uring。
 * */
#include <linux/io_uring.h>

#define QUEUE_DEPTH 1
#define BLOCK_SZ    1024

/* This is x86 specific */
#define read_barrier()  __asm__ __volatile__("":::"memory")
#define write_barrier() __asm__ __volatile__("":::"memory")

struct app_io_sq_ring {
    unsigned *head;
    unsigned *tail;
    unsigned *ring_mask;
    unsigned *ring_entries;
    unsigned *flags;
    unsigned *array;
};

struct app_io_cq_ring {
    unsigned *head;
    unsigned *tail;
    unsigned *ring_mask;
    unsigned *ring_entries;
    struct io_uring_cqe *cqes;
};

struct submitter {
    int ring_fd;
    struct app_io_sq_ring sq_ring;
    struct io_uring_sqe *sqes;
    struct app_io_cq_ring cq_ring;
};

struct file_info {
    off_t file_sz;
    struct iovec iovecs[];      /* Referred by readv/writev */
};

/*
 * 这段代码是在没有io_uring相关系统调用的年代写的
 * 标准 C 库的一部分。所以，我们推出自己的系统调用包装器.
 * */

int io_uring_setup(unsigned entries, struct io_uring_params *p)
{
    return (int) syscall(__NR_io_uring_setup, entries, p);
}

int io_uring_enter(int ring_fd, unsigned int to_submit,
                          unsigned int min_complete, unsigned int flags)
{
    return (int) syscall(__NR_io_uring_enter, ring_fd, to_submit, min_complete,
                   flags, NULL, 0);
}

/*
 * 返回传入其打开文件描述符的文件的大小。
 * 正确处理常规文件和块设备。
 * */

off_t get_file_size(int fd) {
    struct stat st;

    if(fstat(fd, &st) < 0) {
        perror("fstat");
        return -1;
    }
    if (S_ISBLK(st.st_mode)) {
        unsigned long long bytes;
        if (ioctl(fd, BLKGETSIZE64, &bytes) != 0) {
            perror("ioctl");
            return -1;
        }
        return bytes;
    } else if (S_ISREG(st.st_mode))
        return st.st_size;

    return -1;
}

/*
 * io_uring 需要很多设置，看起来很麻烦
 * 所以 io_uring 的作者创建了 liburing，比较好用。
 * 但是，您应该花时间了解此代码。
 * */

int app_setup_uring(struct submitter *s) {
    struct app_io_sq_ring *sring = &s->sq_ring;
    struct app_io_cq_ring *cring = &s->cq_ring;
    struct io_uring_params p;
    void *sq_ptr, *cq_ptr;

    /*
     * 我们需要将 io_uring_params 结构体传递给 io_uring_setup() 去置0初始化。
     * 我们可以设置任何想要的标记。
     * */
    memset(&p, 0, sizeof(p));
    s->ring_fd = io_uring_setup(QUEUE_DEPTH, &p);
    if (s->ring_fd < 0) {
        perror("io_uring_setup");
        return 1;
    }

    /*
     * io_uring 通信通过 2 个共享的内核用户空间环形缓冲区进行，
     * 可以在内核中通过 mmap() 调用映射。 
     * 虽然完成队列是直接映射进去的, 但提交队列里面有个数组，我们也把它映射进* 去
     * */

    int sring_sz = p.sq_off.array + p.sq_entries * sizeof(unsigned);
    int cring_sz = p.cq_off.cqes + p.cq_entries * sizeof(struct io_uring_cqe);

    /* 在内核版本 5.4 及以上,
     * 可以使用单个 mmap() 调用同时完成两个缓冲区的映射。
     * 关于内核版本，可以检查 io_uring_params 的字段，并使用 mask 获取。
     * 如果 IORING_FEAT_SINGLE_MMAP 已设置，我们可以不用第二个 mmap() 去映* 射。
     * */
    if (p.features & IORING_FEAT_SINGLE_MMAP) {
        if (cring_sz > sring_sz) {
            sring_sz = cring_sz;
        }
        cring_sz = sring_sz;
    }

    /* 在提交和完成队列环形缓冲区中映射。
     * 不过，较旧的内核仅映射到提交队列中。
     * */
    sq_ptr = mmap(0, sring_sz, PROT_READ | PROT_WRITE, 
            MAP_SHARED | MAP_POPULATE,
            s->ring_fd, IORING_OFF_SQ_RING);
    if (sq_ptr == MAP_FAILED) {
        perror("mmap");
        return 1;
    }

    if (p.features & IORING_FEAT_SINGLE_MMAP) {
        cq_ptr = sq_ptr;
    } else {
        /* 分别映射到旧内核中的完成队列环形缓冲区 */
        cq_ptr = mmap(0, cring_sz, PROT_READ | PROT_WRITE, 
                MAP_SHARED | MAP_POPULATE,
                s->ring_fd, IORING_OFF_CQ_RING);
        if (cq_ptr == MAP_FAILED) {
            perror("mmap");
            return 1;
        }
    }
    
    /* 将有用的字段保存在全局 app_io_sq_ring 结构中以备后用
     * 简单的一个参考 */
    sring->head = sq_ptr + p.sq_off.head;
    sring->tail = sq_ptr + p.sq_off.tail;
    sring->ring_mask = sq_ptr + p.sq_off.ring_mask;
    sring->ring_entries = sq_ptr + p.sq_off.ring_entries;
    sring->flags = sq_ptr + p.sq_off.flags;
    sring->array = sq_ptr + p.sq_off.array;

    /* 映射到提交队列条目数组 */
    s->sqes = mmap(0, p.sq_entries * sizeof(struct io_uring_sqe),
            PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE,
            s->ring_fd, IORING_OFF_SQES);
    if (s->sqes == MAP_FAILED) {
        perror("mmap");
        return 1;
    }

    /* 将有用的字段保存在全局 app_io_cq_ring 结构中以备后用
     * 简单参考 */
    cring->head = cq_ptr + p.cq_off.head;
    cring->tail = cq_ptr + p.cq_off.tail;
    cring->ring_mask = cq_ptr + p.cq_off.ring_mask;
    cring->ring_entries = cq_ptr + p.cq_off.ring_entries;
    cring->cqes = cq_ptr + p.cq_off.cqes;

    return 0;
}

/*
 * 输出长度为 len 的字符串到 stdout
 * 我们在这里使用缓冲输出以提高效率，
 * 因为我们需要逐个字符地输出。
 * */
void output_to_console(char *buf, int len) {
    while (len--) {
        fputc(*buf++, stdout);
    }
}

/*
 * 从完成队列中读取。
 * 在这个函数中，我们从完成队列中读取完成事件，
 * 得到包含文件数据并将其打印到控制台的数据缓冲区。
 * */

void read_from_cq(struct submitter *s) {
    struct file_info *fi;
    struct app_io_cq_ring *cring = &s->cq_ring;
    struct io_uring_cqe *cqe;
    unsigned head, reaped = 0;

    head = *cring->head;

    do {
        read_barrier();
        /*
         * 请记住，这是一个环形缓冲区。如果头==尾，则表示
         * 缓冲区为空。
         * */
        if (head == *cring->tail)
            break;

        /* 获取条目 */
        cqe = &cring->cqes[head & *s->cq_ring.ring_mask];
        fi = (struct file_info*) cqe->user_data;
        if (cqe->res < 0)
            fprintf(stderr, "Error: %s\n", strerror(abs(cqe->res)));

        int blocks = (int) fi->file_sz / BLOCK_SZ;
        if (fi->file_sz % BLOCK_SZ) blocks++;

        for (int i = 0; i < blocks; i++)
            output_to_console(fi->iovecs[i].iov_base, fi->iovecs[i].iov_len);

        head++;
    } while (1);

    *cring->head = head;
    write_barrier();
}
/*
 * 提交到提交队列。
 * 在这个函数中，我们将请求提交到提交队列。你可以提交
 * 我们的将是 readv() 请求，通过 IORING_OP_READV 指定。
 *
 * */
int submit_to_sq(char *file_path, struct submitter *s) {
    struct file_info *fi;

    int file_fd = open(file_path, O_RDONLY);
    if (file_fd < 0 ) {
        perror("open");
        return 1;
    }

    struct app_io_sq_ring *sring = &s->sq_ring;
    unsigned index = 0, current_block = 0, tail = 0, next_tail = 0;

    off_t file_sz = get_file_size(file_fd);
    if (file_sz < 0)
        return 1;
    off_t bytes_remaining = file_sz;
    int blocks = (int) file_sz / BLOCK_SZ;
    if (file_sz % BLOCK_SZ) blocks++;

    fi = malloc(sizeof(*fi) + sizeof(struct iovec) * blocks);
    if (!fi) {
        fprintf(stderr, "Unable to allocate memory\n");
        return 1;
    }
    fi->file_sz = file_sz;

    /*
     * 对于我们需要读取的文件的每个块，我们分配一个iovec struct
     * 索引到 iovecs 数组中。这个数组作为一部分提交传入。
     * 如果你不明白这一点，那么你需要去
     * 了解一下 readv() 和 writev() 系统调用的工作方式。
     * */
    while (bytes_remaining) {
        off_t bytes_to_read = bytes_remaining;
        if (bytes_to_read > BLOCK_SZ)
            bytes_to_read = BLOCK_SZ;

        fi->iovecs[current_block].iov_len = bytes_to_read;

        void *buf;
        if( posix_memalign(&buf, BLOCK_SZ, BLOCK_SZ)) {
            perror("posix_memalign");
            return 1;
        }
        fi->iovecs[current_block].iov_base = buf;

        current_block++;
        bytes_remaining -= bytes_to_read;
    }

    /* 将我们的提交队列条目添加到 SQE 环形缓冲区的尾部 */
    next_tail = tail = *sring->tail;
    next_tail++;
    read_barrier();
    index = tail & *s->sq_ring.ring_mask;
    struct io_uring_sqe *sqe = &s->sqes[index];
    sqe->fd = file_fd;
    sqe->flags = 0;
    sqe->opcode = IORING_OP_READV;
    sqe->addr = (unsigned long) fi->iovecs;
    sqe->len = blocks;
    sqe->off = 0;
    sqe->user_data = (unsigned long long) fi;
    sring->array[index] = index;
    tail = next_tail;

    /* 更新尾部以便内核可以看到它 */
    if(*sring->tail != tail) {
        *sring->tail = tail;
        write_barrier();
    }

    /*
     * 告诉内核我们已经用 io_uring_enter() 提交了事件。
     * 们还传入了 IOURING_ENTER_GETEVENTS 标志，这会导致
     * io_uring_enter() 调用等待 min_complete 事件完成后返回。
     * */
    int ret =  io_uring_enter(s->ring_fd, 1,1,
            IORING_ENTER_GETEVENTS);
    if(ret < 0) {
        perror("io_uring_enter");
        return 1;
    }

    return 0;
}

int main(int argc, char *argv[]) {
    struct submitter *s;

    if (argc < 2) {
        fprintf(stderr, "Usage: %s <filename>\n", argv[0]);
        return 1;
    }

    s = malloc(sizeof(*s));
    if (!s) {
        perror("malloc");
        return 1;
    }
    memset(s, 0, sizeof(*s));

    if(app_setup_uring(s)) {
        fprintf(stderr, "Unable to setup uring!\n");
        return 1;
    }

    for (int i = 1; i < argc; i++) {
        if(submit_to_sq(argv[i], s)) {
            fprintf(stderr, "Error reading file\n");
            return 1;
        }
        read_from_cq(s);
    }

    return 0;
}
```

使用 gcc 进行编译的时候，需要加上 `-luring` 标识。

#### 初始设置

从 main() 开始，我们调用 app_setup_uring()，它执行我们使用 io_uring 所需的初始化工作。首先，我们使用我们需要的队列深度，和初始化为零的结构实例 io_uring_params 调用系统调用 io_uring_setup() 。当调用返回时，内核将填充此结构成员中的值。io_uring_params 是这样的：

```c
struct io_uring_params {
  __u32 sq_entries;
  __u32 cq_entries;
  __u32 flags;
  __u32 sq_thread_cpu;
  __u32 sq_thread_idle;
  __u32 resv[5];
  struct io_sqring_offsets sq_off;
  struct io_cqring_offsets cq_off;
};
```

在将此结构作为 io_uring_setup() 系统调用的一部分传递之前，您唯一可以指定的是flags结构成员，但在此示例中，我们没有要传递的标志。此外，在本例中，我们一个接一个地处理文件，我们不会做任何并行 I/O，因为这是一个简单的例子，主要是为了理解io_uring. 为此，我们将队列深度设置为一。

来自io_uring_param结构的返回值、文件描述符和其他字段随后将用于调用 mmap() ，将两个环形缓冲区和一个提交队列条目数组映射到用户空间。我删除了一些周围的代码以专注于mmap()s。

```c
    /* 在提交和完成队列环形缓冲区中映射。
     * 不过，较旧的内核仅映射到提交队列中。
     * */
    sq_ptr = mmap(0, sring_sz, PROT_READ | PROT_WRITE, 
            MAP_SHARED | MAP_POPULATE,
            s->ring_fd, IORING_OFF_SQ_RING);
    if (sq_ptr == MAP_FAILED) {
        perror("mmap");
        return 1;
    }

    if (p.features & IORING_FEAT_SINGLE_MMAP) {
        cq_ptr = sq_ptr;
    } else {
        /* 分别映射到旧内核中的完成队列环形缓冲区 */
        cq_ptr = mmap(0, cring_sz, PROT_READ | PROT_WRITE, 
                MAP_SHARED | MAP_POPULATE,
                s->ring_fd, IORING_OFF_CQ_RING);
        if (cq_ptr == MAP_FAILED) {
            perror("mmap");
            return 1;
        }
    }
    
    ....
    
    /* 映射到提交队列条目数组 */
    s->sqes = mmap(0, p.sq_entries * sizeof(struct io_uring_sqe),
            PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE,
            s->ring_fd, IORING_OFF_SQES);
    if (s->sqes == MAP_FAILED) {
        perror("mmap");
        return 1;
    }
```

我们将重要的细节保存在我们的 app_io_sq_ring 结构中，以供日后参考。虽然我们分别映射了两个用于提交和完成的环形缓冲区，但您可能想知道第二个映射是做什么用的:虽然完成队列环直接索引 CQE 的共享数组，但提交环之间有一个间接数组。提交端环形缓冲区是该数组的索引，该数组又包含 SQE 的索引。这对于将提交请求嵌入内部数据结构的某些应用程序很有用。这种设置允许他们一次性提交多个提交条目，同时让他们更容易采用 io_uring。

注意：在内核版本 5.4 及更高版本中，单个 mmap() 映射同时映射提交和完成队列。然而，在较旧的内核中，它们需要单独映射。您可以通过检查 IORING_FEAT_SINGLE_MMAP功能标志，来检查内核将两个队列映射到一个队列的能力，而不是检查内核版本，就像我们在上面的代码中所做的那样。

#### 处理共享的环形缓冲区

在常规编程中，我们习惯于处理用户空间和内核之间非常清晰的接口：系统调用。然而，系统调用确实有成本，并且对于像 那样的高性能接口io_uring，希望尽可能多地取消它们。前面我们看到，使用 io_uring 允许我们批量处理许多 I/O 请求并对io_uring_enter() 系统调用进行一次调用，而不是像通常那样进行多次系统调用。在轮询模式下，甚至不需要调用。

从用户空间读取或更新共享环形缓冲区时，需要注意确保读取时看到的是最新数据，更新后“刷新”或“同步”写入，以便内核会看到您的更新。这是因为 CPU 可以重新排序读取和写入，编译器也可以。当这发生在同一 CPU 上时，这通常不是问题。但是在 io_uring 中，当在两个不同的上下文（用户空间和内核）中涉及共享缓冲区时，在上下文切换后，它们可以在不同的 CPU 上运行。您需要从用户空间确保在读取之前，旧的写入是可见的。或者，当您在 SQE 中填写详细信息并更新提交环形缓冲区的尾部时，您希望确保对 SQE 成员所做的写入，在更新环形缓冲区尾部的写入之前是按顺序的。如果这些写入没有按顺序的，内核可能会看到尾部更新，但是当它读取 SQE 时，它可能找不到它读取时需要的所有数据。在轮询模式下，内核自动发现尾部的变化，这会成为一个真正的问题。这完全是因为 CPU 和编译器会重新排序读取和写入，以进行优化。

#### 读取完成队列条目

与往常一样，我们首先处理事情的完成方面，因为它比提交方面更简单。对于完成事件，内核将 CQE 添加到环形缓冲区并更新尾部，而我们在用户空间从头部读取。与任何环形缓冲区一样，如果头部和尾部相等，则表示环形缓冲区为空。看看下面的代码：

```c
unsigned head;
head = cqring->head;
read_barrier(); /* ensure previous writes are visible */
if (head != cqring->tail) {
    /* There is data available in the ring buffer */
    struct io_uring_cqe *cqe;
    unsigned index;
    index = head & (cqring->mask);
    cqe = &cqring->cqes[index];
    /* process completed cqe here */
     ...
    /* we've now consumed this entry */
    head++;
}
cqring->head = head;
write_barrier();
```

要获取头部的索引，应用程序需要使用环形缓冲区的大小掩码来运算头部。请记住，上面代码中的任何行都可以在上下文切换后运行。所以，就在比较之前，我们有一个read_barrier()。这样，如果内核确实更新了尾部，我们可以在if语句中将其作为比较的一部分读取。一旦我们获得 CQE 并处理它，我们就会更新头部，让内核知道我们已经消耗了环形缓冲区中的一个条目。最后的 write_barrier() 确保我们所做的写入变得可见，以便内核知道它。

#### 提交

提交与阅读完成相反。在完成时内核将条目添加到尾部，我们从环形缓冲区的头部读取条目，但在提交时，我们添加到尾部，内核从环形缓冲区的头部读取条目。

```c
struct io_uring_sqe *sqe;
unsigned tail, index;
tail = sqring->tail;
index = tail & (*sqring->ring_mask);
sqe = &sqring→sqes[index];
/* this function call fills in the SQE details for this IO request */
app_init_io(sqe);
/* fill the SQE index into the SQ ring array */
sqring->array[index] = index;
tail++;
write_barrier();
sqring->tail = tail;
write_barrier();
```

在上面的代码片段中，app_init_io() 函数填写提交请求的详细信息。在更新尾部之前，我们有一个 write_barrier() 来确保在更新尾部之前之前的写入是有序的。然后我们更新尾部, 并再次调用 write_barrier() 以确保我们的更新被内核看到。