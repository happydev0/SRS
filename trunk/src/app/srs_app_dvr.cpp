/*
The MIT License (MIT)

Copyright (c) 2013-2017 SRS(ossrs)

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

#include <srs_app_dvr.hpp>

#ifdef SRS_AUTO_DVR

#include <fcntl.h>
#include <sstream>
#include <algorithm>
using namespace std;

#include <srs_app_config.hpp>
#include <srs_rtmp_stack.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_app_http_hooks.hpp>
#include <srs_kernel_codec.hpp>
#include <srs_kernel_flv.hpp>
#include <srs_kernel_file.hpp>
#include <srs_protocol_amf0.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_protocol_json.hpp>
#include <srs_app_utility.hpp>
#include <srs_kernel_mp4.hpp>

// update the flv duration and filesize every this interval in ms.
#define SRS_DVR_UPDATE_DURATION_INTERVAL 60000

SrsDvrSegmenter::SrsDvrSegmenter()
{
    req = NULL;
    jitter = NULL;
    plan = NULL;
    duration = 0;
    
    path = "";
    fs = new SrsFileWriter();
    jitter_algorithm = SrsRtmpJitterAlgorithmOFF;

    _srs_config->subscribe(this);
}

SrsDvrSegmenter::~SrsDvrSegmenter()
{
    _srs_config->unsubscribe(this);

    srs_freep(jitter);
    srs_freep(fs);
}

int SrsDvrSegmenter::initialize(SrsDvrPlan* p, SrsRequest* r)
{
    int ret = ERROR_SUCCESS;
    
    req = r;
    plan = p;
    
    int jitter = _srs_config->get_dvr_time_jitter(req->vhost);
    jitter_algorithm = (SrsRtmpJitterAlgorithm)jitter;

    return ret;
}

string SrsDvrSegmenter::get_path()
{
    return path;
}

bool SrsDvrSegmenter::is_overflow(int64_t max_duration)
{
    return duration >= max_duration;
}

int SrsDvrSegmenter::open(bool use_tmp_file)
{
    int ret = ERROR_SUCCESS;
    
    // ignore when already open.
    if (fs->is_open()) {
        return ret;
    }

    path = generate_path();
    bool can_append = srs_string_ends_with(path, ".flv");
    
    bool target_exists = srs_path_exists(path);
    if (!can_append && target_exists) {
        ret = ERROR_DVR_CANNOT_APPEND;
        srs_error("DVR can't append to exists path=%s. ret=%d", path.c_str(), ret);
        return ret;
    }
    
    // create dir first.
    std::string dir = srs_path_dirname(path);
    if ((ret = srs_create_dir_recursively(dir)) != ERROR_SUCCESS) {
        srs_error("create dir=%s failed. ret=%d", dir.c_str(), ret);
        return ret;
    }
    srs_info("create dir=%s ok", dir.c_str());

    // create jitter.
    if ((ret = create_jitter(target_exists)) != ERROR_SUCCESS) {
        srs_error("create jitter failed, path=%s, exists=%d. ret=%d", path.c_str(), target_exists, ret);
        return ret;
    }
    
    // generate the tmp flv path.
    if (target_exists || !use_tmp_file) {
        // when path exists, always append to it.
        // so we must use the target flv path as output flv.
        tmp_dvr_file = path;
    } else {
        // when path not exists, dvr to tmp file.
        tmp_dvr_file = path + ".tmp";
    }
    
    // open file writer, in append or create mode.
    if (target_exists) {
        if ((ret = fs->open_append(tmp_dvr_file)) != ERROR_SUCCESS) {
            srs_error("append file stream for file %s failed. ret=%d", path.c_str(), ret);
            return ret;
        }
        srs_trace("dvr: always append to when exists, file=%s.", path.c_str());
    } else {
        if ((ret = fs->open(tmp_dvr_file)) != ERROR_SUCCESS) {
            srs_error("open file stream for file %s failed. ret=%d", path.c_str(), ret);
            return ret;
        }
    }

    // initialize the encoder.
    if ((ret = open_encoder()) != ERROR_SUCCESS) {
        return ret;
    }
    
    srs_trace("dvr stream %s to file %s", req->stream.c_str(), path.c_str());

    return ret;
}

int SrsDvrSegmenter::close()
{
    int ret = ERROR_SUCCESS;
    
    // ignore when already closed.
    if (!fs->is_open()) {
        return ret;
    }

    // Close the encoder, then close the fs object.
    if ((ret = close_encoder()) != ERROR_SUCCESS) {
        return ret;
    }
    
    fs->close();
    
    // when tmp flv file exists, reap it.
    if (tmp_dvr_file != path) {
        if (rename(tmp_dvr_file.c_str(), path.c_str()) < 0) {
            ret = ERROR_SYSTEM_FILE_RENAME;
            srs_error("rename flv file failed, %s => %s. ret=%d", 
                tmp_dvr_file.c_str(), path.c_str(), ret);
            return ret;
        }
    }

    // TODO: FIXME: the http callback is async, which will trigger thread switch,
    //          so the on_video maybe invoked during the http callback, and error.
    if ((ret = plan->on_reap_segment()) != ERROR_SUCCESS) {
        srs_error("dvr: notify plan to reap segment failed. ret=%d", ret);
        return ret;
    }

    return ret;
}

string SrsDvrSegmenter::generate_path()
{
    // the path in config, for example, 
    //      /data/[vhost]/[app]/[stream]/[2006]/[01]/[02]/[15].[04].[05].[999].flv
    std::string path_config = _srs_config->get_dvr_path(req->vhost);
    
    // add [stream].[timestamp].flv as filename for dir
    if (!srs_string_ends_with(path_config, ".flv")) {
        path_config += "/[stream].[timestamp].flv";
    }
    
    // the flv file path
    std::string flv_path = path_config;
    flv_path = srs_path_build_stream(flv_path, req->vhost, req->app, req->stream);
    flv_path = srs_path_build_timestamp(flv_path);

    return flv_path;
}

int SrsDvrSegmenter::create_jitter(bool target_exists)
{
    int ret = ERROR_SUCCESS;
    
    // When DVR target file not exists, create new jitter.
    if (!target_exists) {
        // jitter when publish, ensure whole stream start from 0.
        srs_freep(jitter);
        jitter = new SrsRtmpJitter();
        
        duration = 0;

        return ret;
    }

    // when jitter ok, do nothing.
    if (jitter) {
        return ret;
    }

    // always ensure the jitter crote.
    // for the first time, initialize jitter from exists file.
    jitter = new SrsRtmpJitter();

    // TODO: FIXME: implements it.

    return ret;
}

int SrsDvrSegmenter::on_reload_vhost_dvr(std::string /*vhost*/)
{
    int ret = ERROR_SUCCESS;
    
    jitter_algorithm = (SrsRtmpJitterAlgorithm)_srs_config->get_dvr_time_jitter(req->vhost);
    
    return ret;
}

