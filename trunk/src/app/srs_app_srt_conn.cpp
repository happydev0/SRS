//
// Copyright (c) 2013-2021 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#include <srs_app_srt_conn.hpp>

using namespace std;

#include <srs_kernel_buffer.hpp>
#include <srs_kernel_flv.hpp>
#include <srs_kernel_stream.hpp>
#include <srs_core_autofree.hpp>
#include <srs_rtmp_stack.hpp>
#include <srs_protocol_srt.hpp>
#include <srs_app_config.hpp>
#include <srs_app_http_hooks.hpp>
#include <srs_app_pithy_print.hpp>
#include <srs_app_srt_server.hpp>
#include <srs_app_srt_source.hpp>

SrsSrtConnection::SrsSrtConnection(SRTSOCKET srt_fd)
{
    srt_fd_ = srt_fd;
    srt_skt_ = new SrsSrtSocket(_srt_eventloop->poller(), srt_fd_);
}

SrsSrtConnection::~SrsSrtConnection()
{
    srs_freep(srt_skt_);
}

srs_error_t SrsSrtConnection::initialize()
{
    srs_error_t err = srs_success;
    return err;
}

void SrsSrtConnection::set_recv_timeout(srs_utime_t tm)
{
    srt_skt_->set_recv_timeout(tm);
}

srs_utime_t SrsSrtConnection::get_recv_timeout()
{
    return srt_skt_->get_recv_timeout();
}

srs_error_t SrsSrtConnection::read_fully(void* buf, size_t size, ssize_t* nread)
{
    return srs_error_new(ERROR_SRT_CONN, "unsupport method");
}

int64_t SrsSrtConnection::get_recv_bytes()
{
    return srt_skt_->get_recv_bytes();
}

int64_t SrsSrtConnection::get_send_bytes()
{
    return srt_skt_->get_send_bytes();
}

srs_error_t SrsSrtConnection::read(void* buf, size_t size, ssize_t* nread)
{
    return srt_skt_->recvmsg(buf, size, nread);
}

void SrsSrtConnection::set_send_timeout(srs_utime_t tm)
{
    srt_skt_->set_send_timeout(tm);
}

srs_utime_t SrsSrtConnection::get_send_timeout()
{
    return srt_skt_->get_send_timeout();
}

srs_error_t SrsSrtConnection::write(void* buf, size_t size, ssize_t* nwrite)
{
    return srt_skt_->sendmsg(buf, size, nwrite);
}

srs_error_t SrsSrtConnection::writev(const iovec *iov, int iov_size, ssize_t* nwrite)
{
    return srs_error_new(ERROR_SRT_CONN, "unsupport method");
}

SrsSrtRecvThread::SrsSrtRecvThread(SrsSrtConnection* srt_conn)
{
    srt_conn_ = srt_conn;
    trd_ = new SrsSTCoroutine("srt-recv", this, _srs_context->get_id());
    recv_err_ = srs_success;
}

SrsSrtRecvThread::~SrsSrtRecvThread()
{
    srs_freep(trd_);
    srs_error_reset(recv_err_);
}

srs_error_t SrsSrtRecvThread::cycle()
{
    srs_error_t err = srs_success;

    if ((err = do_cycle()) != srs_success) {
        recv_err_ = srs_error_copy(err);
    }

    return err;
}

srs_error_t SrsSrtRecvThread::do_cycle()
{
    srs_error_t err = srs_success;

    while (true) {
        if ((err = trd_->pull()) != srs_success) {
            return srs_error_wrap(err, "srt: thread quit");
        }

        char buf[1316];
        ssize_t nb = 0;
        if ((err = srt_conn_->read(buf, sizeof(buf), &nb)) != srs_success) {
            if (srs_error_code(err) != ERROR_SRT_TIMEOUT) {
                return srs_error_wrap(err, "srt read");
            }
        }
    }

    return err;
}

srs_error_t SrsSrtRecvThread::start()
{
    srs_error_t err = srs_success;

    if ((err = trd_->start()) != srs_success) {
        return srs_error_wrap(err, "start srt recv thread");
    }

    return err;
}

srs_error_t SrsSrtRecvThread::get_recv_err()
{
    return srs_error_copy(recv_err_);
}

