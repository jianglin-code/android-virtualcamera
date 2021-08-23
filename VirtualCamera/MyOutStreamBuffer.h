#ifndef COMMUNICATION_MYOUTSTREAMBUFFER_H
#define COMMUNICATION_MYOUTSTREAMBUFFER_H

#include <iostream>
#include <streambuf>

#include "fflog.h"

class MyOutStreamBuffer : public std::streambuf {
    enum {
        BUFFER_SIZE = 255,
    };

public:
    MyOutStreamBuffer() {
        buffer_[BUFFER_SIZE] = '\0';
        setp(buffer_, buffer_ + BUFFER_SIZE - 1);
    }

    ~MyOutStreamBuffer() {
        sync();
    }

protected:
    virtual int_type overflow(int_type c) {
        if (c != EOF) {
            *pptr() = c;
            pbump(1);
        }
        flush_buffer();
        return c;
    }

    virtual int sync() {
        flush_buffer();
        return 0;
    }

private:
    int flush_buffer() {
        int len = int(pptr() - pbase());
        if (len <= 0)
            return 0;

        if (len <= BUFFER_SIZE)
            buffer_[len] = '\0';

        printf("%s", buffer_);

        pbump(-len);
        return len;
    }

private:
    char buffer_[BUFFER_SIZE + 1];
};

#endif //COMMUNICATION_MYOUTSTREAMBUFFER_H
