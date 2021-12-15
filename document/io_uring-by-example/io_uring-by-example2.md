# io_uring 动手实践 part2: liburing

## 使用 liburing 的 cat

使用 io_uring 构建像读取文件的程序这样简单的东西可能并不像 io_uring 那么直观. 事实证明，它比使用同步 I/O 读取文件的程序有更多的代码。但是，如果您分析 cat_uring 的代码，您会发现大部分代码都有样板代码，可以很容易地将其隐藏在单独的文件中，并且不影响应用程序逻辑。在任何情况下，我们都是有目的地学习 io_uring 的低级细节，以便更好地理解它是如何工作的。但是，如果您打算在正在构建的实际应用程序中使用 io_uring，您可能不应该直接使用原始接口。您应该改用 liburing，这是一个很好的高级包装器.

现在让我们看看如何使用 liburing 构建 cat_uring. 我们将称之为cat_liburing。

```c
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <liburing.h>
#include <stdlib.h>

#define QUEUE_DEPTH 1
#define BLOCK_SZ    1024

...

/*
 * Wait for a completion to be available, fetch the data from
 * the readv operation and print it to the console.
 * */

int get_completion_and_print(struct io_uring *ring) {
    struct io_uring_cqe *cqe;
    int ret = io_uring_wait_cqe(ring, &cqe);
    if (ret < 0) {
        perror("io_uring_wait_cqe");
        return 1;
    }
    if (cqe->res < 0) {
        fprintf(stderr, "Async readv failed.\n");
        return 1;
    }
    struct file_info *fi = io_uring_cqe_get_data(cqe);
    int blocks = (int) fi->file_sz / BLOCK_SZ;
    if (fi->file_sz % BLOCK_SZ) blocks++;
    for (int i = 0; i < blocks; i ++)
        output_to_console(fi->iovecs[i].iov_base, fi->iovecs[i].iov_len);

    io_uring_cqe_seen(ring, cqe);
    return 0;
}

/*
 * Submit the readv request via liburing
 * */

int submit_read_request(char *file_path, struct io_uring *ring) {
    int file_fd = open(file_path, O_RDONLY);
    if (file_fd < 0) {
        perror("open");
        return 1;
    }
    off_t file_sz = get_file_size(file_fd);
    off_t bytes_remaining = file_sz;
    off_t offset = 0;
    int current_block = 0;
    int blocks = (int) file_sz / BLOCK_SZ;
    if (file_sz % BLOCK_SZ) blocks++;
    struct file_info *fi = malloc(sizeof(*fi) +
                                          (sizeof(struct iovec) * blocks));

    /*
     * For each block of the file we need to read, we allocate an iovec struct
     * which is indexed into the iovecs array. This array is passed in as part
     * of the submission. If you don't understand this, then you need to look
     * up how the readv() and writev() system calls work.
     * */
    while (bytes_remaining) {
        off_t bytes_to_read = bytes_remaining;
        if (bytes_to_read > BLOCK_SZ)
            bytes_to_read = BLOCK_SZ;

        offset += bytes_to_read;
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
    fi->file_sz = file_sz;

    /* Get an SQE */
    struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
    /* Setup a readv operation */
    io_uring_prep_readv(sqe, file_fd, fi->iovecs, blocks, 0);
    /* Set user data */
    io_uring_sqe_set_data(sqe, fi);
    /* Finally, submit the request */
    io_uring_submit(ring);

    return 0;
}

int main(int argc, char *argv[]) {
    struct io_uring ring;

    if (argc < 2) {
        fprintf(stderr, "Usage: %s [file name] <[file name] ...>\n",
                argv[0]);
        return 1;
    }

    /* Initialize io_uring */
    io_uring_queue_init(QUEUE_DEPTH, &ring, 0);

    for (int i = 1; i < argc; i++) {
        int ret = submit_read_request(argv[i], &ring);
        if (ret) {
            fprintf(stderr, "Error reading file: %s\n", argv[i]);
            return 1;
        }
        get_completion_and_print(&ring);
    }

    /* Call the clean-up function. */
    io_uring_queue_exit(&ring);
    return 0;
}
```

让我们比较每个实现所花费的行数：

普通 cat：~120 行
原始 io_uring 的 cat：~360 行
使用 liburing 的 cat：~160 行
现在，使用 liburing， 随着所有样板代码的消失，逻辑就会变得明显起来。让我们快速浏览一下。我们这样初始化 io_uring：

```c
io_uring_queue_init(QUEUE_DEPTH, &ring, 0);
```

在函数 submit_read_request() 中，我们获得一个 SQE，为readv操作准备它，并提交它。

```c
    /* Get an SQE */
    struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
    /* Setup a readv operation */
    io_uring_prep_readv(sqe, file_fd, fi->iovecs, blocks, 0);
    /* Set user data */
    io_uring_sqe_set_data(sqe, fi);
    /* Finally, submit the request */
    io_uring_submit(ring);
```

我们等待事件完成并获取我们在提交端设置的用户数据，如下所示：

```c
    struct io_uring_cqe *cqe;
    int ret = io_uring_wait_cqe(ring, &cqe);
    struct file_info *fi = io_uring_cqe_get_data(cqe);
```

当然，与使用原始接口相比，这使用起来要简单得多。