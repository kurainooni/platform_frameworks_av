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

#ifndef AVC_ENCODER_H_

#define AVC_ENCODER_H_

#include <media/stagefright/MediaBuffer.h>
#include <media/stagefright/MediaSource.h>
#include <utils/Vector.h>


#define ON2_AVC_ENC 1

struct tagAVCHandle;
struct tagAVCEncParam;

namespace android {

struct MediaBuffer;
struct MediaBufferGroup;

struct AVCEncoder : public MediaSource,
                    public MediaBufferObserver {
    AVCEncoder(const sp<MediaSource> &source,
            const sp<MetaData>& meta);

    virtual status_t start(MetaData *params);
    virtual status_t stop();

    virtual sp<MetaData> getFormat();

	status_t get_encoder_param(void *param);

	status_t set_encoder_param(void *param);
	void Set_IDR_Frame();
	void Set_IDR_Interval(int64_t interval_time);
	void Set_Flag(int flag);
    virtual status_t read(
            MediaBuffer **buffer, const ReadOptions *options);

    virtual void signalBufferReturned(MediaBuffer *buffer);

    // Callbacks required by the encoder
    int32_t allocOutputBuffers(unsigned int sizeInMbs, unsigned int numBuffers);
    void    unbindOutputBuffer(int32_t index);
    int32_t bindOutputBuffer(int32_t index, uint8_t **yuv);

protected:
    virtual ~AVCEncoder();

private:
    sp<MediaSource> mSource;
    sp<MetaData>    mFormat;
    sp<MetaData>    mMeta;

    int32_t  wimo_flag;
    int32_t  mVideoWidth;
    int32_t  mVideoHeight;
    int32_t  mVideoFrameRate;
    int32_t  mVideoBitRate;
    int32_t  mVideoColorFormat;
    int64_t  mNumInputFrames;
    int64_t  mPrevTimestampUs;
    int64_t  mPrev_IDR_TimestampUs;
    status_t mInitCheck;
    bool     mStarted;
    bool     mSpsPpsHeaderReceived;
    bool     mReadyForNextFrame;
    int32_t  mIsIDRFrame;  // for set kKeyIsSyncFrame
    bool     skipFlag;
    int64_t  totaldealt;
	int64_t  mIDR_Interval_time;
    tagAVCHandle          *mHandle;
    tagAVCEncParam        *mEncParams;
    MediaBuffer           *mInputBuffer;
    uint8_t               *mInputFrameData;
    MediaBufferGroup      *mGroup;
    Vector<MediaBuffer *> mOutputBuffers;


    status_t initCheck(const sp<MetaData>& meta);
    void releaseOutputBuffers();

    AVCEncoder(const AVCEncoder &);
    AVCEncoder &operator=(const AVCEncoder &);
#if ON2_AVC_ENC
	void *avc_enc_api;
    void *vp8_enc_api;
#endif
	
};

}  // namespace android

#endif  // AVC_ENCODER_H_
