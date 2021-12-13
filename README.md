# SRS(Simple Realtime Server)

![](http://ossrs.net/gif/v1/sls.gif?site=github.com&path=/srs/develop)
[![](https://github.com/ossrs/srs/actions/workflows/codeql-analysis.yml/badge.svg?branch=develop)](https://github.com/ossrs/srs/actions?query=workflow%3ACodeQL+branch%3Adevelop)
[![](https://github.com/ossrs/srs/actions/workflows/release.yml/badge.svg)](https://github.com/ossrs/srs/actions/workflows/release.yml?query=workflow%3ARelease)
[![](https://github.com/ossrs/srs/actions/workflows/test.yml/badge.svg?branch=develop)](https://github.com/ossrs/srs/actions?query=workflow%3ATest+branch%3Adevelop)
[![](https://codecov.io/gh/ossrs/srs/branch/develop/graph/badge.svg)](https://codecov.io/gh/ossrs/srs/branch/develop)
[![](https://gitee.com/winlinvip/srs-wiki/raw/master/images/wechat-badge4.svg)](../../wikis/Contact#wechat)
[![](https://gitee.com/winlinvip/srs-wiki/raw/master/images/srs-faq.svg)](https://github.com/ossrs/srs/issues/2716)
[![](https://gitee.com/winlinvip/srs-wiki/raw/master/images/mulan-incubating.svg)](http://mulanos.cn)
[![](https://gitee.com/winlinvip/srs-wiki/raw/master/images/srs-alternativeto.svg)](https://alternativeto.net/software/srs/about/)
[![](https://gitee.com/winlinvip/srs-wiki/raw/master/images/srs-facebook.svg)](https://www.facebook.com/winlinvip)
[![](https://badgen.net/twitter/follow/winlinvip)](https://twitter.com/winlinvip)
[![](https://img.shields.io/youtube/channel/views/UCP6ZblCL_fIJoEyUzZxC1ng?style=social)](https://www.youtube.com/channel/UCP6ZblCL_fIJoEyUzZxC1ng)
[![](https://img.shields.io/twitch/status/winlinvip?style=social)](https://www.twitch.tv/winlinvip)
[![](https://badgen.net/discord/members/yZ4BnPmHAd)](https://discord.gg/yZ4BnPmHAd)
[![](https://opencollective.com/srs-server/tiers/badge.svg)](https://opencollective.com/srs-server/contribute)

SRS/5.0，[Bee](https://github.com/ossrs/srs/wiki/Product#release50) 是一个简单高效的实时视频服务器，支持RTMP/WebRTC/HLS/HTTP-FLV/SRT。

SRS/5.0 is a simple, high efficiency and realtime video server, supports RTMP/WebRTC/HLS/HTTP-FLV/SRT.

SRS is licenced under [MIT](https://github.com/ossrs/srs/blob/develop/LICENSE), but some depended libraries are distributed using their [own licenses](https://github.com/ossrs/srs/wiki/LicenseMixing).

[![SRS Overview](https://gitee.com/winlinvip/srs-wiki/raw/master/images/SRS-Overview-4.0.png)](https://gitee.com/winlinvip/srs-wiki/raw/master/images/SRS-Overview-4.0.png)

> Note: If image load fail, please see it at [here](https://www.processon.com/view/link/619f29791efad425fd699fd2).

<a name="product"></a>
<a name="usage-docker"></a>
## Usage

Build SRS from source or **docker([CN](https://github.com/ossrs/srs/wiki/v4_CN_Home#docker) / [EN](https://github.com/ossrs/srs/wiki/v4_EN_Home#docker))**, please read **Wiki: Gettting Started([CN](https://github.com/ossrs/srs/wiki/v4_CN_Home#getting-started) / [EN](https://github.com/ossrs/srs/wiki/v4_EN_Home#getting-started))**:

```
git clone -b develop https://gitee.com/ossrs/srs.git &&
cd srs/trunk && ./configure && make && ./objs/srs -c conf/srs.conf
```

Open [http://localhost:8080/](http://localhost:8080/) to check it, then publish
by [FFmpeg](https://ffmpeg.org/download.html) or [OBS](https://obsproject.com/download) as:

```bash
ffmpeg -re -i ./doc/source.flv -c copy -f flv -y rtmp://localhost/live/livestream
```

> Note: It's also able to [publish by H5](http://localhost:8080/players/rtc_publisher.html?autostart=true) if WebRTC is enabled.

Play the following streams by [players](https://ossrs.net):

* RTMP (by [VLC](https://www.videolan.org/)): rtmp://localhost/live/livestream
* H5(HTTP-FLV): [http://localhost:8080/live/livestream.flv](http://localhost:8080/players/srs_player.html?autostart=true&stream=livestream.flv&port=8080&schema=http)
* H5(HLS): [http://localhost:8080/live/livestream.m3u8](http://localhost:8080/players/srs_player.html?autostart=true&stream=livestream.m3u8&port=8080&schema=http)
* H5(WebRTC): [webrtc://localhost/live/livestream](http://localhost:8080/players/rtc_player.html?autostart=true)

<a name="srs-40-wiki"></a>
<a name="wiki"></a>

From here, please read wikis:

* [Getting Started](https://github.com/ossrs/srs/wiki/v4_EN_Home#getting-started), please read Wiki first.
* [中文文档：起步](https://github.com/ossrs/srs/wiki/v4_CN_Home#getting-started)，不读Wiki一定扑街，不读文档请不要提Issue，不读文档请不要提问题，任何文档中明确说过的疑问都不会解答。

Fast index for Wikis:

* Overview? ([CN](https://github.com/ossrs/srs/wiki/v4_CN_Home), [EN](https://github.com/ossrs/srs/wiki/v4_EN_Home))
* How to deliver RTMP streaming?([CN](https://github.com/ossrs/srs/wiki/v4_CN_SampleRTMP), [EN](https://github.com/ossrs/srs/wiki/v4_EN_SampleRTMP))
* How to build RTMP Edge-Cluster?([CN](https://github.com/ossrs/srs/wiki/v4_CN_SampleRTMPCluster), [EN](https://github.com/ossrs/srs/wiki/v4_EN_SampleRTMPCluster))
* How to build RTMP Origin-Cluster?([CN](https://github.com/ossrs/srs/wiki/v4_CN_SampleOriginCluster), [EN](https://github.com/ossrs/srs/wiki/v4_EN_SampleOriginCluster))
* How to deliver HTTP-FLV streaming?([CN](https://github.com/ossrs/srs/wiki/v4_CN_SampleHttpFlv), [EN](https://github.com/ossrs/srs/wiki/v4_EN_SampleHttpFlv))
* How to deliver HLS streaming?([CN](https://github.com/ossrs/srs/wiki/v4_CN_SampleHLS), [EN](https://github.com/ossrs/srs/wiki/v4_EN_SampleHLS))
* How to deliver low-latency streaming?([CN](https://github.com/ossrs/srs/wiki/v4_CN_SampleRealtime), [EN](https://github.com/ossrs/srs/wiki/v4_EN_SampleRealtime))
* How to use WebRTC? ([CN](https://github.com/ossrs/srs/wiki/v4_CN_WebRTC), [EN](https://github.com/ossrs/srs/wiki/v4_EN_WebRTC))

Other important wiki:

* Usage: How to delivery DASH(Experimental)?([CN](https://github.com/ossrs/srs/wiki/v4_CN_SampleDASH), [EN](https://github.com/ossrs/srs/wiki/v4_EN_SampleDASH))
* Usage: How to transode RTMP stream by FFMPEG?([CN](https://github.com/ossrs/srs/wiki/v4_CN_SampleFFMPEG), [EN](https://github.com/ossrs/srs/wiki/v4_EN_SampleFFMPEG))
* Usage: How to delivery HTTP FLV Live Streaming Cluster?([CN](https://github.com/ossrs/srs/wiki/v4_CN_SampleHttpFlvCluster), [EN](https://github.com/ossrs/srs/wiki/v4_EN_SampleHttpFlvCluster))
* Usage: How to ingest file/stream/device to RTMP?([CN](https://github.com/ossrs/srs/wiki/v4_CN_SampleIngest), [EN](https://github.com/ossrs/srs/wiki/v4_EN_SampleIngest))
* Usage: How to forward stream to other servers?([CN](https://github.com/ossrs/srs/wiki/v4_CN_SampleForward), [EN](https://github.com/ossrs/srs/wiki/v4_EN_SampleForward))
* Usage: How to improve edge performance for multiple CPUs? ([CN](https://github.com/ossrs/srs/wiki/v4_CN_REUSEPORT), [EN](https://github.com/ossrs/srs/wiki/v4_EN_REUSEPORT))
* Usage: How to file a bug or contact us? ([CN](https://github.com/ossrs/srs/wiki/v4_CN_Contact), [EN](https://github.com/ossrs/srs/wiki/v4_EN_Contact))

## AUTHORS

[![](https://opencollective.com/srs-server/contributors.svg?width=800&button=false)](https://opencollective.com/srs-server/contribute)

> Note: You may provide financial support for this project by donating [via Open Collective](https://opencollective.com/srs-server/contribute). Thank you for your support!

The [TOC(Technical Oversight Committee)](trunk/AUTHORS.md#toc) and [contributors](trunk/AUTHORS.md#contributors):

* [Winlin](https://github.com/winlinvip): Focus on [ST](https://github.com/ossrs/state-threads) and [Issues/PR](https://github.com/ossrs/srs/issues).
* [ZhaoWenjie](https://github.com/wenjiegit): Focus on [HDS](https://github.com/simple-rtmp-server/srs/wiki/v4_CN_DeliveryHDS) and [Windows](https://github.com/ossrs/srs/issues/2532).
* [ShiWei](https://github.com/runner365): Focus on [SRT](https://github.com/simple-rtmp-server/srs/wiki/v4_CN_SRTWiki) and [H.265](https://github.com/ossrs/srs/issues/465).
* [XiaoZhihong](https://github.com/xiaozhihong): Focus on [WebRTC/QUIC](https://github.com/ossrs/srs/issues/2091) and [SRT](https://github.com/simple-rtmp-server/srs/wiki/v4_CN_SRTWiki).
* [WuPengqiang](https://github.com/Bepartofyou): Focus on [H.265](https://github.com/ossrs/srs/issues/465).
* [XiaLixin](https://github.com/xialixin): Focus on [GB28181](https://github.com/ossrs/srs/issues/1500).
* [LiPeng](https://github.com/lipeng19811218): Focus on [WebRTC](https://github.com/simple-rtmp-server/srs/wiki/v4_CN_WebRTC).
* [ChenGuanghua](https://github.com/chen-guanghua): Focus on [WebRTC/QoS](https://github.com/ossrs/srs/issues/2051).
* [ChenHaibo](https://github.com/duiniuluantanqin): Focus on [GB28181](https://github.com/ossrs/srs/issues/1500) and [API](https://github.com/ossrs/srs/issues/1657).

A big `THANK YOU` also goes to:

* All [contributors](trunk/AUTHORS.md#contributors) of SRS.
* All friends of SRS for [big supports](https://github.com/ossrs/srs/wiki/Product).
* [Genes](http://sourceforge.net/users/genes), [Mabbott](http://sourceforge.net/users/mabbott) and [Michael Talyanksy](https://github.com/michaeltalyansky) for [st](https://github.com/ossrs/state-threads/tree/srs).

## Releases

* 2021-12-01, Release [v4.0-b0](https://github.com/ossrs/srs/releases/tag/v4.0-b0), v4.0-b0, 4.0 beta0, v4.0.201, 144022 lines.
* 2021-11-15, Release [v4.0.198](https://github.com/ossrs/srs/releases/tag/v4.0.198), 4.0 dev8, v4.0.198, 144010 lines.
* 2021-11-02, Release [v4.0.191](https://github.com/ossrs/srs/releases/tag/v4.0.191), 4.0 dev7, v4.0.191, 143890 lines.
* 2021-10-12, Release [v4.0.177](https://github.com/ossrs/srs/releases/tag/v4.0.177), 4.0 dev6, v4.0.177, 143686 lines.
* 2021-09-05, Release [v4.0.161](https://github.com/ossrs/srs/releases/tag/v4.0.161), 4.0 dev5, v4.0.161, 145865 lines.
* 2021-08-15, Release [v4.0.156](https://github.com/ossrs/srs/releases/tag/v4.0.156), 4.0 dev4, v4.0.156, 145490 lines.
* 2021-08-14, Release [v4.0.153](https://github.com/ossrs/srs/releases/tag/v4.0.153), 4.0 dev3, v4.0.153, 145506 lines.
* 2021-08-07, Release [v4.0.150](https://github.com/ossrs/srs/releases/tag/v4.0.150), 4.0 dev2, v4.0.150, 145289 lines.
* 2021-07-25, Release [v4.0.146](https://github.com/ossrs/srs/releases/tag/v4.0.146), 4.0 dev1, v4.0.146, 144026 lines.
* 2021-07-04, Release [v4.0.139](https://github.com/ossrs/srs/releases/tag/v4.0.139), 4.0 dev0, v4.0.139, 143245 lines.
* 2020-06-27, [Release v3.0-r0](https://github.com/ossrs/srs/releases/tag/v3.0-r0), 3.0 release0, 3.0.141, 122674 lines.
* 2020-02-02, [Release v3.0-b0](https://github.com/ossrs/srs/releases/tag/v3.0-b0), 3.0 beta0, 3.0.112, 121709 lines.
* 2019-10-04, [Release v3.0-a0](https://github.com/ossrs/srs/releases/tag/v3.0-a0), 3.0 alpha0, 3.0.56, 107946 lines.
* 2017-03-03, [Release v2.0-r0](https://github.com/ossrs/srs/releases/tag/v2.0-r0), 2.0 release0, 2.0.234, 86373 lines.
* 2016-08-06, [Release v2.0-b0](https://github.com/ossrs/srs/releases/tag/v2.0-b0), 2.0 beta0, 2.0.210, 89704 lines.
* 2015-08-23, [Release v2.0-a0](https://github.com/ossrs/srs/releases/tag/v2.0-a0), 2.0 alpha0, 2.0.185, 89022 lines.
* 2014-12-05, [Release v1.0-r0](https://github.com/ossrs/srs/releases/tag/v1.0-r0), all bug fixed, 1.0.10, 59391 lines.
* 2014-10-09, [Release v0.9.8](https://github.com/ossrs/srs/releases/tag/v0.9.8), all bug fixed, 1.0.0, 59316 lines.
* 2014-04-07, [Release v0.9.1](https://github.com/ossrs/srs/releases/tag/v0.9.1), live streaming. 30000 lines.
* 2013-10-23, [Release v0.1.0](https://github.com/ossrs/srs/releases/tag/v0.1.0), rtmp. 8287 lines.
* 2013-10-17, Created.

## Features

Please read [FEATURES](trunk/doc/Features.md#features).

<a name="history"></a>
<a name="change-logs"></a>

## Changelog

Please read [CHANGELOG](trunk/doc/CHANGELOG.md#changelog).

## Compare

Comparing with other media servers, SRS is much better and stronger, for details please 
read Product([CN](https://github.com/ossrs/srs/wiki/v4_CN_Compare)/[EN](https://github.com/ossrs/srs/wiki/v4_EN_Compare)).

## Performance

Please read [PERFORMANCE](trunk/doc/PERFORMANCE.md#performance).

## Architecture

Please read [ARCHITECTURE](trunk/doc/Architecture.md#architecture).

## Ports

Please read [PORTS](trunk/doc/Resources.md#ports).

## APIs

Please read [APIS](trunk/doc/Resources.md#apis).

## Mirrors

Please read [MIRRORS](trunk/doc/Resources.md#mirrors).

Beijing, 2013.10<br/>
Winlin