SrsMpegtsSrtConn::SrsMpegtsSrtConn(SrsSrtServer* srt_server, SRTSOCKET srt_fd, std::string ip, int port)
{
    // Create a identify for this client.
    _srs_context->set_id(_srs_context->generate_id());

    srt_server_ = srt_server;

    srt_fd_ = srt_fd;
    srt_conn_ = new SrsSrtConnection(srt_fd_);
    clock_ = new SrsWallClock();
    kbps_ = new SrsKbps(clock_);
    kbps_->set_io(srt_conn_, srt_conn_);
    ip_ = ip;
    port_ = port;

    trd_ = new SrsSTCoroutine("ts-srt", this, _srs_context->get_id());

    srt_source_ = NULL;
    req_ = new SrsRequest();
    mode_ = SrtModePull;
}

SrsMpegtsSrtConn::~SrsMpegtsSrtConn()
{
    srs_freep(trd_);

    srs_freep(kbps_);
    srs_freep(clock_);

    srs_freep(srt_conn_);

    srs_freep(req_);
}

std::string SrsMpegtsSrtConn::desc()
{
    return "srt-ts-conn";
}

void SrsMpegtsSrtConn::remark(int64_t* in, int64_t* out)
{
    // TODO: FIXME: no impl currently.
    kbps_->remark(in, out);
}

srs_error_t SrsMpegtsSrtConn::start()
{
    srs_error_t err = srs_success;

    if ((err = trd_->start()) != srs_success) {
        return srs_error_wrap(err, "coroutine");
    }

    return err;
}

std::string SrsMpegtsSrtConn::remote_ip()
{
    return ip_;
}

const SrsContextId& SrsMpegtsSrtConn::get_id()
{
    return trd_->cid();
}

srs_error_t SrsMpegtsSrtConn::cycle()
{
    srs_error_t err = srs_success;

    err = do_cycle();

    // Notify manager to remove it.
    // Note that we create this object, so we use manager to remove it.
    srt_server_->remove(this);

    // success.
    if (err == srs_success) {
        srs_trace("srt client finished.");
        return err;
    }

    srs_error("srt serve error %s", srs_error_desc(err).c_str());
    srs_freep(err);
    return srs_success;
}

srs_error_t SrsMpegtsSrtConn::do_cycle()
{
    srs_error_t err = srs_success;

    if ((err = fetch_or_create_source()) != srs_success) {
        return srs_error_wrap(err, "fetch or create srt source");
    }

    if ((err = http_hooks_on_connect()) != srs_success) {
        return srs_error_wrap(err, "on connect");
    }

    if (mode_ == SrtModePush) {
        err = publishing();
    } else if (mode_ == SrtModePull) {
        err = playing();
    }
    
    http_hooks_on_close();
    
    return err;
}

srs_error_t SrsMpegtsSrtConn::fetch_or_create_source()
{
    srs_error_t err = srs_success;

    string streamid = "";
    if ((err = srs_srt_get_streamid(srt_fd_, streamid)) != srs_success) {
        return srs_error_wrap(err, "get srt streamid");
    }

    // Must have streamid, because srt ts packet will convert to rtmp or rtc.
    if (streamid.empty()) {
        return srs_error_new(ERROR_SRT_CONN, "empty srt streamid");
    }

    // Detect streamid of srt to request.
    if (! srs_srt_streamid_to_request(streamid, mode_, req_)) {
        return srs_error_new(ERROR_SRT_CONN, "invalid srt streamid=%s", streamid.c_str());
    }

    if (! _srs_config->get_srt_enabled(req_->vhost)) {
        return srs_error_new(ERROR_SRT_CONN, "srt disabled, vhost=%s", req_->vhost.c_str());
    }

    srs_trace("@srt, streamid=%s, stream_url=%s, vhost=%s, app=%s, stream=%s, param=%s",
        streamid.c_str(), req_->get_stream_url().c_str(), req_->vhost.c_str(), req_->app.c_str(), req_->stream.c_str(), req_->param.c_str());

    if ((err = _srs_srt_sources->fetch_or_create(req_, &srt_source_)) != srs_success) {
        return srs_error_wrap(err, "fetch srt source");
    }

    return err;
}

srs_error_t SrsMpegtsSrtConn::publishing()
{
    srs_error_t err = srs_success;
    
    if ((err = http_hooks_on_publish()) != srs_success) {
        return srs_error_wrap(err, "srt: callback on publish");
    }
    
    if ((err = acquire_publish()) == srs_success) {
        err = do_publishing();
        release_publish();
    }
    
    http_hooks_on_unpublish();
    
    return err;
}

