/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2020 Winlin
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <srs_app_hybrid.hpp>

#include <srs_app_server.hpp>
#include <srs_app_config.hpp>
#include <srs_kernel_error.hpp>
#include <srs_service_st.hpp>
#include <srs_app_utility.hpp>

using namespace std;

extern SrsPps* _srs_pps_cids_get;
extern SrsPps* _srs_pps_cids_set;

extern SrsPps* _srs_pps_timer;
extern SrsPps* _srs_pps_pub;
extern SrsPps* _srs_pps_conn;
extern SrsPps* _srs_pps_dispose;

ISrsHybridServer::ISrsHybridServer()
{
}

ISrsHybridServer::~ISrsHybridServer()
{
}

SrsServerAdapter::SrsServerAdapter()
{
    srs = new SrsServer();
}

SrsServerAdapter::~SrsServerAdapter()
{
    srs_freep(srs);
}

srs_error_t SrsServerAdapter::initialize()
{
    srs_error_t err = srs_success;
    return err;
}

srs_error_t SrsServerAdapter::run()
{
    srs_error_t err = srs_success;

    // Initialize the whole system, set hooks to handle server level events.
    if ((err = srs->initialize(NULL)) != srs_success) {
        return srs_error_wrap(err, "server initialize");
    }

    if ((err = srs->initialize_st()) != srs_success) {
        return srs_error_wrap(err, "initialize st");
    }

    if ((err = srs->acquire_pid_file()) != srs_success) {
        return srs_error_wrap(err, "acquire pid file");
    }

    if ((err = srs->initialize_signal()) != srs_success) {
        return srs_error_wrap(err, "initialize signal");
    }

    if ((err = srs->listen()) != srs_success) {
        return srs_error_wrap(err, "listen");
    }

    if ((err = srs->register_signal()) != srs_success) {
        return srs_error_wrap(err, "register signal");
    }

    if ((err = srs->http_handle()) != srs_success) {
        return srs_error_wrap(err, "http handle");
    }

    if ((err = srs->ingest()) != srs_success) {
        return srs_error_wrap(err, "ingest");
    }

    if ((err = srs->start()) != srs_success) {
        return srs_error_wrap(err, "start");
    }

    return err;
}

void SrsServerAdapter::stop()
{
}

SrsServer* SrsServerAdapter::instance()
{
    return srs;
}

SrsHybridServer::SrsHybridServer()
{
    timer_ = NULL;
}

SrsHybridServer::~SrsHybridServer()
{
    vector<ISrsHybridServer*>::iterator it;
    for (it = servers.begin(); it != servers.end(); ++it) {
        ISrsHybridServer* server = *it;
        srs_freep(server);
    }
    servers.clear();
}

void SrsHybridServer::register_server(ISrsHybridServer* svr)
{
    servers.push_back(svr);
}

srs_error_t SrsHybridServer::initialize()
{
    srs_error_t err = srs_success;

    // init st
    if ((err = srs_st_init()) != srs_success) {
        return srs_error_wrap(err, "initialize st failed");
    }

    if ((err = setup_ticks()) != srs_success) {
        return srs_error_wrap(err, "tick");
    }

    vector<ISrsHybridServer*>::iterator it;
    for (it = servers.begin(); it != servers.end(); ++it) {
        ISrsHybridServer* server = *it;

        if ((err = server->initialize()) != srs_success) {
            return srs_error_wrap(err, "init server");
        }
    }

    return err;
}

srs_error_t SrsHybridServer::run()
{
    srs_error_t err = srs_success;

    vector<ISrsHybridServer*>::iterator it;
    for (it = servers.begin(); it != servers.end(); ++it) {
        ISrsHybridServer* server = *it;

        if ((err = server->run()) != srs_success) {
            return srs_error_wrap(err, "run server");
        }
    }

    // Wait for all server to quit.
    srs_thread_exit(NULL);

    return err;
}

void SrsHybridServer::stop()
{
    vector<ISrsHybridServer*>::iterator it;
    for (it = servers.begin(); it != servers.end(); ++it) {
        ISrsHybridServer* server = *it;
        server->stop();
    }
}

SrsServerAdapter* SrsHybridServer::srs()
{
    for (vector<ISrsHybridServer*>::iterator it = servers.begin(); it != servers.end(); ++it) {
        if (dynamic_cast<SrsServerAdapter*>(*it)) {
            return dynamic_cast<SrsServerAdapter*>(*it);
        }
    }
    return NULL;
}

srs_error_t SrsHybridServer::setup_ticks()
{
    srs_error_t err = srs_success;

    timer_ = new SrsHourGlass("hybrid", this, 1 * SRS_UTIME_SECONDS);

    if ((err = timer_->tick(1, 5 * SRS_UTIME_SECONDS)) != srs_success) {
        return srs_error_wrap(err, "tick");
    }

    if ((err = timer_->start()) != srs_success) {
        return srs_error_wrap(err, "start");
    }

    return err;
}

srs_error_t SrsHybridServer::notify(int event, srs_utime_t interval, srs_utime_t tick)
{
    srs_error_t err = srs_success;

    // Show statistics for RTC server.
    SrsProcSelfStat* u = srs_get_self_proc_stat();
    // Resident Set Size: number of pages the process has in real memory.
    int memory = (int)(u->rss * 4 / 1024);

    _srs_pps_cids_get->update(); _srs_pps_cids_set->update();
    _srs_pps_timer->update(); _srs_pps_pub->update(); _srs_pps_conn->update(); _srs_pps_dispose->update();

    srs_trace("Hybrid cpu=%.2f%%,%dMB, cid=%d,%d, timer=%d,%d,%d, free=%d",
        u->percent * 100, memory,
        _srs_pps_cids_get->r10s(), _srs_pps_cids_set->r10s(),
        _srs_pps_timer->r10s(), _srs_pps_pub->r10s(), _srs_pps_conn->r10s(),
        _srs_pps_dispose->r10s()
    );

    return err;
}

SrsHybridServer* _srs_hybrid = new SrsHybridServer();

