// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef WEBKIT_RENDERER_MEDIA_WEBMEDIAPLAYER_UTIL_H_
#define WEBKIT_RENDERER_MEDIA_WEBMEDIAPLAYER_UTIL_H_

#include "base/time.h"
#include "media/base/pipeline_status.h"
#include "media/base/ranges.h"
#include "third_party/WebKit/public/web/WebMediaPlayer.h"
#include "third_party/WebKit/public/web/WebTimeRange.h"

namespace webkit_media {

// Platform independent method for converting and rounding floating point
// seconds to an int64 timestamp.
//
// Refer to https://bugs.webkit.org/show_bug.cgi?id=52697 for details.
base::TimeDelta ConvertSecondsToTimestamp(double seconds);

WebKit::WebTimeRanges ConvertToWebTimeRanges(
    const media::Ranges<base::TimeDelta>& ranges);

WebKit::WebMediaPlayer::NetworkState PipelineErrorToNetworkState(
    media::PipelineStatus error);

}  // namespace webkit_media

#endif  // WEBKIT_RENDERER_MEDIA_WEBMEDIAPLAYER_UTIL_H_