srs_error_t SrsMpegtsSrtConn::playing()
{
    srs_error_t err = srs_success;

    if ((err = http_hooks_on_play()) != srs_success) {
        return srs_error_wrap(err, "rtmp: callback on play");
    }
    
    err = do_playing();
    http_hooks_on_stop();
    
    return err;
}

srs_error_t SrsMpegtsSrtConn::acquire_publish()
{
    srs_error_t err = srs_success;

    // Check srt stream is busy.
    if (! srt_source_->can_publish()) {
        return srs_error_new(ERROR_SRT_SOURCE_BUSY, "srt stream %s busy", req_->get_stream_url().c_str());
    }

    if (_srs_config->get_srt_to_rtmp(req_->vhost)) {
        // Check rtmp stream is busy.
        SrsLiveSource *live_source = _srs_sources->fetch(req_);
        if (live_source && !live_source->can_publish(false)) {
            return srs_error_new(ERROR_SYSTEM_STREAM_BUSY, "live_source stream %s busy", req_->get_stream_url().c_str());
        }

        if ((err = _srs_sources->fetch_or_create(req_, _srs_hybrid->srs()->instance(), &live_source)) != srs_success) {
            return srs_error_wrap(err, "create source");
        }

        SrsRtmpFromSrtBridge *bridger = new SrsRtmpFromSrtBridge(live_source);
        if ((err = bridger->initialize(req_)) != srs_success) {
            srs_freep(bridger);
            return srs_error_wrap(err, "create bridger");
        }

        srt_source_->set_bridger(bridger);
    }

    if ((err = srt_source_->on_publish()) != srs_success) {
        return srs_error_wrap(err, "srt source publish");
    }

    return err;
}

void SrsMpegtsSrtConn::release_publish()
{
    srt_source_->on_unpublish();
}

srs_error_t SrsMpegtsSrtConn::do_publishing()
{
    srs_error_t err = srs_success;

    SrsPithyPrint* pprint = SrsPithyPrint::create_srt_publish();
    SrsAutoFree(SrsPithyPrint, pprint);

    int nb_packets = 0;

    // Max udp packet size equal to 1500.
    char buf[1500];
    while (true) {
        if ((err = trd_->pull()) != srs_success) {
            return srs_error_wrap(err, "srt: thread quit");
        }

        pprint->elapse();

        // reportable
        if (pprint->can_print()) {
            SrsSrtStat s;
            if ((err = s.fetch(srt_fd_, true)) != srs_success) {
                srs_freep(err);
            } else {
                srs_trace("<- " SRS_CONSTS_LOG_SRT_PUBLISH " Transport Stats # pktRecv=%" PRId64 ", pktRcvLoss=%d, pktRcvRetrans=%d, pktRcvDrop=%d",
                    s.pktRecv(), s.pktRcvLoss(), s.pktRcvRetrans(), s.pktRcvDrop());
            }

            kbps_->sample();

            srs_trace("<- " SRS_CONSTS_LOG_SRT_PUBLISH " time=%d, packets=%d, okbps=%d,%d,%d, ikbps=%d,%d,%d",
                (int)pprint->age(), nb_packets, kbps_->get_send_kbps(), kbps_->get_send_kbps_30s(), kbps_->get_send_kbps_5m(),
                kbps_->get_recv_kbps(), kbps_->get_recv_kbps_30s(), kbps_->get_recv_kbps_5m());
            nb_packets = 0;
        }

        ssize_t nb = 0;
        if ((err = srt_conn_->read(buf, sizeof(buf), &nb)) != srs_success) {
            return srs_error_wrap(err, "srt: recvmsg");
        }

        ++nb_packets;

        if ((err = on_srt_packet(buf, nb)) != srs_success) {
            return srs_error_wrap(err, "srt: process packet");
        }
    }
        
    return err;
}