SrsDvrFlvSegmenter::SrsDvrFlvSegmenter()
{
    enc = new SrsFlvEncoder();
    
    duration_offset = 0;
    filesize_offset = 0;
    
    has_keyframe = false;
    
    starttime = -1;
    stream_starttime = 0;
    stream_previous_pkt_time = -1;
    stream_duration = 0;
}

SrsDvrFlvSegmenter::~SrsDvrFlvSegmenter()
{
    srs_freep(enc);
}

int SrsDvrFlvSegmenter::open_encoder()
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = enc->initialize(fs)) != ERROR_SUCCESS) {
        srs_error("initialize enc by fs for file %s failed. ret=%d", path.c_str(), ret);
        return ret;
    }
    
    bool target_exists = srs_path_exists(path);
    if (target_exists){
        has_keyframe = false;
        
        // fresh stream starting.
        starttime = -1;
        stream_previous_pkt_time = -1;
        stream_starttime = srs_update_system_time_ms();
        stream_duration = 0;
    
        // write the flv header to writer.
        if ((ret = enc->write_header()) != ERROR_SUCCESS) {
            srs_error("write flv header failed. ret=%d", ret);
            return ret;
        }
    }
    
    // update the duration and filesize offset.
    duration_offset = 0;
    filesize_offset = 0;
    
    return ret;
}

