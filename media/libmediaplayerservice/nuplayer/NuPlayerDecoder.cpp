/*
 * Copyright (C) 2010 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

//#define LOG_NDEBUG 0
#define LOG_TAG "NuPlayerDecoder"
#include <utils/Log.h>

#include "NuPlayerDecoder.h"

#include "ESDS.h"

#include <media/stagefright/foundation/ABuffer.h>
#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/foundation/AMessage.h>
#include <media/stagefright/ACodec.h>
#include <media/stagefright/MediaDefs.h>
#include <media/stagefright/MetaData.h>
#include <media/stagefright/Utils.h>

namespace android {

NuPlayer::Decoder::Decoder(
        const sp<AMessage> &notify,
        const sp<NativeWindowWrapper> &nativeWindow)
    : mNotify(notify),
      mInitFlag(false),
      mNativeWindow(nativeWindow) {
}

NuPlayer::Decoder::~Decoder() {
}

void NuPlayer::Decoder::configure(const sp<MetaData> &meta) {
    CHECK(mCodec == NULL);

    const char *mime;
    CHECK(meta->findCString(kKeyMIMEType, &mime));

    sp<AMessage> notifyMsg =
        new AMessage(kWhatCodecNotify, id());

    sp<AMessage> format = makeFormat(meta);

    if (mNativeWindow != NULL) {
        format->setObject("native-window", mNativeWindow);
    }

    // Current video decoders do not return from OMX_FillThisBuffer
    // quickly, violating the OpenMAX specs, until that is remedied
    // we need to invest in an extra looper to free the main event
    // queue.
    bool needDedicatedLooper = !strncasecmp(mime, "video/", 6);

    mCodec = new ACodec;

    if (needDedicatedLooper && mCodecLooper == NULL) {
        mCodecLooper = new ALooper;
        mCodecLooper->setName("NuPlayerDecoder");
        mCodecLooper->start(false, false, ANDROID_PRIORITY_AUDIO);
    }

    (needDedicatedLooper ? mCodecLooper : looper())->registerHandler(mCodec);

    mCodec->setNotificationMessage(notifyMsg);
    mCodec->initiateSetup(format);
}

void NuPlayer::Decoder::onMessageReceived(const sp<AMessage> &msg) {
    switch (msg->what()) {
        case kWhatCodecNotify:
        {
            int32_t what;
            CHECK(msg->findInt32("what", &what));

            if (what == ACodec::kWhatFillThisBuffer) {
	        if(!mInitFlag){
		        mInitFlag = true;
		    }
                onFillThisBuffer(msg);
            } else {
                sp<AMessage> notify = mNotify->dup();
                notify->setMessage("codec-request", msg);
                notify->post();
            }
            break;
        }

        default:
            TRESPASS();
            break;
    }
}

sp<AMessage> NuPlayer::Decoder::makeFormat(const sp<MetaData> &meta) {
    CHECK(mCSD.isEmpty());

    sp<AMessage> msg;
    CHECK_EQ(convertMetaDataToMessage(meta, &msg), (status_t)OK);

    mCSDIndex = 0;
    for (size_t i = 0;; ++i) {
        sp<ABuffer> csd;
        if (!msg->findBuffer(StringPrintf("csd-%d", i).c_str(), &csd)) {
            break;
        }

        mCSD.push(csd);
    }

    return msg;
}

void NuPlayer::Decoder::onFillThisBuffer(const sp<AMessage> &msg) {
    sp<AMessage> reply;
    CHECK(msg->findMessage("reply", &reply));

#if 0
    sp<ABuffer> outBuffer;
    CHECK(msg->findBuffer("buffer", &outBuffer));
#else
    sp<ABuffer> outBuffer;
#endif

    if (mCSDIndex < mCSD.size()) {
        outBuffer = mCSD.editItemAt(mCSDIndex++);
        outBuffer->meta()->setInt64("timeUs", 0);

        reply->setBuffer("buffer", outBuffer);
        reply->post();
        return;
    }

    sp<AMessage> notify = mNotify->dup();
    notify->setMessage("codec-request", msg);
    notify->post();
}

void NuPlayer::Decoder::signalFlush() {
    if (mCodec != NULL) {
        mCodec->signalFlush();
    }
}
int32_t NuPlayer::Decoder::checkinit(){
    return mInitFlag;
}
void NuPlayer::Decoder::signalResume() {
    if (mCodec != NULL) {
        mCodec->signalResume();
    }
}

void NuPlayer::Decoder::initiateShutdown() {
    if (mCodec != NULL) {
        mCodec->initiateShutdown();
    }
}

}  // namespace android

