/*
**
** Copyright 2007, The Android Open Source Project
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/

#ifndef ANDROID_AUDIO_FLINGER_H
#define ANDROID_AUDIO_FLINGER_H

#include <stdint.h>
#include <sys/types.h>
#include <limits.h>

#include <common_time/cc_helper.h>

#include <media/IAudioFlinger.h>
#include <media/IAudioFlingerClient.h>
#include <media/IAudioTrack.h>
#include <media/IAudioRecord.h>
#include <media/AudioSystem.h>
#include <media/AudioTrack.h>

#include <utils/Atomic.h>
#include <utils/Errors.h>
#include <utils/threads.h>
#include <utils/SortedVector.h>
#include <utils/TypeHelpers.h>
#include <utils/Vector.h>

#include <binder/BinderService.h>
#include <binder/MemoryDealer.h>

#include <system/audio.h>
#include <hardware/audio.h>
#include <hardware/audio_policy.h>

#include "AudioBufferProvider.h"
#include "ExtendedAudioBufferProvider.h"
#include "FastMixer.h"
#include "NBAIO.h"
#include "AudioWatchdog.h"

#include <powermanager/IPowerManager.h>

namespace android {

class audio_track_cblk_t;
class effect_param_cblk_t;
class AudioMixer;
class AudioBuffer;
class AudioResampler;
class FastMixer;

// ----------------------------------------------------------------------------

// AudioFlinger has a hard-coded upper limit of 2 channels for capture and playback.
// There is support for > 2 channel tracks down-mixed to 2 channel output via a down-mix effect.
// Adding full support for > 2 channel capture or playback would require more than simply changing
// this #define.  There is an independent hard-coded upper limit in AudioMixer;
// removing that AudioMixer limit would be necessary but insufficient to support > 2 channels.
// The macro FCC_2 highlights some (but not all) places where there is are 2-channel assumptions.
// Search also for "2", "left", "right", "[0]", "[1]", ">> 16", "<< 16", etc.
#define FCC_2 2     // FCC_2 = Fixed Channel Count 2

#ifdef TARGET_RK2928
static const nsecs_t kDefaultStandbyTimeInNsecs = seconds(0);
#else	
static const nsecs_t kDefaultStandbyTimeInNsecs = seconds(3);
#endif 

class AudioFlinger :
    public BinderService<AudioFlinger>,
    public BnAudioFlinger
{
    friend class BinderService<AudioFlinger>;   // for AudioFlinger()
public:
    static const char* getServiceName() { return "media.audio_flinger"; }

    virtual     status_t    dump(int fd, const Vector<String16>& args);

    // IAudioFlinger interface, in binder opcode order
    virtual sp<IAudioTrack> createTrack(
                                pid_t pid,
                                audio_stream_type_t streamType,
                                uint32_t sampleRate,
                                audio_format_t format,
                                uint32_t channelMask,
                                int frameCount,
                                IAudioFlinger::track_flags_t flags,
                                const sp<IMemory>& sharedBuffer,
                                audio_io_handle_t output,
                                pid_t tid,
                                int *sessionId,
                                status_t *status);

    virtual sp<IAudioRecord> openRecord(
                                pid_t pid,
                                audio_io_handle_t input,
                                uint32_t sampleRate,
                                audio_format_t format,
                                uint32_t channelMask,
                                int frameCount,
                                IAudioFlinger::track_flags_t flags,
                                int *sessionId,
                                status_t *status);

    virtual     uint32_t    sampleRate(audio_io_handle_t output) const;
    virtual     int         channelCount(audio_io_handle_t output) const;
    virtual     audio_format_t format(audio_io_handle_t output) const;
    virtual     size_t      frameCount(audio_io_handle_t output) const;
    virtual     uint32_t    latency(audio_io_handle_t output) const;

    virtual     status_t    setMasterVolume(float value);
    virtual     status_t    setMasterMute(bool muted);

    virtual     float       masterVolume() const;
    virtual     float       masterVolumeSW() const;
    virtual     bool        masterMute() const;

    virtual     status_t    setStreamVolume(audio_stream_type_t stream, float value,
                                            audio_io_handle_t output);
    virtual     status_t    setStreamMute(audio_stream_type_t stream, bool muted);

    virtual     float       streamVolume(audio_stream_type_t stream,
                                         audio_io_handle_t output) const;
    virtual     bool        streamMute(audio_stream_type_t stream) const;

    virtual     status_t    setMode(audio_mode_t mode);

    virtual     status_t    setMicMute(bool state);
    virtual     bool        getMicMute() const;

    virtual     status_t    setParameters(audio_io_handle_t ioHandle, const String8& keyValuePairs);
    virtual     String8     getParameters(audio_io_handle_t ioHandle, const String8& keys) const;

    virtual     void        registerClient(const sp<IAudioFlingerClient>& client);

    virtual     size_t      getInputBufferSize(uint32_t sampleRate, audio_format_t format, int channelCount) const;

    virtual audio_io_handle_t openOutput(audio_module_handle_t module,
                                         audio_devices_t *pDevices,
                                         uint32_t *pSamplingRate,
                                         audio_format_t *pFormat,
                                         audio_channel_mask_t *pChannelMask,
                                         uint32_t *pLatencyMs,
                                         audio_output_flags_t flags);

    virtual audio_io_handle_t openDuplicateOutput(audio_io_handle_t output1,
                                                  audio_io_handle_t output2);

    virtual status_t closeOutput(audio_io_handle_t output);

    virtual status_t suspendOutput(audio_io_handle_t output);

    virtual status_t restoreOutput(audio_io_handle_t output);

    virtual audio_io_handle_t openInput(audio_module_handle_t module,
                                        audio_devices_t *pDevices,
                                        uint32_t *pSamplingRate,
                                        audio_format_t *pFormat,
                                        audio_channel_mask_t *pChannelMask);

    virtual status_t closeInput(audio_io_handle_t input);

    virtual status_t setStreamOutput(audio_stream_type_t stream, audio_io_handle_t output);

    virtual status_t setVoiceVolume(float volume);

    virtual status_t getRenderPosition(uint32_t *halFrames, uint32_t *dspFrames,
                                       audio_io_handle_t output) const;

    virtual     unsigned int  getInputFramesLost(audio_io_handle_t ioHandle) const;

    virtual int newAudioSessionId();

    virtual void acquireAudioSessionId(int audioSession);

    virtual void releaseAudioSessionId(int audioSession);

    virtual status_t queryNumberEffects(uint32_t *numEffects) const;

    virtual status_t queryEffect(uint32_t index, effect_descriptor_t *descriptor) const;

    virtual status_t getEffectDescriptor(const effect_uuid_t *pUuid,
                                         effect_descriptor_t *descriptor) const;

    virtual sp<IEffect> createEffect(pid_t pid,
                        effect_descriptor_t *pDesc,
                        const sp<IEffectClient>& effectClient,
                        int32_t priority,
                        audio_io_handle_t io,
                        int sessionId,
                        status_t *status,
                        int *id,
                        int *enabled);

    virtual status_t moveEffects(int sessionId, audio_io_handle_t srcOutput,
                        audio_io_handle_t dstOutput);

    virtual audio_module_handle_t loadHwModule(const char *name);

    virtual     status_t    onTransact(
                                uint32_t code,
                                const Parcel& data,
                                Parcel* reply,
                                uint32_t flags);

    // end of IAudioFlinger interface

    class SyncEvent;

    typedef void (*sync_event_callback_t)(const wp<SyncEvent>& event) ;

    class SyncEvent : public RefBase {
    public:
        SyncEvent(AudioSystem::sync_event_t type,
                  int triggerSession,
                  int listenerSession,
                  sync_event_callback_t callBack,
                  void *cookie)
        : mType(type), mTriggerSession(triggerSession), mListenerSession(listenerSession),
          mCallback(callBack), mCookie(cookie)
        {}

        virtual ~SyncEvent() {}

        void trigger() { Mutex::Autolock _l(mLock); if (mCallback) mCallback(this); }
        bool isCancelled() { Mutex::Autolock _l(mLock); return (mCallback == NULL); }
        void cancel() {Mutex::Autolock _l(mLock); mCallback = NULL; }
        AudioSystem::sync_event_t type() const { return mType; }
        int triggerSession() const { return mTriggerSession; }
        int listenerSession() const { return mListenerSession; }
        void *cookie() const { return mCookie; }

    private:
          const AudioSystem::sync_event_t mType;
          const int mTriggerSession;
          const int mListenerSession;
          sync_event_callback_t mCallback;
          void * const mCookie;
          Mutex mLock;
    };

    sp<SyncEvent> createSyncEvent(AudioSystem::sync_event_t type,
                                        int triggerSession,
                                        int listenerSession,
                                        sync_event_callback_t callBack,
                                        void *cookie);

private:
               audio_mode_t getMode() const { return mMode; }

                bool        btNrecIsOff() const { return mBtNrecIsOff; }

                            AudioFlinger();
    virtual                 ~AudioFlinger();

    // call in any IAudioFlinger method that accesses mPrimaryHardwareDev
    status_t                initCheck() const { return mPrimaryHardwareDev == NULL ? NO_INIT : NO_ERROR; }

    // RefBase
    virtual     void        onFirstRef();

    audio_hw_device_t*      findSuitableHwDev_l(audio_module_handle_t module, uint32_t devices);
    void                    purgeStaleEffects_l();

    // standby delay for MIXER and DUPLICATING playback threads is read from property
    // ro.audio.flinger_standbytime_ms or defaults to kDefaultStandbyTimeInNsecs
    static nsecs_t          mStandbyTimeInNsecs;

    // Internal dump utilites.
    status_t dumpPermissionDenial(int fd, const Vector<String16>& args);
    status_t dumpClients(int fd, const Vector<String16>& args);
    status_t dumpInternals(int fd, const Vector<String16>& args);

    // --- Client ---
    class Client : public RefBase {
    public:
                            Client(const sp<AudioFlinger>& audioFlinger, pid_t pid);
        virtual             ~Client();
        sp<MemoryDealer>    heap() const;
        pid_t               pid() const { return mPid; }
        sp<AudioFlinger>    audioFlinger() const { return mAudioFlinger; }

        bool reserveTimedTrack();
        void releaseTimedTrack();

    private:
                            Client(const Client&);
                            Client& operator = (const Client&);
        const sp<AudioFlinger> mAudioFlinger;
        const sp<MemoryDealer> mMemoryDealer;
        const pid_t         mPid;

        Mutex               mTimedTrackLock;
        int                 mTimedTrackCount;
    };

    // --- Notification Client ---
    class NotificationClient : public IBinder::DeathRecipient {
    public:
                            NotificationClient(const sp<AudioFlinger>& audioFlinger,
                                                const sp<IAudioFlingerClient>& client,
                                                pid_t pid);
        virtual             ~NotificationClient();

                sp<IAudioFlingerClient> audioFlingerClient() const { return mAudioFlingerClient; }

                // IBinder::DeathRecipient
                virtual     void        binderDied(const wp<IBinder>& who);

    private:
                            NotificationClient(const NotificationClient&);
                            NotificationClient& operator = (const NotificationClient&);

        const sp<AudioFlinger>  mAudioFlinger;
        const pid_t             mPid;
        const sp<IAudioFlingerClient> mAudioFlingerClient;
    };

    class TrackHandle;
    class RecordHandle;
    class RecordThread;
    class PlaybackThread;
    class MixerThread;
    class DirectOutputThread;
    class DuplicatingThread;
    class Track;
    class RecordTrack;
    class EffectModule;
    class EffectHandle;
    class EffectChain;
    struct AudioStreamOut;
    struct AudioStreamIn;

    class ThreadBase : public Thread {
    public:

        enum type_t {
            MIXER,              // Thread class is MixerThread
            DIRECT,             // Thread class is DirectOutputThread
            DUPLICATING,        // Thread class is DuplicatingThread
            RECORD              // Thread class is RecordThread
        };

        ThreadBase (const sp<AudioFlinger>& audioFlinger, audio_io_handle_t id, uint32_t device, type_t type);
        virtual             ~ThreadBase();

        status_t dumpBase(int fd, const Vector<String16>& args);
        status_t dumpEffectChains(int fd, const Vector<String16>& args);

        void clearPowerManager();

        // base for record and playback
        class TrackBase : public ExtendedAudioBufferProvider, public RefBase {

        public:
            enum track_state {
                IDLE,
                TERMINATED,
                FLUSHED,
                STOPPED,
                // next 2 states are currently used for fast tracks only
                STOPPING_1,     // waiting for first underrun
                STOPPING_2,     // waiting for presentation complete
                RESUMING,
                ACTIVE,
                PAUSING,
                PAUSED
            };

                                TrackBase(ThreadBase *thread,
                                        const sp<Client>& client,
                                        uint32_t sampleRate,
                                        audio_format_t format,
                                        uint32_t channelMask,
                                        int frameCount,
                                        const sp<IMemory>& sharedBuffer,
                                        int sessionId);
            virtual             ~TrackBase();

            virtual status_t    start(AudioSystem::sync_event_t event = AudioSystem::SYNC_EVENT_NONE,
                                     int triggerSession = 0) = 0;
            virtual void        stop() = 0;
                    sp<IMemory> getCblk() const { return mCblkMemory; }
                    audio_track_cblk_t* cblk() const { return mCblk; }
                    int         sessionId() const { return mSessionId; }
            virtual status_t    setSyncEvent(const sp<SyncEvent>& event);

        protected:
                                TrackBase(const TrackBase&);
                                TrackBase& operator = (const TrackBase&);

            // AudioBufferProvider interface
            virtual status_t getNextBuffer(AudioBufferProvider::Buffer* buffer, int64_t pts) = 0;
            virtual void releaseBuffer(AudioBufferProvider::Buffer* buffer);

            // ExtendedAudioBufferProvider interface is only needed for Track,
            // but putting it in TrackBase avoids the complexity of virtual inheritance
            virtual size_t  framesReady() const { return SIZE_MAX; }

            audio_format_t format() const {
                return mFormat;
            }

            int channelCount() const { return mChannelCount; }

            uint32_t channelMask() const { return mChannelMask; }

            int sampleRate() const; // FIXME inline after cblk sr moved

            void* getBuffer(uint32_t offset, uint32_t frames) const;

            bool isStopped() const {
                return (mState == STOPPED || mState == FLUSHED);
            }

            // for fast tracks only
            bool isStopping() const {
                return mState == STOPPING_1 || mState == STOPPING_2;
            }
            bool isStopping_1() const {
                return mState == STOPPING_1;
            }
            bool isStopping_2() const {
                return mState == STOPPING_2;
            }

            bool isTerminated() const {
                return mState == TERMINATED;
            }

            bool step();
            void reset();

            const wp<ThreadBase> mThread;
            /*const*/ sp<Client> mClient;   // see explanation at ~TrackBase() why not const
            sp<IMemory>         mCblkMemory;
            audio_track_cblk_t* mCblk;
            void*               mBuffer;
            void*               mBufferEnd;
            uint32_t            mFrameCount;
            // we don't really need a lock for these
            track_state         mState;
            const uint32_t      mSampleRate;    // initial sample rate only; for tracks which
                                // support dynamic rates, the current value is in control block
            const audio_format_t mFormat;
            bool                mStepServerFailed;
            const int           mSessionId;
            uint8_t             mChannelCount;
            uint32_t            mChannelMask;
            Vector < sp<SyncEvent> >mSyncEvents;
        };

        class ConfigEvent {
        public:
            ConfigEvent() : mEvent(0), mParam(0) {}

            int mEvent;
            int mParam;
        };

        class PMDeathRecipient : public IBinder::DeathRecipient {
        public:
                        PMDeathRecipient(const wp<ThreadBase>& thread) : mThread(thread) {}
            virtual     ~PMDeathRecipient() {}

            // IBinder::DeathRecipient
            virtual     void        binderDied(const wp<IBinder>& who);

        private:
                        PMDeathRecipient(const PMDeathRecipient&);
                        PMDeathRecipient& operator = (const PMDeathRecipient&);

            wp<ThreadBase> mThread;
        };

        virtual     status_t    initCheck() const = 0;
                    type_t      type() const { return mType; }
                    uint32_t    sampleRate() const { return mSampleRate; }
                    int         channelCount() const { return mChannelCount; }
                    audio_format_t format() const { return mFormat; }
                    // Called by AudioFlinger::frameCount(audio_io_handle_t output) and effects,
                    // and returns the normal mix buffer's frame count.  No API for HAL frame count.
                    size_t      frameCount() const { return mNormalFrameCount; }
                    void        wakeUp()    { mWaitWorkCV.broadcast(); }
        // Should be "virtual status_t requestExitAndWait()" and override same
        // method in Thread, but Thread::requestExitAndWait() is not yet virtual.
                    void        exit();
        virtual     bool        checkForNewParameters_l() = 0;
        virtual     status_t    setParameters(const String8& keyValuePairs);
        virtual     String8     getParameters(const String8& keys) = 0;
        virtual     void        audioConfigChanged_l(int event, int param = 0) = 0;
                    void        sendConfigEvent(int event, int param = 0);
                    void        sendConfigEvent_l(int event, int param = 0);
                    void        processConfigEvents();
                    audio_io_handle_t id() const { return mId;}
                    bool        standby() const { return mStandby; }
                    uint32_t    device() const { return mDevice; }
        virtual     audio_stream_t* stream() const = 0;

                    sp<EffectHandle> createEffect_l(
                                        const sp<AudioFlinger::Client>& client,
                                        const sp<IEffectClient>& effectClient,
                                        int32_t priority,
                                        int sessionId,
                                        effect_descriptor_t *desc,
                                        int *enabled,
                                        status_t *status);
                    void disconnectEffect(const sp< EffectModule>& effect,
                                          const wp<EffectHandle>& handle,
                                          bool unpinIfLast);

                    // return values for hasAudioSession (bit field)
                    enum effect_state {
                        EFFECT_SESSION = 0x1,   // the audio session corresponds to at least one
                                                // effect
                        TRACK_SESSION = 0x2     // the audio session corresponds to at least one
                                                // track
                    };

                    // get effect chain corresponding to session Id.
                    sp<EffectChain> getEffectChain(int sessionId);
                    // same as getEffectChain() but must be called with ThreadBase mutex locked
                    sp<EffectChain> getEffectChain_l(int sessionId);
                    // add an effect chain to the chain list (mEffectChains)
        virtual     status_t addEffectChain_l(const sp<EffectChain>& chain) = 0;
                    // remove an effect chain from the chain list (mEffectChains)
        virtual     size_t removeEffectChain_l(const sp<EffectChain>& chain) = 0;
                    // lock all effect chains Mutexes. Must be called before releasing the
                    // ThreadBase mutex before processing the mixer and effects. This guarantees the
                    // integrity of the chains during the process.
                    // Also sets the parameter 'effectChains' to current value of mEffectChains.
                    void lockEffectChains_l(Vector< sp<EffectChain> >& effectChains);
                    // unlock effect chains after process
                    void unlockEffectChains(const Vector< sp<EffectChain> >& effectChains);
                    // set audio mode to all effect chains
                    void setMode(audio_mode_t mode);
                    // get effect module with corresponding ID on specified audio session
                    sp<AudioFlinger::EffectModule> getEffect(int sessionId, int effectId);
                    sp<AudioFlinger::EffectModule> getEffect_l(int sessionId, int effectId);
                    // add and effect module. Also creates the effect chain is none exists for
                    // the effects audio session
                    status_t addEffect_l(const sp< EffectModule>& effect);
                    // remove and effect module. Also removes the effect chain is this was the last
                    // effect
                    void removeEffect_l(const sp< EffectModule>& effect);
                    // detach all tracks connected to an auxiliary effect
        virtual     void detachAuxEffect_l(int effectId) {}
                    // returns either EFFECT_SESSION if effects on this audio session exist in one
                    // chain, or TRACK_SESSION if tracks on this audio session exist, or both
                    virtual uint32_t hasAudioSession(int sessionId) = 0;
                    // the value returned by default implementation is not important as the
                    // strategy is only meaningful for PlaybackThread which implements this method
                    virtual uint32_t getStrategyForSession_l(int sessionId) { return 0; }

                    // suspend or restore effect according to the type of effect passed. a NULL
                    // type pointer means suspend all effects in the session
                    void setEffectSuspended(const effect_uuid_t *type,
                                            bool suspend,
                                            int sessionId = AUDIO_SESSION_OUTPUT_MIX);
                    // check if some effects must be suspended/restored when an effect is enabled
                    // or disabled
                    void checkSuspendOnEffectEnabled(const sp<EffectModule>& effect,
                                                     bool enabled,
                                                     int sessionId = AUDIO_SESSION_OUTPUT_MIX);
                    void checkSuspendOnEffectEnabled_l(const sp<EffectModule>& effect,
                                                       bool enabled,
                                                       int sessionId = AUDIO_SESSION_OUTPUT_MIX);

                    virtual status_t    setSyncEvent(const sp<SyncEvent>& event) = 0;
                    virtual bool        isValidSyncEvent(const sp<SyncEvent>& event) = 0;


        mutable     Mutex                   mLock;

    protected:

                    // entry describing an effect being suspended in mSuspendedSessions keyed vector
                    class SuspendedSessionDesc : public RefBase {
                    public:
                        SuspendedSessionDesc() : mRefCount(0) {}

                        int mRefCount;          // number of active suspend requests
                        effect_uuid_t mType;    // effect type UUID
                    };

                    void        acquireWakeLock();
                    void        acquireWakeLock_l();
                    void        releaseWakeLock();
                    void        releaseWakeLock_l();
                    void setEffectSuspended_l(const effect_uuid_t *type,
                                              bool suspend,
                                              int sessionId = AUDIO_SESSION_OUTPUT_MIX);
                    // updated mSuspendedSessions when an effect suspended or restored
                    void        updateSuspendedSessions_l(const effect_uuid_t *type,
                                                          bool suspend,
                                                          int sessionId);
                    // check if some effects must be suspended when an effect chain is added
                    void checkSuspendOnAddEffectChain_l(const sp<EffectChain>& chain);

        friend class AudioFlinger;      // for mEffectChains

                    const type_t            mType;

                    // Used by parameters, config events, addTrack_l, exit
                    Condition               mWaitWorkCV;

                    const sp<AudioFlinger>  mAudioFlinger;
                    uint32_t                mSampleRate;
                    size_t                  mFrameCount;       // output HAL, direct output, record
                    size_t                  mNormalFrameCount; // normal mixer and effects
                    uint32_t                mChannelMask;
                    uint16_t                mChannelCount;
                    size_t                  mFrameSize;
                    audio_format_t          mFormat;

                    // Parameter sequence by client: binder thread calling setParameters():
                    //  1. Lock mLock
                    //  2. Append to mNewParameters
                    //  3. mWaitWorkCV.signal
                    //  4. mParamCond.waitRelative with timeout
                    //  5. read mParamStatus
                    //  6. mWaitWorkCV.signal
                    //  7. Unlock
                    //
                    // Parameter sequence by server: threadLoop calling checkForNewParameters_l():
                    // 1. Lock mLock
                    // 2. If there is an entry in mNewParameters proceed ...
                    // 2. Read first entry in mNewParameters
                    // 3. Process
                    // 4. Remove first entry from mNewParameters
                    // 5. Set mParamStatus
                    // 6. mParamCond.signal
                    // 7. mWaitWorkCV.wait with timeout (this is to avoid overwriting mParamStatus)
                    // 8. Unlock
                    Condition               mParamCond;
                    Vector<String8>         mNewParameters;
                    status_t                mParamStatus;

                    Vector<ConfigEvent>     mConfigEvents;
                    bool                    mStandby;
                    const audio_io_handle_t mId;
                    Vector< sp<EffectChain> > mEffectChains;
                    uint32_t                mDevice;    // output device for PlaybackThread
                                                        // input + output devices for RecordThread
                    static const int        kNameLength = 16;   // prctl(PR_SET_NAME) limit
                    char                    mName[kNameLength];
                    sp<IPowerManager>       mPowerManager;
                    sp<IBinder>             mWakeLockToken;
                    const sp<PMDeathRecipient> mDeathRecipient;
                    // list of suspended effects per session and per type. The first vector is
                    // keyed by session ID, the second by type UUID timeLow field
                    KeyedVector< int, KeyedVector< int, sp<SuspendedSessionDesc> > >  mSuspendedSessions;
    };

    struct  stream_type_t {
        stream_type_t()
            :   volume(1.0f),
                mute(false)
        {
        }
        float       volume;
        bool        mute;
    };

    // --- PlaybackThread ---
    class PlaybackThread : public ThreadBase {
    public:

        enum mixer_state {
            MIXER_IDLE,             // no active tracks
            MIXER_TRACKS_ENABLED,   // at least one active track, but no track has any data ready
            MIXER_TRACKS_READY      // at least one active track, and at least one track has data
            // standby mode does not have an enum value
            // suspend by audio policy manager is orthogonal to mixer state
        };

        // playback track
        class Track : public TrackBase, public VolumeProvider {
        public:
                                Track(  PlaybackThread *thread,
                                        const sp<Client>& client,
                                        audio_stream_type_t streamType,
                                        uint32_t sampleRate,
                                        audio_format_t format,
                                        uint32_t channelMask,
                                        int frameCount,
                                        const sp<IMemory>& sharedBuffer,
                                        int sessionId,
                                        IAudioFlinger::track_flags_t flags);
            virtual             ~Track();

            static  void        appendDumpHeader(String8& result);
                    void        dump(char* buffer, size_t size);
            virtual status_t    start(AudioSystem::sync_event_t event = AudioSystem::SYNC_EVENT_NONE,
                                     int triggerSession = 0);
            virtual void        stop();
                    void        pause();

                    void        flush();
                    void        destroy();
                    void        mute(bool);
                    int name() const {
                        return mName;
                    }

                    audio_stream_type_t streamType() const {
                        return mStreamType;
                    }
                    status_t    attachAuxEffect(int EffectId);
                    void        setAuxBuffer(int EffectId, int32_t *buffer);
                    int32_t     *auxBuffer() const { return mAuxBuffer; }
                    void        setMainBuffer(int16_t *buffer) { mMainBuffer = buffer; }
                    int16_t     *mainBuffer() const { return mMainBuffer; }
                    int         auxEffectId() const { return mAuxEffectId; }

        // implement FastMixerState::VolumeProvider interface
            virtual uint32_t    getVolumeLR();
            virtual status_t    setSyncEvent(const sp<SyncEvent>& event);

        protected:
            // for numerous
            friend class PlaybackThread;
            friend class MixerThread;
            friend class DirectOutputThread;

                                Track(const Track&);
                                Track& operator = (const Track&);

            // AudioBufferProvider interface
            virtual status_t getNextBuffer(AudioBufferProvider::Buffer* buffer, int64_t pts = kInvalidPTS);
            // releaseBuffer() not overridden

            virtual size_t framesReady() const;

            bool isMuted() const { return mMute; }
            bool isPausing() const {
                return mState == PAUSING;
            }
            bool isPaused() const {
                return mState == PAUSED;
            }
            bool isResuming() const {
                return mState == RESUMING;
            }
            bool isReady() const;
            void setPaused() { mState = PAUSED; }
            void reset();

            bool isOutputTrack() const {
                return (mStreamType == AUDIO_STREAM_CNT);
            }

            sp<IMemory> sharedBuffer() const { return mSharedBuffer; }

            bool presentationComplete(size_t framesWritten, size_t audioHalFrames);

        public:
            void triggerEvents(AudioSystem::sync_event_t type);
            virtual bool isTimedTrack() const { return false; }
            bool isFastTrack() const { return (mFlags & IAudioFlinger::TRACK_FAST) != 0; }
        protected:

            // we don't really need a lock for these
            volatile bool       mMute;
            // FILLED state is used for suppressing volume ramp at begin of playing
            enum {FS_INVALID, FS_FILLING, FS_FILLED, FS_ACTIVE};
            mutable uint8_t     mFillingUpStatus;
            int8_t              mRetryCount;
            const sp<IMemory>   mSharedBuffer;
            bool                mResetDone;
            const audio_stream_type_t mStreamType;
            int                 mName;      // track name on the normal mixer,
                                            // allocated statically at track creation time,
                                            // and is even allocated (though unused) for fast tracks
                                            // FIXME don't allocate track name for fast tracks
            int16_t             *mMainBuffer;
            int32_t             *mAuxBuffer;
            int                 mAuxEffectId;
            bool                mHasVolumeController;
            size_t              mPresentationCompleteFrames; // number of frames written to the audio HAL
                                                       // when this track will be fully rendered
        private:
            IAudioFlinger::track_flags_t mFlags;

            // The following fields are only for fast tracks, and should be in a subclass
            int                 mFastIndex; // index within FastMixerState::mFastTracks[];
                                            // either mFastIndex == -1 if not isFastTrack()
                                            // or 0 < mFastIndex < FastMixerState::kMaxFast because
                                            // index 0 is reserved for normal mixer's submix;
                                            // index is allocated statically at track creation time
                                            // but the slot is only used if track is active
            FastTrackUnderruns  mObservedUnderruns; // Most recently observed value of
                                            // mFastMixerDumpState.mTracks[mFastIndex].mUnderruns
            uint32_t            mUnderrunCount; // Counter of total number of underruns, never reset
            volatile float      mCachedVolume;  // combined master volume and stream type volume;
                                                // 'volatile' means accessed without lock or
                                                // barrier, but is read/written atomically
        };  // end of Track

        class TimedTrack : public Track {
          public:
            static sp<TimedTrack> create(PlaybackThread *thread,
                                         const sp<Client>& client,
                                         audio_stream_type_t streamType,
                                         uint32_t sampleRate,
                                         audio_format_t format,
                                         uint32_t channelMask,
                                         int frameCount,
                                         const sp<IMemory>& sharedBuffer,
                                         int sessionId);
            ~TimedTrack();

            class TimedBuffer {
              public:
                TimedBuffer();
                TimedBuffer(const sp<IMemory>& buffer, int64_t pts);
                const sp<IMemory>& buffer() const { return mBuffer; }
                int64_t pts() const { return mPTS; }
                uint32_t position() const { return mPosition; }
                void setPosition(uint32_t pos) { mPosition = pos; }
              private:
                sp<IMemory> mBuffer;
                int64_t     mPTS;
                uint32_t    mPosition;
            };

            // Mixer facing methods.
            virtual bool isTimedTrack() const { return true; }
            virtual size_t framesReady() const;

            // AudioBufferProvider interface
            virtual status_t getNextBuffer(AudioBufferProvider::Buffer* buffer,
                                           int64_t pts);
            virtual void releaseBuffer(AudioBufferProvider::Buffer* buffer);

            // Client/App facing methods.
            status_t    allocateTimedBuffer(size_t size,
                                            sp<IMemory>* buffer);
            status_t    queueTimedBuffer(const sp<IMemory>& buffer,
                                         int64_t pts);
            status_t    setMediaTimeTransform(const LinearTransform& xform,
                                              TimedAudioTrack::TargetTimeline target);

          private:
            TimedTrack(PlaybackThread *thread,
                       const sp<Client>& client,
                       audio_stream_type_t streamType,
                       uint32_t sampleRate,
                       audio_format_t format,
                       uint32_t channelMask,
                       int frameCount,
                       const sp<IMemory>& sharedBuffer,
                       int sessionId);

            void timedYieldSamples_l(AudioBufferProvider::Buffer* buffer);
            void timedYieldSilence_l(uint32_t numFrames,
                                     AudioBufferProvider::Buffer* buffer);
            void trimTimedBufferQueue_l();
            void trimTimedBufferQueueHead_l(const char* logTag);
            void updateFramesPendingAfterTrim_l(const TimedBuffer& buf,
                                                const char* logTag);

            uint64_t            mLocalTimeFreq;
            LinearTransform     mLocalTimeToSampleTransform;
            LinearTransform     mMediaTimeToSampleTransform;
            sp<MemoryDealer>    mTimedMemoryDealer;

            Vector<TimedBuffer> mTimedBufferQueue;
            bool                mQueueHeadInFlight;
            bool                mTrimQueueHeadOnRelease;
            uint32_t            mFramesPendingInQueue;

            uint8_t*            mTimedSilenceBuffer;
            uint32_t            mTimedSilenceBufferSize;
            mutable Mutex       mTimedBufferQueueLock;
            bool                mTimedAudioOutputOnTime;
            CCHelper            mCCHelper;

            Mutex               mMediaTimeTransformLock;
            LinearTransform     mMediaTimeTransform;
            bool                mMediaTimeTransformValid;
            TimedAudioTrack::TargetTimeline mMediaTimeTransformTarget;
        };


        // playback track
        class OutputTrack : public Track {
        public:

            class Buffer: public AudioBufferProvider::Buffer {
            public:
                int16_t *mBuffer;
            };

                                OutputTrack(PlaybackThread *thread,
                                        DuplicatingThread *sourceThread,
                                        uint32_t sampleRate,
                                        audio_format_t format,
                                        uint32_t channelMask,
                                        int frameCount);
            virtual             ~OutputTrack();

            virtual status_t    start(AudioSystem::sync_event_t event = AudioSystem::SYNC_EVENT_NONE,
                                     int triggerSession = 0);
            virtual void        stop();
                    bool        write(int16_t* data, uint32_t frames);
                    bool        bufferQueueEmpty() const { return mBufferQueue.size() == 0; }
                    bool        isActive() const { return mActive; }
            const wp<ThreadBase>& thread() const { return mThread; }

        private:

            enum {
                NO_MORE_BUFFERS = 0x80000001,   // same in AudioTrack.h, ok to be different value
            };

            status_t            obtainBuffer(AudioBufferProvider::Buffer* buffer, uint32_t waitTimeMs);
            void                clearBufferQueue();

            // Maximum number of pending buffers allocated by OutputTrack::write()
            static const uint8_t kMaxOverFlowBuffers = 10;

            Vector < Buffer* >          mBufferQueue;
            AudioBufferProvider::Buffer mOutBuffer;
            bool                        mActive;
            DuplicatingThread* const mSourceThread; // for waitTimeMs() in write()
        };  // end of OutputTrack

        PlaybackThread (const sp<AudioFlinger>& audioFlinger, AudioStreamOut* output,
                        audio_io_handle_t id, uint32_t device, type_t type);
        virtual             ~PlaybackThread();

                    status_t    dump(int fd, const Vector<String16>& args);

        // Thread virtuals
        virtual     status_t    readyToRun();
        virtual     bool        threadLoop();

        // RefBase
        virtual     void        onFirstRef();

protected:
        // Code snippets that were lifted up out of threadLoop()
        virtual     void        threadLoop_mix() = 0;
        virtual     void        threadLoop_sleepTime() = 0;
        virtual     void        threadLoop_write();
        virtual     void        threadLoop_standby();
        virtual     void        threadLoop_removeTracks(const Vector< sp<Track> >& tracksToRemove);

                    // prepareTracks_l reads and writes mActiveTracks, and returns
                    // the pending set of tracks to remove via Vector 'tracksToRemove'.  The caller
                    // is responsible for clearing or destroying this Vector later on, when it
                    // is safe to do so. That will drop the final ref count and destroy the tracks.
        virtual     mixer_state prepareTracks_l(Vector< sp<Track> > *tracksToRemove) = 0;

public:

        virtual     status_t    initCheck() const { return (mOutput == NULL) ? NO_INIT : NO_ERROR; }

                    // return estimated latency in milliseconds, as reported by HAL
                    uint32_t    latency() const;
                    // same, but lock must already be held
                    uint32_t    latency_l() const;

                    void        setMasterVolume(float value);
                    void        setMasterMute(bool muted);

                    void        setStreamVolume(audio_stream_type_t stream, float value);
                    void        setStreamMute(audio_stream_type_t stream, bool muted);

                    float       streamVolume(audio_stream_type_t stream) const;

                    sp<Track>   createTrack_l(
                                    const sp<AudioFlinger::Client>& client,
                                    audio_stream_type_t streamType,
                                    uint32_t sampleRate,
                                    audio_format_t format,
                                    uint32_t channelMask,
                                    int frameCount,
                                    const sp<IMemory>& sharedBuffer,
                                    int sessionId,
                                    IAudioFlinger::track_flags_t flags,
                                    pid_t tid,
                                    status_t *status);

                    AudioStreamOut* getOutput() const;
                    AudioStreamOut* clearOutput();
                    virtual audio_stream_t* stream() const;

                    void        suspend() { mSuspended++; }
                    void        restore() { if (mSuspended > 0) mSuspended--; }
                    bool        isSuspended() const { return (mSuspended > 0); }
        virtual     String8     getParameters(const String8& keys);
        virtual     void        audioConfigChanged_l(int event, int param = 0);
                    status_t    getRenderPosition(uint32_t *halFrames, uint32_t *dspFrames);
                    int16_t     *mixBuffer() const { return mMixBuffer; };

        virtual     void detachAuxEffect_l(int effectId);
                    status_t attachAuxEffect(const sp<AudioFlinger::PlaybackThread::Track> track,
                            int EffectId);
                    status_t attachAuxEffect_l(const sp<AudioFlinger::PlaybackThread::Track> track,
                            int EffectId);

                    virtual status_t addEffectChain_l(const sp<EffectChain>& chain);
                    virtual size_t removeEffectChain_l(const sp<EffectChain>& chain);
                    virtual uint32_t hasAudioSession(int sessionId);
                    virtual uint32_t getStrategyForSession_l(int sessionId);


                    virtual status_t setSyncEvent(const sp<SyncEvent>& event);
                    virtual bool     isValidSyncEvent(const sp<SyncEvent>& event);

    protected:
        int16_t*                        mMixBuffer;
        uint32_t                        mSuspended;     // suspend count, > 0 means suspended
        int                             mBytesWritten;
    private:
        // mMasterMute is in both PlaybackThread and in AudioFlinger.  When a
        // PlaybackThread needs to find out if master-muted, it checks it's local
        // copy rather than the one in AudioFlinger.  This optimization saves a lock.
        bool                            mMasterMute;
                    void        setMasterMute_l(bool muted) { mMasterMute = muted; }
    protected:
        SortedVector< wp<Track> >       mActiveTracks;  // FIXME check if this could be sp<>

        // Allocate a track name for a given channel mask.
        //   Returns name >= 0 if successful, -1 on failure.
        virtual int             getTrackName_l(audio_channel_mask_t channelMask) = 0;
        virtual void            deleteTrackName_l(int name) = 0;

        // Time to sleep between cycles when:
        virtual uint32_t        activeSleepTimeUs() const;      // mixer state MIXER_TRACKS_ENABLED
        virtual uint32_t        idleSleepTimeUs() const = 0;    // mixer state MIXER_IDLE
        virtual uint32_t        suspendSleepTimeUs() const = 0; // audio policy manager suspended us
        // No sleep when mixer state == MIXER_TRACKS_READY; relies on audio HAL stream->write()
        // No sleep in standby mode; waits on a condition

        // Code snippets that are temporarily lifted up out of threadLoop() until the merge
                    void        checkSilentMode_l();

        // Non-trivial for DUPLICATING only
        virtual     void        saveOutputTracks() { }
        virtual     void        clearOutputTracks() { }

        // Cache various calculated values, at threadLoop() entry and after a parameter change
        virtual     void        cacheParameters_l();

        virtual     uint32_t    correctLatency(uint32_t latency) const;

    private:

        friend class AudioFlinger;      // for numerous

        PlaybackThread(const Client&);
        PlaybackThread& operator = (const PlaybackThread&);

        status_t    addTrack_l(const sp<Track>& track);
        void        destroyTrack_l(const sp<Track>& track);
        void        removeTrack_l(const sp<Track>& track);

        void        readOutputParameters();

        virtual status_t    dumpInternals(int fd, const Vector<String16>& args);
        status_t    dumpTracks(int fd, const Vector<String16>& args);

        SortedVector< sp<Track> >       mTracks;
        // mStreamTypes[] uses 1 additional stream type internally for the OutputTrack used by DuplicatingThread
        stream_type_t                   mStreamTypes[AUDIO_STREAM_CNT + 1];
        AudioStreamOut                  *mOutput;
        float                           mMasterVolume;
        nsecs_t                         mLastWriteTime;
        int                             mNumWrites;
        int                             mNumDelayedWrites;
        bool                            mInWrite;

        // FIXME rename these former local variables of threadLoop to standard "m" names
        nsecs_t                         standbyTime;
        size_t                          mixBufferSize;

        // cached copies of activeSleepTimeUs() and idleSleepTimeUs() made by cacheParameters_l()
        uint32_t                        activeSleepTime;
        uint32_t                        idleSleepTime;

        uint32_t                        sleepTime;

        // mixer status returned by prepareTracks_l()
        mixer_state                     mMixerStatus; // current cycle
                                                      // previous cycle when in prepareTracks_l()
        mixer_state                     mMixerStatusIgnoringFastTracks;
                                                      // FIXME or a separate ready state per track

        // FIXME move these declarations into the specific sub-class that needs them
        // MIXER only
        bool                            longStandbyExit;
        uint32_t                        sleepTimeShift;

        // same as AudioFlinger::mStandbyTimeInNsecs except for DIRECT which uses a shorter value
        nsecs_t                         standbyDelay;

        // MIXER only
        nsecs_t                         maxPeriod;

        // DUPLICATING only
        uint32_t                        writeFrames;

    private:
        // The HAL output sink is treated as non-blocking, but current implementation is blocking
        sp<NBAIO_Sink>          mOutputSink;
        // If a fast mixer is present, the blocking pipe sink, otherwise clear
        sp<NBAIO_Sink>          mPipeSink;
        // The current sink for the normal mixer to write it's (sub)mix, mOutputSink or mPipeSink
        sp<NBAIO_Sink>          mNormalSink;
        // For dumpsys
        sp<NBAIO_Sink>          mTeeSink;
        sp<NBAIO_Source>        mTeeSource;
        uint32_t                mScreenState;   // cached copy of gScreenState
    public:
        virtual     bool        hasFastMixer() const = 0;
        virtual     FastTrackUnderruns getFastTrackUnderruns(size_t fastIndex) const
                                    { FastTrackUnderruns dummy; return dummy; }

    protected:
                    // accessed by both binder threads and within threadLoop(), lock on mutex needed
                    unsigned    mFastTrackAvailMask;    // bit i set if fast track [i] is available

    };

    class MixerThread : public PlaybackThread {
    public:
        MixerThread (const sp<AudioFlinger>& audioFlinger,
                     AudioStreamOut* output,
                     audio_io_handle_t id,
                     uint32_t device,
                     type_t type = MIXER);
        virtual             ~MixerThread();

        // Thread virtuals

                    void        invalidateTracks(audio_stream_type_t streamType);
        virtual     bool        checkForNewParameters_l();
        virtual     status_t    dumpInternals(int fd, const Vector<String16>& args);

    protected:
        virtual     mixer_state prepareTracks_l(Vector< sp<Track> > *tracksToRemove);
        virtual     int         getTrackName_l(audio_channel_mask_t channelMask);
        virtual     void        deleteTrackName_l(int name);
        virtual     uint32_t    idleSleepTimeUs() const;
        virtual     uint32_t    suspendSleepTimeUs() const;
        virtual     void        cacheParameters_l();

        // threadLoop snippets
        virtual     void        threadLoop_write();
        virtual     void        threadLoop_standby();
        virtual     void        threadLoop_mix();
        virtual     void        threadLoop_sleepTime();
        virtual     void        threadLoop_removeTracks(const Vector< sp<Track> >& tracksToRemove);
        virtual     uint32_t    correctLatency(uint32_t latency) const;

                    AudioMixer* mAudioMixer;    // normal mixer
    private:
#ifdef SOAKER
                    Thread*     mSoaker;
#endif
                    // one-time initialization, no locks required
                    FastMixer*  mFastMixer;         // non-NULL if there is also a fast mixer
                    sp<AudioWatchdog> mAudioWatchdog; // non-0 if there is an audio watchdog thread

                    // contents are not guaranteed to be consistent, no locks required
                    FastMixerDumpState mFastMixerDumpState;
#ifdef STATE_QUEUE_DUMP
                    StateQueueObserverDump mStateQueueObserverDump;
                    StateQueueMutatorDump  mStateQueueMutatorDump;
#endif
                    AudioWatchdogDump mAudioWatchdogDump;

                    // accessible only within the threadLoop(), no locks required
                    //          mFastMixer->sq()    // for mutating and pushing state
                    int32_t     mFastMixerFutex;    // for cold idle

    public:
        virtual     bool        hasFastMixer() const { return mFastMixer != NULL; }
        virtual     FastTrackUnderruns getFastTrackUnderruns(size_t fastIndex) const {
                                  ALOG_ASSERT(fastIndex < FastMixerState::kMaxFastTracks);
                                  return mFastMixerDumpState.mTracks[fastIndex].mUnderruns;
                                }
    };

    class DirectOutputThread : public PlaybackThread {
    public:

        DirectOutputThread (const sp<AudioFlinger>& audioFlinger, AudioStreamOut* output,
                            audio_io_handle_t id, uint32_t device);
        virtual                 ~DirectOutputThread();

        // Thread virtuals

        virtual     bool        checkForNewParameters_l();

    protected:
        virtual     int         getTrackName_l(audio_channel_mask_t channelMask);
        virtual     void        deleteTrackName_l(int name);
        virtual     uint32_t    activeSleepTimeUs() const;
        virtual     uint32_t    idleSleepTimeUs() const;
        virtual     uint32_t    suspendSleepTimeUs() const;
        virtual     void        cacheParameters_l();

        // threadLoop snippets
        virtual     mixer_state prepareTracks_l(Vector< sp<Track> > *tracksToRemove);
        virtual     void        threadLoop_mix();
        virtual     void        threadLoop_sleepTime();

        // volumes last sent to audio HAL with stream->set_volume()
        float mLeftVolFloat;
        float mRightVolFloat;

private:
        // prepareTracks_l() tells threadLoop_mix() the name of the single active track
        sp<Track>               mActiveTrack;
    public:
        virtual     bool        hasFastMixer() const { return false; }
    };

    class DuplicatingThread : public MixerThread {
    public:
        DuplicatingThread (const sp<AudioFlinger>& audioFlinger, MixerThread* mainThread,
                           audio_io_handle_t id);
        virtual                 ~DuplicatingThread();

        // Thread virtuals
                    void        addOutputTrack(MixerThread* thread);
                    void        removeOutputTrack(MixerThread* thread);
                    uint32_t    waitTimeMs() const { return mWaitTimeMs; }
    protected:
        virtual     uint32_t    activeSleepTimeUs() const;

    private:
                    bool        outputsReady(const SortedVector< sp<OutputTrack> > &outputTracks);
    protected:
        // threadLoop snippets
        virtual     void        threadLoop_mix();
        virtual     void        threadLoop_sleepTime();
        virtual     void        threadLoop_write();
        virtual     void        threadLoop_standby();
        virtual     void        cacheParameters_l();

    private:
        // called from threadLoop, addOutputTrack, removeOutputTrack
        virtual     void        updateWaitTime_l();
    protected:
        virtual     void        saveOutputTracks();
        virtual     void        clearOutputTracks();
    private:

                    uint32_t    mWaitTimeMs;
        SortedVector < sp<OutputTrack> >  outputTracks;
        SortedVector < sp<OutputTrack> >  mOutputTracks;
    public:
        virtual     bool        hasFastMixer() const { return false; }
    };

              PlaybackThread *checkPlaybackThread_l(audio_io_handle_t output) const;
              MixerThread *checkMixerThread_l(audio_io_handle_t output) const;
              RecordThread *checkRecordThread_l(audio_io_handle_t input) const;
              // no range check, AudioFlinger::mLock held
              bool streamMute_l(audio_stream_type_t stream) const
                                { return mStreamTypes[stream].mute; }
              // no range check, doesn't check per-thread stream volume, AudioFlinger::mLock held
              float streamVolume_l(audio_stream_type_t stream) const
                                { return mStreamTypes[stream].volume; }
              void audioConfigChanged_l(int event, audio_io_handle_t ioHandle, const void *param2);

              // allocate an audio_io_handle_t, session ID, or effect ID
              uint32_t nextUniqueId();

              status_t moveEffectChain_l(int sessionId,
                                     PlaybackThread *srcThread,
                                     PlaybackThread *dstThread,
                                     bool reRegister);
              // return thread associated with primary hardware device, or NULL
              PlaybackThread *primaryPlaybackThread_l() const;
              uint32_t primaryOutputDevice_l() const;

              sp<PlaybackThread> getEffectThread_l(int sessionId, int EffectId);

    // server side of the client's IAudioTrack
    class TrackHandle : public android::BnAudioTrack {
    public:
                            TrackHandle(const sp<PlaybackThread::Track>& track);
        virtual             ~TrackHandle();
        virtual sp<IMemory> getCblk() const;
        virtual status_t    start();
        virtual void        stop();
        virtual void        flush();
        virtual void        mute(bool);
        virtual void        pause();
        virtual status_t    attachAuxEffect(int effectId);
        virtual status_t    allocateTimedBuffer(size_t size,
                                                sp<IMemory>* buffer);
        virtual status_t    queueTimedBuffer(const sp<IMemory>& buffer,
                                             int64_t pts);
        virtual status_t    setMediaTimeTransform(const LinearTransform& xform,
                                                  int target);
        virtual status_t onTransact(
            uint32_t code, const Parcel& data, Parcel* reply, uint32_t flags);
    private:
        const sp<PlaybackThread::Track> mTrack;
    };

                void        removeClient_l(pid_t pid);
                void        removeNotificationClient(pid_t pid);


    // record thread
    class RecordThread : public ThreadBase, public AudioBufferProvider
    {
    public:

        // record track
        class RecordTrack : public TrackBase {
        public:
                                RecordTrack(RecordThread *thread,
                                        const sp<Client>& client,
                                        uint32_t sampleRate,
                                        audio_format_t format,
                                        uint32_t channelMask,
                                        int frameCount,
                                        int sessionId);
            virtual             ~RecordTrack();

            virtual status_t    start(AudioSystem::sync_event_t event = AudioSystem::SYNC_EVENT_NONE,
                                     int triggerSession = 0);
            virtual void        stop();

                    bool        overflow() { bool tmp = mOverflow; mOverflow = false; return tmp; }
                    bool        setOverflow() { bool tmp = mOverflow; mOverflow = true; return tmp; }

                    void        dump(char* buffer, size_t size);

        private:
            friend class AudioFlinger;  // for mState

                                RecordTrack(const RecordTrack&);
                                RecordTrack& operator = (const RecordTrack&);

            // AudioBufferProvider interface
            virtual status_t getNextBuffer(AudioBufferProvider::Buffer* buffer, int64_t pts = kInvalidPTS);
            // releaseBuffer() not overridden

            bool                mOverflow;
        };


                RecordThread(const sp<AudioFlinger>& audioFlinger,
                        AudioStreamIn *input,
                        uint32_t sampleRate,
                        uint32_t channels,
                        audio_io_handle_t id,
                        uint32_t device);
                virtual     ~RecordThread();

        // Thread
        virtual bool        threadLoop();
        virtual status_t    readyToRun();

        // RefBase
        virtual void        onFirstRef();

        virtual status_t    initCheck() const { return (mInput == NULL) ? NO_INIT : NO_ERROR; }
                sp<AudioFlinger::RecordThread::RecordTrack>  createRecordTrack_l(
                        const sp<AudioFlinger::Client>& client,
                        uint32_t sampleRate,
                        audio_format_t format,
                        int channelMask,
                        int frameCount,
                        int sessionId,
                        status_t *status);

                status_t    start(RecordTrack* recordTrack,
                                  AudioSystem::sync_event_t event,
                                  int triggerSession);
                void        stop(RecordTrack* recordTrack);
                status_t    dump(int fd, const Vector<String16>& args);
                AudioStreamIn* getInput() const;
                AudioStreamIn* clearInput();
                virtual audio_stream_t* stream() const;

        // AudioBufferProvider interface
        virtual status_t    getNextBuffer(AudioBufferProvider::Buffer* buffer, int64_t pts);
        virtual void        releaseBuffer(AudioBufferProvider::Buffer* buffer);

        virtual bool        checkForNewParameters_l();
        virtual String8     getParameters(const String8& keys);
        virtual void        audioConfigChanged_l(int event, int param = 0);
                void        readInputParameters();
        virtual unsigned int  getInputFramesLost();

        virtual status_t addEffectChain_l(const sp<EffectChain>& chain);
        virtual size_t removeEffectChain_l(const sp<EffectChain>& chain);
        virtual uint32_t hasAudioSession(int sessionId);
                RecordTrack* track();

        virtual status_t setSyncEvent(const sp<SyncEvent>& event);
        virtual bool     isValidSyncEvent(const sp<SyncEvent>& event);

        static void syncStartEventCallback(const wp<SyncEvent>& event);
               void handleSyncStartEvent(const sp<SyncEvent>& event);

    private:
                void clearSyncStartEvent();

                RecordThread();
                AudioStreamIn                       *mInput;
                RecordTrack*                        mTrack;
                sp<RecordTrack>                     mActiveTrack;
                Condition                           mStartStopCond;
                AudioResampler                      *mResampler;
                int32_t                             *mRsmpOutBuffer;
                int16_t                             *mRsmpInBuffer;
                size_t                              mRsmpInIndex;
                size_t                              mInputBytes;
                const int                           mReqChannelCount;
                const uint32_t                      mReqSampleRate;
                ssize_t                             mBytesRead;
                // sync event triggering actual audio capture. Frames read before this event will
                // be dropped and therefore not read by the application.
                sp<SyncEvent>                       mSyncStartEvent;
                // number of captured frames to drop after the start sync event has been received.
                // when < 0, maximum frames to drop before starting capture even if sync event is
                // not received
                ssize_t                             mFramestoDrop;
    };

    // server side of the client's IAudioRecord
    class RecordHandle : public android::BnAudioRecord {
    public:
        RecordHandle(const sp<RecordThread::RecordTrack>& recordTrack);
        virtual             ~RecordHandle();
        virtual sp<IMemory> getCblk() const;
        virtual status_t    start(int event, int triggerSession);
        virtual void        stop();
        virtual status_t onTransact(
            uint32_t code, const Parcel& data, Parcel* reply, uint32_t flags);
    private:
        const sp<RecordThread::RecordTrack> mRecordTrack;
    };

    //--- Audio Effect Management

    // EffectModule and EffectChain classes both have their own mutex to protect
    // state changes or resource modifications. Always respect the following order
    // if multiple mutexes must be acquired to avoid cross deadlock:
    // AudioFlinger -> ThreadBase -> EffectChain -> EffectModule

    // The EffectModule class is a wrapper object controlling the effect engine implementation
    // in the effect library. It prevents concurrent calls to process() and command() functions
    // from different client threads. It keeps a list of EffectHandle objects corresponding
    // to all client applications using this effect and notifies applications of effect state,
    // control or parameter changes. It manages the activation state machine to send appropriate
    // reset, enable, disable commands to effect engine and provide volume
    // ramping when effects are activated/deactivated.
    // When controlling an auxiliary effect, the EffectModule also provides an input buffer used by
    // the attached track(s) to accumulate their auxiliary channel.
    class EffectModule: public RefBase {
    public:
        EffectModule(ThreadBase *thread,
                        const wp<AudioFlinger::EffectChain>& chain,
                        effect_descriptor_t *desc,
                        int id,
                        int sessionId);
        virtual ~EffectModule();

        enum effect_state {
            IDLE,
            RESTART,
            STARTING,
            ACTIVE,
            STOPPING,
            STOPPED,
            DESTROYED
        };

        int         id() const { return mId; }
        void process();
        void updateState();
        status_t command(uint32_t cmdCode,
                         uint32_t cmdSize,
                         void *pCmdData,
                         uint32_t *replySize,
                         void *pReplyData);

        void reset_l();
        status_t configure();
        status_t init();
        effect_state state() const {
            return mState;
        }
        uint32_t status() {
            return mStatus;
        }
        int sessionId() const {
            return mSessionId;
        }
        status_t    setEnabled(bool enabled);
        bool isEnabled() const;
        bool isProcessEnabled() const;

        void        setInBuffer(int16_t *buffer) { mConfig.inputCfg.buffer.s16 = buffer; }
        int16_t     *inBuffer() { return mConfig.inputCfg.buffer.s16; }
        void        setOutBuffer(int16_t *buffer) { mConfig.outputCfg.buffer.s16 = buffer; }
        int16_t     *outBuffer() { return mConfig.outputCfg.buffer.s16; }
        void        setChain(const wp<EffectChain>& chain) { mChain = chain; }
        void        setThread(const wp<ThreadBase>& thread) { mThread = thread; }
        const wp<ThreadBase>& thread() { return mThread; }

        status_t addHandle(const sp<EffectHandle>& handle);
        void disconnect(const wp<EffectHandle>& handle, bool unpinIfLast);
        size_t removeHandle (const wp<EffectHandle>& handle);

        effect_descriptor_t& desc() { return mDescriptor; }
        wp<EffectChain>&     chain() { return mChain; }

        status_t         setDevice(uint32_t device);
        status_t         setVolume(uint32_t *left, uint32_t *right, bool controller);
        status_t         setMode(audio_mode_t mode);
        status_t         start();
        status_t         stop();
        void             setSuspended(bool suspended);
        bool             suspended() const;

        sp<EffectHandle> controlHandle();

        bool             isPinned() const { return mPinned; }
        void             unPin() { mPinned = false; }

        status_t         dump(int fd, const Vector<String16>& args);

    protected:
        friend class AudioFlinger;      // for mHandles
        bool                mPinned;

        // Maximum time allocated to effect engines to complete the turn off sequence
        static const uint32_t MAX_DISABLE_TIME_MS = 10000;

        EffectModule(const EffectModule&);
        EffectModule& operator = (const EffectModule&);

        status_t start_l();
        status_t stop_l();

mutable Mutex               mLock;      // mutex for process, commands and handles list protection
        wp<ThreadBase>      mThread;    // parent thread
        wp<EffectChain>     mChain;     // parent effect chain
        int                 mId;        // this instance unique ID
        int                 mSessionId; // audio session ID
        effect_descriptor_t mDescriptor;// effect descriptor received from effect engine
        effect_config_t     mConfig;    // input and output audio configuration
        effect_handle_t  mEffectInterface; // Effect module C API
        status_t            mStatus;    // initialization status
        effect_state        mState;     // current activation state
        Vector< wp<EffectHandle> > mHandles;    // list of client handles
                    // First handle in mHandles has highest priority and controls the effect module
        uint32_t mMaxDisableWaitCnt;    // maximum grace period before forcing an effect off after
                                        // sending disable command.
        uint32_t mDisableWaitCnt;       // current process() calls count during disable period.
        bool     mSuspended;            // effect is suspended: temporarily disabled by framework
    };

    // The EffectHandle class implements the IEffect interface. It provides resources
    // to receive parameter updates, keeps track of effect control
    // ownership and state and has a pointer to the EffectModule object it is controlling.
    // There is one EffectHandle object for each application controlling (or using)
    // an effect module.
    // The EffectHandle is obtained by calling AudioFlinger::createEffect().
    class EffectHandle: public android::BnEffect {
    public:

        EffectHandle(const sp<EffectModule>& effect,
                const sp<AudioFlinger::Client>& client,
                const sp<IEffectClient>& effectClient,
                int32_t priority);
        virtual ~EffectHandle();

        // IEffect
        virtual status_t enable();
        virtual status_t disable();
        virtual status_t command(uint32_t cmdCode,
                                 uint32_t cmdSize,
                                 void *pCmdData,
                                 uint32_t *replySize,
                                 void *pReplyData);
        virtual void disconnect();
    private:
                void disconnect(bool unpinIfLast);
    public:
        virtual sp<IMemory> getCblk() const { return mCblkMemory; }
        virtual status_t onTransact(uint32_t code, const Parcel& data,
                Parcel* reply, uint32_t flags);


        // Give or take control of effect module
        // - hasControl: true if control is given, false if removed
        // - signal: true client app should be signaled of change, false otherwise
        // - enabled: state of the effect when control is passed
        void setControl(bool hasControl, bool signal, bool enabled);
        void commandExecuted(uint32_t cmdCode,
                             uint32_t cmdSize,
                             void *pCmdData,
                             uint32_t replySize,
                             void *pReplyData);
        void setEnabled(bool enabled);
        bool enabled() const { return mEnabled; }

        // Getters
        int id() const { return mEffect->id(); }
        int priority() const { return mPriority; }
        bool hasControl() const { return mHasControl; }
        sp<EffectModule> effect() const { return mEffect; }

        void dump(char* buffer, size_t size);

    protected:
        friend class AudioFlinger;          // for mEffect, mHasControl, mEnabled
        EffectHandle(const EffectHandle&);
        EffectHandle& operator =(const EffectHandle&);

        sp<EffectModule> mEffect;           // pointer to controlled EffectModule
        sp<IEffectClient> mEffectClient;    // callback interface for client notifications
        /*const*/ sp<Client> mClient;       // client for shared memory allocation, see disconnect()
        sp<IMemory>         mCblkMemory;    // shared memory for control block
        effect_param_cblk_t* mCblk;         // control block for deferred parameter setting via shared memory
        uint8_t*            mBuffer;        // pointer to parameter area in shared memory
        int mPriority;                      // client application priority to control the effect
        bool mHasControl;                   // true if this handle is controlling the effect
        bool mEnabled;                      // cached enable state: needed when the effect is
                                            // restored after being suspended
    };

    // the EffectChain class represents a group of effects associated to one audio session.
    // There can be any number of EffectChain objects per output mixer thread (PlaybackThread).
    // The EffecChain with session ID 0 contains global effects applied to the output mix.
    // Effects in this chain can be insert or auxiliary. Effects in other chains (attached to tracks)
    // are insert only. The EffectChain maintains an ordered list of effect module, the order corresponding
    // in the effect process order. When attached to a track (session ID != 0), it also provide it's own
    // input buffer used by the track as accumulation buffer.
    class EffectChain: public RefBase {
    public:
        EffectChain(const wp<ThreadBase>& wThread, int sessionId);
        EffectChain(ThreadBase *thread, int sessionId);
        virtual ~EffectChain();

        // special key used for an entry in mSuspendedEffects keyed vector
        // corresponding to a suspend all request.
        static const int        kKeyForSuspendAll = 0;

        // minimum duration during which we force calling effect process when last track on
        // a session is stopped or removed to allow effect tail to be rendered
        static const int        kProcessTailDurationMs = 1000;

        void process_l();

        void lock() {
            mLock.lock();
        }
        void unlock() {
            mLock.unlock();
        }

        status_t addEffect_l(const sp<EffectModule>& handle);
        size_t removeEffect_l(const sp<EffectModule>& handle);

        int sessionId() const { return mSessionId; }
        void setSessionId(int sessionId) { mSessionId = sessionId; }

        sp<EffectModule> getEffectFromDesc_l(effect_descriptor_t *descriptor);
        sp<EffectModule> getEffectFromId_l(int id);
        sp<EffectModule> getEffectFromType_l(const effect_uuid_t *type);
        bool setVolume_l(uint32_t *left, uint32_t *right);
        void setDevice_l(uint32_t device);
        void setMode_l(audio_mode_t mode);

        void setInBuffer(int16_t *buffer, bool ownsBuffer = false) {
            mInBuffer = buffer;
            mOwnInBuffer = ownsBuffer;
        }
        int16_t *inBuffer() const {
            return mInBuffer;
        }
        void setOutBuffer(int16_t *buffer) {
            mOutBuffer = buffer;
        }
        int16_t *outBuffer() const {
            return mOutBuffer;
        }

        void incTrackCnt() { android_atomic_inc(&mTrackCnt); }
        void decTrackCnt() { android_atomic_dec(&mTrackCnt); }
        int32_t trackCnt() const { return mTrackCnt;}

        void incActiveTrackCnt() { android_atomic_inc(&mActiveTrackCnt);
                                   mTailBufferCount = mMaxTailBuffers; }
        void decActiveTrackCnt() { android_atomic_dec(&mActiveTrackCnt); }
        int32_t activeTrackCnt() const { return mActiveTrackCnt;}

        uint32_t strategy() const { return mStrategy; }
        void setStrategy(uint32_t strategy)
                { mStrategy = strategy; }

        // suspend effect of the given type
        void setEffectSuspended_l(const effect_uuid_t *type,
                                  bool suspend);
        // suspend all eligible effects
        void setEffectSuspendedAll_l(bool suspend);
        // check if effects should be suspend or restored when a given effect is enable or disabled
        void checkSuspendOnEffectEnabled(const sp<EffectModule>& effect,
                                              bool enabled);

        void clearInputBuffer();

        status_t dump(int fd, const Vector<String16>& args);

    protected:
        friend class AudioFlinger;  // for mThread, mEffects
        EffectChain(const EffectChain&);
        EffectChain& operator =(const EffectChain&);

        class SuspendedEffectDesc : public RefBase {
        public:
            SuspendedEffectDesc() : mRefCount(0) {}

            int mRefCount;
            effect_uuid_t mType;
            wp<EffectModule> mEffect;
        };

        // get a list of effect modules to suspend when an effect of the type
        // passed is enabled.
        void                       getSuspendEligibleEffects(Vector< sp<EffectModule> > &effects);

        // get an effect module if it is currently enable
        sp<EffectModule> getEffectIfEnabled(const effect_uuid_t *type);
        // true if the effect whose descriptor is passed can be suspended
        // OEMs can modify the rules implemented in this method to exclude specific effect
        // types or implementations from the suspend/restore mechanism.
        bool isEffectEligibleForSuspend(const effect_descriptor_t& desc);

        void clearInputBuffer_l(sp<ThreadBase> thread);

        wp<ThreadBase> mThread;     // parent mixer thread
        Mutex mLock;                // mutex protecting effect list
        Vector< sp<EffectModule> > mEffects; // list of effect modules
        int mSessionId;             // audio session ID
        int16_t *mInBuffer;         // chain input buffer
        int16_t *mOutBuffer;        // chain output buffer
        volatile int32_t mActiveTrackCnt;  // number of active tracks connected
        volatile int32_t mTrackCnt;        // number of tracks connected
        int32_t mTailBufferCount;   // current effect tail buffer count
        int32_t mMaxTailBuffers;    // maximum effect tail buffers
        bool mOwnInBuffer;          // true if the chain owns its input buffer
        int mVolumeCtrlIdx;         // index of insert effect having control over volume
        uint32_t mLeftVolume;       // previous volume on left channel
        uint32_t mRightVolume;      // previous volume on right channel
        uint32_t mNewLeftVolume;       // new volume on left channel
        uint32_t mNewRightVolume;      // new volume on right channel
        uint32_t mStrategy; // strategy for this effect chain
        // mSuspendedEffects lists all effects currently suspended in the chain.
        // Use effect type UUID timelow field as key. There is no real risk of identical
        // timeLow fields among effect type UUIDs.
        // Updated by updateSuspendedSessions_l() only.
        KeyedVector< int, sp<SuspendedEffectDesc> > mSuspendedEffects;
    };

    // AudioStreamOut and AudioStreamIn are immutable, so their fields are const.
    // For emphasis, we could also make all pointers to them be "const *",
    // but that would clutter the code unnecessarily.

    struct AudioStreamOut {
        audio_hw_device_t*  const hwDev;
        audio_stream_out_t* const stream;

        AudioStreamOut(audio_hw_device_t *dev, audio_stream_out_t *out) :
            hwDev(dev), stream(out) {}
    };

    struct AudioStreamIn {
        audio_hw_device_t* const hwDev;
        audio_stream_in_t* const stream;

        AudioStreamIn(audio_hw_device_t *dev, audio_stream_in_t *in) :
            hwDev(dev), stream(in) {}
    };

    // for mAudioSessionRefs only
    struct AudioSessionRef {
        AudioSessionRef(int sessionid, pid_t pid) :
            mSessionid(sessionid), mPid(pid), mCnt(1) {}
        const int   mSessionid;
        const pid_t mPid;
        int         mCnt;
    };

    enum master_volume_support {
        // MVS_NONE:
        // Audio HAL has no support for master volume, either setting or
        // getting.  All master volume control must be implemented in SW by the
        // AudioFlinger mixing core.
        MVS_NONE,

        // MVS_SETONLY:
        // Audio HAL has support for setting master volume, but not for getting
        // master volume (original HAL design did not include a getter).
        // AudioFlinger needs to keep track of the last set master volume in
        // addition to needing to set an initial, default, master volume at HAL
        // load time.
        MVS_SETONLY,

        // MVS_FULL:
        // Audio HAL has support both for setting and getting master volume.
        // AudioFlinger should send all set and get master volume requests
        // directly to the HAL.
        MVS_FULL,
    };

    class AudioHwDevice {
    public:
        AudioHwDevice(const char *moduleName, audio_hw_device_t *hwDevice) :
            mModuleName(strdup(moduleName)), mHwDevice(hwDevice){}
        ~AudioHwDevice() { free((void *)mModuleName); }

        const char *moduleName() const { return mModuleName; }
        audio_hw_device_t *hwDevice() const { return mHwDevice; }
    private:
        const char * const mModuleName;
        audio_hw_device_t * const mHwDevice;
    };

    mutable     Mutex                               mLock;

                DefaultKeyedVector< pid_t, wp<Client> >     mClients;   // see ~Client()

                mutable     Mutex                   mHardwareLock;
                // NOTE: If both mLock and mHardwareLock mutexes must be held,
                // always take mLock before mHardwareLock

                // These two fields are immutable after onFirstRef(), so no lock needed to access
                audio_hw_device_t*                  mPrimaryHardwareDev; // mAudioHwDevs[0] or NULL
                DefaultKeyedVector<audio_module_handle_t, AudioHwDevice*>  mAudioHwDevs;

    // for dump, indicates which hardware operation is currently in progress (but not stream ops)
    enum hardware_call_state {
        AUDIO_HW_IDLE = 0,              // no operation in progress
        AUDIO_HW_INIT,                  // init_check
        AUDIO_HW_OUTPUT_OPEN,           // open_output_stream
        AUDIO_HW_OUTPUT_CLOSE,          // unused
        AUDIO_HW_INPUT_OPEN,            // unused
        AUDIO_HW_INPUT_CLOSE,           // unused
        AUDIO_HW_STANDBY,               // unused
        AUDIO_HW_SET_MASTER_VOLUME,     // set_master_volume
        AUDIO_HW_GET_ROUTING,           // unused
        AUDIO_HW_SET_ROUTING,           // unused
        AUDIO_HW_GET_MODE,              // unused
        AUDIO_HW_SET_MODE,              // set_mode
        AUDIO_HW_GET_MIC_MUTE,          // get_mic_mute
        AUDIO_HW_SET_MIC_MUTE,          // set_mic_mute
        AUDIO_HW_SET_VOICE_VOLUME,      // set_voice_volume
        AUDIO_HW_SET_PARAMETER,         // set_parameters
        AUDIO_HW_GET_INPUT_BUFFER_SIZE, // get_input_buffer_size
        AUDIO_HW_GET_MASTER_VOLUME,     // get_master_volume
        AUDIO_HW_GET_PARAMETER,         // get_parameters
    };

    mutable     hardware_call_state                 mHardwareStatus;    // for dump only


                DefaultKeyedVector< audio_io_handle_t, sp<PlaybackThread> >  mPlaybackThreads;
                stream_type_t                       mStreamTypes[AUDIO_STREAM_CNT];

                // both are protected by mLock
                float                               mMasterVolume;
                float                               mMasterVolumeSW;
                master_volume_support               mMasterVolumeSupportLvl;
                bool                                mMasterMute;

                DefaultKeyedVector< audio_io_handle_t, sp<RecordThread> >    mRecordThreads;

                DefaultKeyedVector< pid_t, sp<NotificationClient> >    mNotificationClients;
                volatile int32_t                    mNextUniqueId;  // updated by android_atomic_inc
                audio_mode_t                        mMode;
                bool                                mBtNrecIsOff;

                // protected by mLock
                Vector<AudioSessionRef*> mAudioSessionRefs;

                float       masterVolume_l() const;
                float       masterVolumeSW_l() const  { return mMasterVolumeSW; }
                bool        masterMute_l() const    { return mMasterMute; }
                audio_module_handle_t loadHwModule_l(const char *name);

                Vector < sp<SyncEvent> > mPendingSyncEvents; // sync events awaiting for a session
                                                             // to be created

private:
    sp<Client>  registerPid_l(pid_t pid);    // always returns non-0

};


// ----------------------------------------------------------------------------

}; // namespace android

#endif // ANDROID_AUDIO_FLINGER_H