int SrsDvrFlvSegmenter::close_encoder()
{
    return refresh_metadata();
}

int SrsDvrFlvSegmenter::on_update_duration(SrsSharedPtrMessage* msg)
{
    int ret = ERROR_SUCCESS;
    
    // we must assumpt that the stream timestamp is monotonically increase,
    // that is, always use time jitter to correct the timestamp.
    // except the time jitter is disabled in config.
    
    // set the segment starttime at first time
    if (starttime < 0) {
        starttime = msg->timestamp;
    }
    
    // no previous packet or timestamp overflow.
    if (stream_previous_pkt_time < 0 || stream_previous_pkt_time > msg->timestamp) {
        stream_previous_pkt_time = msg->timestamp;
    }
    
    // collect segment and stream duration, timestamp overflow is ok.
    duration += msg->timestamp - stream_previous_pkt_time;
    stream_duration += msg->timestamp - stream_previous_pkt_time;
    
    // update previous packet time
    stream_previous_pkt_time = msg->timestamp;
    
    return ret;
}

int SrsDvrFlvSegmenter::write_metadata(SrsSharedPtrMessage* metadata)
{
    int ret = ERROR_SUCCESS;
    
    // Ignore when metadata already written.
    if (duration_offset || filesize_offset) {
        return ret;
    }
    
    SrsBuffer stream;
    if ((ret = stream.initialize(metadata->payload, metadata->size)) != ERROR_SUCCESS) {
        return ret;
    }
    
    SrsAmf0Any* name = SrsAmf0Any::str();
    SrsAutoFree(SrsAmf0Any, name);
    if ((ret = name->read(&stream)) != ERROR_SUCCESS) {
        return ret;
    }
    
    SrsAmf0Object* obj = SrsAmf0Any::object();
    SrsAutoFree(SrsAmf0Object, obj);
    if ((ret = obj->read(&stream)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // remove duration and filesize.
    obj->set("filesize", NULL);
    obj->set("duration", NULL);
    
    // add properties.
    obj->set("service", SrsAmf0Any::str(RTMP_SIG_SRS_SERVER));
    obj->set("filesize", SrsAmf0Any::number(0));
    obj->set("duration", SrsAmf0Any::number(0));
    
    int size = name->total_size() + obj->total_size();
    char* payload = new char[size];
    SrsAutoFreeA(char, payload);
    
    // 11B flv header, 3B object EOF, 8B number value, 1B number flag.
    duration_offset = fs->tellg() + size + 11 - SrsAmf0Size::object_eof() - SrsAmf0Size::number();
    // 2B string flag, 8B number value, 8B string 'duration', 1B number flag
    filesize_offset = duration_offset - SrsAmf0Size::utf8("duration") - SrsAmf0Size::number();
    
    // convert metadata to bytes.
    if ((ret = stream.initialize(payload, size)) != ERROR_SUCCESS) {
        return ret;
    }
    if ((ret = name->write(&stream)) != ERROR_SUCCESS) {
        return ret;
    }
    if ((ret = obj->write(&stream)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // to flv file.
    if ((ret = enc->write_metadata(18, payload, size)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsDvrFlvSegmenter::write_audio(SrsSharedPtrMessage* shared_audio)
{
    int ret = ERROR_SUCCESS;
    
    SrsSharedPtrMessage* audio = shared_audio->copy();
    SrsAutoFree(SrsSharedPtrMessage, audio);
    
    if ((jitter->correct(audio, jitter_algorithm)) != ERROR_SUCCESS) {
        return ret;
    }
    
    char* payload = audio->payload;
    int size = audio->size;
    int64_t timestamp = plan->filter_timestamp(audio->timestamp);
    if ((ret = enc->write_audio(timestamp, payload, size)) != ERROR_SUCCESS) {
        return ret;
    }
    
    if ((ret = on_update_duration(audio)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsDvrFlvSegmenter::write_video(SrsSharedPtrMessage* shared_video)
{
    int ret = ERROR_SUCCESS;
    
    SrsSharedPtrMessage* video = shared_video->copy();
    SrsAutoFree(SrsSharedPtrMessage, video);
    
    char* payload = video->payload;
    int size = video->size;
    
    bool is_sequence_header = SrsFlvCodec::video_is_sequence_header(payload, size);
    bool is_key_frame = SrsFlvCodec::video_is_h264(payload, size)
        && SrsFlvCodec::video_is_keyframe(payload, size) && !is_sequence_header;
    if (is_key_frame) {
        has_keyframe = true;
        if ((ret = plan->on_video_keyframe()) != ERROR_SUCCESS) {
            return ret;
        }
    }
    srs_verbose("dvr video is key: %d", is_key_frame);
    
    // accept the sequence header here.
    // when got no keyframe, ignore when should wait keyframe.
    if (!has_keyframe && !is_sequence_header) {
        bool wait_keyframe = _srs_config->get_dvr_wait_keyframe(req->vhost);
        if (wait_keyframe) {
            srs_info("dvr: ignore when wait keyframe.");
            return ret;
        }
    }
    
    if ((jitter->correct(video, jitter_algorithm)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // update segment duration, session plan just update the duration,
    // the segment plan will reap segment if exceed, this video will write to next segment.
    if ((ret = on_update_duration(video)) != ERROR_SUCCESS) {
        return ret;
    }
    
    int32_t timestamp = (int32_t)plan->filter_timestamp(video->timestamp);
    if ((ret = enc->write_video(timestamp, payload, size)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsDvrFlvSegmenter::refresh_metadata()
{
    int ret = ERROR_SUCCESS;
    
    // no duration or filesize specified.
    if (!duration_offset || !filesize_offset) {
        return ret;
    }
    
    int64_t cur = fs->tellg();
    
    // buffer to write the size.
    char* buf = new char[SrsAmf0Size::number()];
    SrsAutoFreeA(char, buf);
    
    SrsBuffer stream;
    if ((ret = stream.initialize(buf, SrsAmf0Size::number())) != ERROR_SUCCESS) {
        return ret;
    }
    
    // filesize to buf.
    SrsAmf0Any* size = SrsAmf0Any::number((double)cur);
    SrsAutoFree(SrsAmf0Any, size);
    
    stream.skip(-1 * stream.pos());
    if ((ret = size->write(&stream)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // update the flesize.
    fs->seek2(filesize_offset);
    if ((ret = fs->write(buf, SrsAmf0Size::number(), NULL)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // duration to buf
    SrsAmf0Any* dur = SrsAmf0Any::number((double)duration / 1000.0);
    SrsAutoFree(SrsAmf0Any, dur);
    
    stream.skip(-1 * stream.pos());
    if ((ret = dur->write(&stream)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // update the duration
    fs->seek2(duration_offset);
    if ((ret = fs->write(buf, SrsAmf0Size::number(), NULL)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // reset the offset.
    fs->seek2(cur);
    
    return ret;
}

SrsDvrMp4Segmenter::SrsDvrMp4Segmenter()
{
    enc = new SrsMp4Encoder();
}

SrsDvrMp4Segmenter::~SrsDvrMp4Segmenter()
{
    srs_freep(enc);
}

int SrsDvrMp4Segmenter::open_encoder()
{
    int ret = ERROR_SUCCESS;
    return ret;
}

int SrsDvrMp4Segmenter::close_encoder()
{
    int ret = ERROR_SUCCESS;
    return ret;
}

int SrsDvrMp4Segmenter::write_metadata(SrsSharedPtrMessage* metadata)
{
    int ret = ERROR_SUCCESS;
    return ret;
}

int SrsDvrMp4Segmenter::write_audio(SrsSharedPtrMessage* shared_audio)
{
    int ret = ERROR_SUCCESS;
    return ret;
}

int SrsDvrMp4Segmenter::write_video(SrsSharedPtrMessage* shared_video)
{
    int ret = ERROR_SUCCESS;
    return ret;
}

int SrsDvrMp4Segmenter::refresh_metadata()
{
    int ret = ERROR_SUCCESS;
    return ret;
}

SrsDvrAsyncCallOnDvr::SrsDvrAsyncCallOnDvr(int c, SrsRequest* r, string p)
{
    cid = c;
    req = r->copy();
    path = p;
}

SrsDvrAsyncCallOnDvr::~SrsDvrAsyncCallOnDvr()
{
    srs_freep(req);
}

int SrsDvrAsyncCallOnDvr::call()
{
    int ret = ERROR_SUCCESS;
    
    if (!_srs_config->get_vhost_http_hooks_enabled(req->vhost)) {
        return ret;
    }
    
    // the http hooks will cause context switch,
    // so we must copy all hooks for the on_connect may freed.
    // @see https://github.com/ossrs/srs/issues/475
    vector<string> hooks;
    
    if (true) {
        SrsConfDirective* conf = _srs_config->get_vhost_on_dvr(req->vhost);
        
        if (!conf) {
            srs_info("ignore the empty http callback: on_dvr");
            return ret;
        }
        
        hooks = conf->args;
    }
    
    for (int i = 0; i < (int)hooks.size(); i++) {
        std::string url = hooks.at(i);
        if ((ret = SrsHttpHooks::on_dvr(cid, url, req, path)) != ERROR_SUCCESS) {
            srs_error("hook client on_dvr failed. url=%s, ret=%d", url.c_str(), ret);
            return ret;
        }
    }

    return ret;
}

string SrsDvrAsyncCallOnDvr::to_string()
{
    std::stringstream ss;
    ss << "vhost=" << req->vhost << ", file=" << path;
    return ss.str();
}

SrsDvrPlan::SrsDvrPlan()
{
    req = NULL;

    dvr_enabled = false;
    segment = NULL;
    async = new SrsAsyncCallWorker();
}

SrsDvrPlan::~SrsDvrPlan()
{
    srs_freep(segment);
    srs_freep(async);
}

int SrsDvrPlan::initialize(SrsDvrSegmenter* s, SrsRequest* r)
{
    int ret = ERROR_SUCCESS;
    
    req = r;
    segment = s;

    if ((ret = segment->initialize(this, r)) != ERROR_SUCCESS) {
        return ret;
    }

    if ((ret = async->start()) != ERROR_SUCCESS) {
        return ret;
    }

    return ret;
}

int SrsDvrPlan::on_video_keyframe()
{
    return ERROR_SUCCESS;
}

int64_t SrsDvrPlan::filter_timestamp(int64_t timestamp)
{
    return timestamp;
}

int SrsDvrPlan::on_meta_data(SrsSharedPtrMessage* shared_metadata)
{
    int ret = ERROR_SUCCESS;
    
    if (!dvr_enabled) {
        return ret;
    }
    
    return segment->write_metadata(shared_metadata);
}

int SrsDvrPlan::on_audio(SrsSharedPtrMessage* shared_audio)
{
    int ret = ERROR_SUCCESS;
    
    if (!dvr_enabled) {
        return ret;
    }

    if ((ret = segment->write_audio(shared_audio)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsDvrPlan::on_video(SrsSharedPtrMessage* shared_video)
{
    int ret = ERROR_SUCCESS;
    
    if (!dvr_enabled) {
        return ret;
    }

    if ((ret = segment->write_video(shared_video)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsDvrPlan::on_reap_segment()
{
    int ret = ERROR_SUCCESS;

    int cid = _srs_context->get_id();
    if ((ret = async->execute(new SrsDvrAsyncCallOnDvr(cid, req, segment->get_path()))) != ERROR_SUCCESS) {
        return ret;
    }

    return ret;
}

int SrsDvrPlan::create_plan(string vhost, SrsDvrPlan** pplan)
{
    int ret = ERROR_SUCCESS;
    
    std::string plan = _srs_config->get_dvr_plan(vhost);
    std::string path = _srs_config->get_dvr_path(vhost);
    bool is_mp4 = srs_string_ends_with(path, ".mp4");
    
    if (srs_config_dvr_is_plan_segment(plan)) {
        *pplan = new SrsDvrSegmentPlan();
    } else if (srs_config_dvr_is_plan_session(plan)) {
        *pplan = new SrsDvrSessionPlan();
    } else if (srs_config_dvr_is_plan_append(plan)) {
        if (is_mp4) {
            ret = ERROR_DVR_ILLEGAL_PLAN;
            srs_error("DVR plan append not support MP4. ret=%d", ret);
            return ret;
        }
        *pplan = new SrsDvrAppendPlan();
    } else {
        srs_error("invalid dvr plan=%s, vhost=%s", plan.c_str(), vhost.c_str());
        srs_assert(false);
    }
    
    return ret;
}

SrsDvrSessionPlan::SrsDvrSessionPlan()
{
}

SrsDvrSessionPlan::~SrsDvrSessionPlan()
{
}

int SrsDvrSessionPlan::on_publish()
{
    int ret = ERROR_SUCCESS;
    
    // support multiple publish.
    if (dvr_enabled) {
        return ret;
    }

    if (!_srs_config->get_dvr_enabled(req->vhost)) {
        return ret;
    }

    if ((ret = segment->close()) != ERROR_SUCCESS) {
        return ret;
    }

    if ((ret = segment->open(true)) != ERROR_SUCCESS) {
        return ret;
    }

    dvr_enabled = true;

    return ret;
}

void SrsDvrSessionPlan::on_unpublish()
{
    // support multiple publish.
    if (!dvr_enabled) {
        return;
    }
    
    // ignore error.
    int ret = segment->close();
    if (ret != ERROR_SUCCESS) {
        srs_warn("ignore flv close error. ret=%d", ret);
    }
    
    dvr_enabled = false;
}

SrsDvrAppendPlan::SrsDvrAppendPlan()
{
    last_update_time = 0;
}

SrsDvrAppendPlan::~SrsDvrAppendPlan()
{
}

int SrsDvrAppendPlan::on_publish()
{
    int ret = ERROR_SUCCESS;
    
    // support multiple publish.
    if (dvr_enabled) {
        return ret;
    }

    if (!_srs_config->get_dvr_enabled(req->vhost)) {
        return ret;
    }

    if ((ret = segment->open(false)) != ERROR_SUCCESS) {
        return ret;
    }

    dvr_enabled = true;

    return ret;
}

void SrsDvrAppendPlan::on_unpublish()
{
}

int SrsDvrAppendPlan::on_audio(SrsSharedPtrMessage* shared_audio)
{
    int ret = ERROR_SUCCESS;

    if ((ret = update_duration(shared_audio)) != ERROR_SUCCESS) {
        return ret;
    }
    
    if ((ret = SrsDvrPlan::on_audio(shared_audio)) != ERROR_SUCCESS) {
        return ret;
    }

    return ret;
}

int SrsDvrAppendPlan::on_video(SrsSharedPtrMessage* shared_video)
{
    int ret = ERROR_SUCCESS;

    if ((ret = update_duration(shared_video)) != ERROR_SUCCESS) {
        return ret;
    }
    
    if ((ret = SrsDvrPlan::on_video(shared_video)) != ERROR_SUCCESS) {
        return ret;
    }

    return ret;
}

int SrsDvrAppendPlan::update_duration(SrsSharedPtrMessage* msg)
{
    int ret = ERROR_SUCCESS;

    if (last_update_time <= 0) {
        last_update_time = msg->timestamp;
        return ret;
    }

    if (msg->timestamp < last_update_time) {
        last_update_time = msg->timestamp;
        return ret;
    }

    if (SRS_DVR_UPDATE_DURATION_INTERVAL > msg->timestamp - last_update_time) {
        return ret;
    }
    last_update_time = msg->timestamp;
    
    srs_assert(segment);
    if ((ret = segment->refresh_metadata()) != ERROR_SUCCESS) {
        return ret;
    }

    return ret;
}

SrsDvrSegmentPlan::SrsDvrSegmentPlan()
{
    segment_duration = -1;
    metadata = sh_video = sh_audio = NULL;
}

SrsDvrSegmentPlan::~SrsDvrSegmentPlan()
{
    srs_freep(sh_video);
    srs_freep(sh_audio);
    srs_freep(metadata);
}

int SrsDvrSegmentPlan::initialize(SrsDvrSegmenter* s, SrsRequest* req)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = SrsDvrPlan::initialize(s, req)) != ERROR_SUCCESS) {
        return ret;
    }
    
    segment_duration = _srs_config->get_dvr_duration(req->vhost);
    // to ms
    segment_duration *= 1000;
    
    return ret;
}

int SrsDvrSegmentPlan::on_publish()
{
    int ret = ERROR_SUCCESS;
    
    // support multiple publish.
    if (dvr_enabled) {
        return ret;
    }

    if (!_srs_config->get_dvr_enabled(req->vhost)) {
        return ret;
    }

    if ((ret = segment->close()) != ERROR_SUCCESS) {
        return ret;
    }

    if ((ret = segment->open(true)) != ERROR_SUCCESS) {
        return ret;
    }

    dvr_enabled = true;

    return ret;
}

void SrsDvrSegmentPlan::on_unpublish()
{
}

int SrsDvrSegmentPlan::on_meta_data(SrsSharedPtrMessage* shared_metadata)
{
    int ret = ERROR_SUCCESS;
    
    srs_freep(metadata);
    metadata = shared_metadata->copy();
    
    if ((ret = SrsDvrPlan::on_meta_data(shared_metadata)) != ERROR_SUCCESS) {
        return ret;
    }

    return ret;
}

int SrsDvrSegmentPlan::on_audio(SrsSharedPtrMessage* shared_audio)
{
    int ret = ERROR_SUCCESS;
    
    if (SrsFlvCodec::audio_is_sequence_header(shared_audio->payload, shared_audio->size)) {
        srs_freep(sh_audio);
        sh_audio = shared_audio->copy();
    }

    if ((ret = update_duration(shared_audio)) != ERROR_SUCCESS) {
        return ret;
    }
    
    if ((ret = SrsDvrPlan::on_audio(shared_audio)) != ERROR_SUCCESS) {
        return ret;
    }

    return ret;
}

int SrsDvrSegmentPlan::on_video(SrsSharedPtrMessage* shared_video)
{
    int ret = ERROR_SUCCESS;

    if (SrsFlvCodec::video_is_sequence_header(shared_video->payload, shared_video->size)) {
        srs_freep(sh_video);
        sh_video = shared_video->copy();
    }

    if ((ret = update_duration(shared_video)) != ERROR_SUCCESS) {
        return ret;
    }
    
    if ((ret = SrsDvrPlan::on_video(shared_video)) != ERROR_SUCCESS) {
        return ret;
    }

    return ret;
}

int SrsDvrSegmentPlan::update_duration(SrsSharedPtrMessage* msg)
{
    int ret = ERROR_SUCCESS;
    
    srs_assert(segment);
    
    // ignore if duration ok.
    if (segment_duration <= 0 || !segment->is_overflow(segment_duration)) {
        return ret;
    }
    
    // when wait keyframe, ignore if no frame arrived.
    // @see https://github.com/ossrs/srs/issues/177
    if (_srs_config->get_dvr_wait_keyframe(req->vhost)) {
        if (!msg->is_video()) {
            return ret;
        }
        
        char* payload = msg->payload;
        int size = msg->size;
        bool is_key_frame = SrsFlvCodec::video_is_h264(payload, size) 
            && SrsFlvCodec::video_is_keyframe(payload, size) 
            && !SrsFlvCodec::video_is_sequence_header(payload, size);
        if (!is_key_frame) {
            return ret;
        }
    }
    
    // reap segment
    if ((ret = segment->close()) != ERROR_SUCCESS) {
        return ret;
    }
    
    // open new flv file
    if ((ret = segment->open(true)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // update sequence header
    if (metadata && (ret = SrsDvrPlan::on_meta_data(metadata)) != ERROR_SUCCESS) {
        return ret;
    }
    if (sh_video && (ret = SrsDvrPlan::on_video(sh_video)) != ERROR_SUCCESS) {
        return ret;
    }
    if (sh_audio && (ret = SrsDvrPlan::on_audio(sh_audio)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

SrsDvr::SrsDvr()
{
    hub = NULL;
    plan = NULL;
    req = NULL;
    actived = false;
    
    _srs_config->subscribe(this);
}

SrsDvr::~SrsDvr()
{
    _srs_config->unsubscribe(this);
    
    srs_freep(plan);
}

int SrsDvr::initialize(SrsOriginHub* h, SrsRequest* r)
{
    int ret = ERROR_SUCCESS;

    req = r;
    hub = h;
    
    SrsConfDirective* conf = _srs_config->get_dvr_apply(r->vhost);
    actived = srs_config_apply_filter(conf, r);
    
    srs_freep(plan);
    if ((ret = SrsDvrPlan::create_plan(r->vhost, &plan)) != ERROR_SUCCESS) {
        return ret;
    }
    
    std::string path = _srs_config->get_dvr_path(r->vhost);
    SrsDvrSegmenter* segmenter = NULL;
    if (srs_string_ends_with(path, ".mp4")) {
        segmenter = new SrsDvrMp4Segmenter();
    } else {
        segmenter = new SrsDvrFlvSegmenter();
    }
    
    if ((ret = plan->initialize(segmenter, r)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsDvr::on_publish(bool fetch_sequence_header)
{
    int ret = ERROR_SUCCESS;
    
    // the dvr for this stream is not actived.
    if (!actived) {
        return ret;
    }
    
    if ((ret = plan->on_publish()) != ERROR_SUCCESS) {
        return ret;
    }
    
    if (fetch_sequence_header && (ret = hub->on_dvr_request_sh()) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

void SrsDvr::on_unpublish()
{
    plan->on_unpublish();
}

// TODO: FIXME: source should use shared message instead.
int SrsDvr::on_meta_data(SrsOnMetaDataPacket* m)
{
    int ret = ERROR_SUCCESS;
    
    // the dvr for this stream is not actived.
    if (!actived) {
        return ret;
    }

    int size = 0;
    char* payload = NULL;
    if ((ret = m->encode(size, payload)) != ERROR_SUCCESS) {
        return ret;
    }

    SrsSharedPtrMessage metadata;
    if ((ret = metadata.create(NULL, payload, size)) != ERROR_SUCCESS) {
        return ret;
    }

    if ((ret = plan->on_meta_data(&metadata)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsDvr::on_audio(SrsSharedPtrMessage* shared_audio)
{
    // the dvr for this stream is not actived.
    if (!actived) {
        return ERROR_SUCCESS;
    }
    
    return plan->on_audio(shared_audio);
}

int SrsDvr::on_video(SrsSharedPtrMessage* shared_video)
{
    // the dvr for this stream is not actived.
    if (!actived) {
        return ERROR_SUCCESS;
    }
    
    return plan->on_video(shared_video);
}

int SrsDvr::on_reload_vhost_dvr_apply(string vhost)
{
    int ret = ERROR_SUCCESS;
    
    SrsConfDirective* conf = _srs_config->get_dvr_apply(req->vhost);
    bool v = srs_config_apply_filter(conf, req);
    
    // the apply changed, republish the dvr.
    if (v != actived) {
        actived = v;
        
        on_unpublish();
        return on_publish(true);
    }
    
    return ret;
}

#endif