srs_error_t SrsMpegtsSrtConn::do_playing()
{
    srs_error_t err = srs_success;

    SrsSrtConsumer* consumer = NULL;
    SrsAutoFree(SrsSrtConsumer, consumer);
    if ((err = srt_source_->create_consumer(consumer)) != srs_success) {
        return srs_error_wrap(err, "create consumer, ts source=%s", req_->get_stream_url().c_str());
    }

    srs_assert(consumer);

    // TODO: FIXME: Dumps the SPS/PPS from gop cache, without other frames.
    if ((err = srt_source_->consumer_dumps(consumer)) != srs_success) {
        return srs_error_wrap(err, "dumps consumer, url=%s", req_->get_stream_url().c_str());
    }

    SrsPithyPrint* pprint = SrsPithyPrint::create_srt_play();
    SrsAutoFree(SrsPithyPrint, pprint);

    SrsSrtRecvThread srt_recv_trd(srt_conn_);
    if ((err = srt_recv_trd.start()) != srs_success) {
        return srs_error_wrap(err, "start srt recv trd");
    }

    int nb_packets = 0;

    while (true) {
        if ((err = trd_->pull()) != srs_success) {
            return srs_error_wrap(err, "srt play thread");
        }

        if ((err = srt_recv_trd.get_recv_err()) != srs_success) {
            return srs_error_wrap(err, "srt play recv thread");
        }

        pprint->elapse();

        // Wait for amount of packets.
        SrsSrtPacket* pkt = NULL;
        SrsAutoFree(SrsSrtPacket, pkt);
        consumer->dump_packet(&pkt);
        if (!pkt) {
            // TODO: FIXME: We should check the quit event.
            consumer->wait(1, 1000 * SRS_UTIME_MILLISECONDS);
            continue;
        }

        // reportable
        if (pprint->can_print()) {
            SrsSrtStat s;
            if ((err = s.fetch(srt_fd_, true)) != srs_success) {
                srs_freep(err);
            } else {
                srs_trace("-> " SRS_CONSTS_LOG_SRT_PLAY " Transport Stats # pktSent=%" PRId64 ", pktSndLoss=%d, pktRetrans=%d, pktSndDrop=%d",
                    s.pktSent(), s.pktSndLoss(), s.pktRetrans(), s.pktSndDrop());
            }

            kbps_->sample();

            srs_trace("-> " SRS_CONSTS_LOG_SRT_PLAY " time=%d, packets=%d, okbps=%d,%d,%d, ikbps=%d,%d,%d",
                (int)pprint->age(), nb_packets, kbps_->get_send_kbps(), kbps_->get_send_kbps_30s(), kbps_->get_send_kbps_5m(),
                kbps_->get_recv_kbps(), kbps_->get_recv_kbps_30s(), kbps_->get_recv_kbps_5m());
            nb_packets = 0;
        }
        
        ++nb_packets;

        ssize_t nb_write = 0;
        if ((err = srt_conn_->write(pkt->data(), pkt->size(), &nb_write)) != srs_success) {
            return srs_error_wrap(err, "srt send, size=%d", pkt->size());
        }
    }

    return err;
}

srs_error_t SrsMpegtsSrtConn::on_srt_packet(char* buf, int nb_buf)
{
    srs_error_t err = srs_success;

    // Check srt payload, mpegts must be N times of SRS_TS_PACKET_SIZE, and the first byte must be 0x47
    if ((nb_buf <= 0) || (nb_buf % SRS_TS_PACKET_SIZE != 0) || (buf[0] != 0x47)) {
        return srs_error_new(ERROR_SRT_CONN, "invalid ts packet");
    }

    SrsSrtPacket* packet = new SrsSrtPacket();
    SrsAutoFree(SrsSrtPacket, packet);
    packet->wrap(buf, nb_buf);

    if ((err = srt_source_->on_packet(packet)) != srs_success) {
        return srs_error_wrap(err, "on srt packet");
    }
    
    return err;
}

srs_error_t SrsMpegtsSrtConn::http_hooks_on_connect()
{
    srs_error_t err = srs_success;
    
    if (!_srs_config->get_vhost_http_hooks_enabled(req_->vhost)) {
        return err;
    }
    
    // the http hooks will cause context switch,
    // so we must copy all hooks for the on_connect may freed.
    // @see https://github.com/ossrs/srs/issues/475
    vector<string> hooks;
    
    if (true) {
        SrsConfDirective* conf = _srs_config->get_vhost_on_connect(req_->vhost);
        
        if (!conf) {
            return err;
        }
        
        hooks = conf->args;
    }
    
    for (int i = 0; i < (int)hooks.size(); i++) {
        std::string url = hooks.at(i);
        if ((err = SrsHttpHooks::on_connect(url, req_)) != srs_success) {
            return srs_error_wrap(err, "srt on_connect %s", url.c_str());
        }
    }
    
    return err;
}

