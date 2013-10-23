/*
The MIT License (MIT)

Copyright (c) 2013 winlin

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include <srs_core_socket.hpp>

#include <srs_core_error.hpp>

SrsSocket::SrsSocket(st_netfd_t client_stfd)
{
    stfd = client_stfd;
	recv_timeout = ST_UTIME_NO_TIMEOUT;
	send_timeout = ST_UTIME_NO_TIMEOUT;
}

SrsSocket::~SrsSocket()
{
}

void SrsSocket::set_recv_timeout(int timeout_ms)
{
	recv_timeout = timeout_ms * 1000;
}

void SrsSocket::set_send_timeout(int timeout_ms)
{
	send_timeout = timeout_ms * 1000;
}

int SrsSocket::read(const void* buf, size_t size, ssize_t* nread)
{
    int ret = ERROR_SUCCESS;
    
    *nread = st_read(stfd, (void*)buf, size, recv_timeout);
    
    // On success a non-negative integer indicating the number of bytes actually read is returned 
    // (a value of 0 means the network connection is closed or end of file is reached).
    if (*nread <= 0) {
		if (errno == ETIME) {
			return ERROR_SOCKET_TIMEOUT;
		}
		
        if (*nread == 0) {
            errno = ECONNRESET;
        }
        
        ret = ERROR_SOCKET_READ;
    }
        
    return ret;
}

int SrsSocket::read_fully(const void* buf, size_t size, ssize_t* nread)
{
    int ret = ERROR_SUCCESS;
    
    *nread = st_read_fully(stfd, (void*)buf, size, recv_timeout);
    
    // On success a non-negative integer indicating the number of bytes actually read is returned 
    // (a value less than nbyte means the network connection is closed or end of file is reached)
    if (*nread != (ssize_t)size) {
		if (errno == ETIME) {
			return ERROR_SOCKET_TIMEOUT;
		}
		
        if (*nread >= 0) {
            errno = ECONNRESET;
        }
        
        ret = ERROR_SOCKET_READ_FULLY;
    }
    
    return ret;
}

int SrsSocket::write(const void* buf, size_t size, ssize_t* nwrite)
{
    int ret = ERROR_SUCCESS;
    
    *nwrite = st_write(stfd, (void*)buf, size, send_timeout);
    
    if (*nwrite <= 0) {
		if (errno == ETIME) {
			return ERROR_SOCKET_TIMEOUT;
		}
		
        ret = ERROR_SOCKET_WRITE;
    }
        
    return ret;
}

int SrsSocket::writev(const iovec *iov, int iov_size, ssize_t* nwrite)
{
    int ret = ERROR_SUCCESS;
    
    *nwrite = st_writev(stfd, iov, iov_size, send_timeout);
    
    if (*nwrite <= 0) {
		if (errno == ETIME) {
			return ERROR_SOCKET_TIMEOUT;
		}
		
        ret = ERROR_SOCKET_WRITE;
    }
    
    return ret;
}

