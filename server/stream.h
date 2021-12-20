#ifndef STREAM_H
#define STREAM_H

class stream_base
{
    int fd;
};

class read_awaitable: stream_base
{

};

class write_awaitable: stream_base
{

};

#endif