void SrsMpegtsSrtConn::http_hooks_on_close()
{
    if (!_srs_config->get_vhost_http_hooks_enabled(req_->vhost)) {
        return;
    }
    
    // the http hooks will cause context switch,
    // so we must copy all hooks for the on_connect may freed.
    // @see https://github.com/ossrs/srs/issues/475
    vector<string> hooks;
    
    if (true) {
        SrsConfDirective* conf = _srs_config->get_vhost_on_close(req_->vhost);
        
        if (!conf) {
            return;
        }
        
        hooks = conf->args;
    }
    
    for (int i = 0; i < (int)hooks.size(); i++) {
        std::string url = hooks.at(i);
        SrsHttpHooks::on_close(url, req_, kbps_->get_send_bytes(), kbps_->get_recv_bytes());
    }
}

srs_error_t SrsMpegtsSrtConn::http_hooks_on_publish()
{
    srs_error_t err = srs_success;
    
    if (!_srs_config->get_vhost_http_hooks_enabled(req_->vhost)) {
        return err;
    }
    
    // the http hooks will cause context switch,
    // so we must copy all hooks for the on_connect may freed.
    // @see https://github.com/ossrs/srs/issues/475
    vector<string> hooks;
    
    if (true) {
        SrsConfDirective* conf = _srs_config->get_vhost_on_publish(req_->vhost);
        
        if (!conf) {
            return err;
        }
        
        hooks = conf->args;
    }
    
    for (int i = 0; i < (int)hooks.size(); i++) {
        std::string url = hooks.at(i);
        if ((err = SrsHttpHooks::on_publish(url, req_)) != srs_success) {
            return srs_error_wrap(err, "srt on_publish %s", url.c_str());
        }
    }
    
    return err;
}

void SrsMpegtsSrtConn::http_hooks_on_unpublish()
{
    if (!_srs_config->get_vhost_http_hooks_enabled(req_->vhost)) {
        return;
    }
    
    // the http hooks will cause context switch,
    // so we must copy all hooks for the on_connect may freed.
    // @see https://github.com/ossrs/srs/issues/475
    vector<string> hooks;
    
    if (true) {
        SrsConfDirective* conf = _srs_config->get_vhost_on_unpublish(req_->vhost);
        
        if (!conf) {
            return;
        }
        
        hooks = conf->args;
    }
    
    for (int i = 0; i < (int)hooks.size(); i++) {
        std::string url = hooks.at(i);
        SrsHttpHooks::on_unpublish(url, req_);
    }
}

srs_error_t SrsMpegtsSrtConn::http_hooks_on_play()
{
    srs_error_t err = srs_success;
    
    if (!_srs_config->get_vhost_http_hooks_enabled(req_->vhost)) {
        return err;
    }
    
    // the http hooks will cause context switch,
    // so we must copy all hooks for the on_connect may freed.
    // @see https://github.com/ossrs/srs/issues/475
    vector<string> hooks;
    
    if (true) {
        SrsConfDirective* conf = _srs_config->get_vhost_on_play(req_->vhost);
        
        if (!conf) {
            return err;
        }
        
        hooks = conf->args;
    }
    
    for (int i = 0; i < (int)hooks.size(); i++) {
        std::string url = hooks.at(i);
        if ((err = SrsHttpHooks::on_play(url, req_)) != srs_success) {
            return srs_error_wrap(err, "srt on_play %s", url.c_str());
        }
    }
    
    return err;
}

void SrsMpegtsSrtConn::http_hooks_on_stop()
{
    if (!_srs_config->get_vhost_http_hooks_enabled(req_->vhost)) {
        return;
    }
    
    // the http hooks will cause context switch,
    // so we must copy all hooks for the on_connect may freed.
    // @see https://github.com/ossrs/srs/issues/475
    vector<string> hooks;
    
    if (true) {
        SrsConfDirective* conf = _srs_config->get_vhost_on_stop(req_->vhost);
        
        if (!conf) {
            return;
        }
        
        hooks = conf->args;
    }
    
    for (int i = 0; i < (int)hooks.size(); i++) {
        std::string url = hooks.at(i);
        SrsHttpHooks::on_stop(url, req_);
    }
    
    return;
}
