#include "drc_processor.h"

#include <cmath>

namespace {

static inline float ClampFloat(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static inline int16_t ClampS16(float v)
{
    if (v > 32767.0f) return 32767;
    if (v < -32768.0f) return -32768;
    return static_cast<int16_t>(v);
}

static inline int32_t ClampS32(float v)
{
    if (v > 2147483520.0f) return 2147483647;
    if (v < -2147483648.0f) return -2147483648;
    return static_cast<int32_t>(v);
}

static inline float DbToLin(float db)
{
    return std::pow(10.0f, db / 20.0f);
}

static inline float LinToDb(float lin)
{
    const float x = (lin < 1e-12f) ? 1e-12f : lin;
    return 20.0f * std::log10(x);
}

static inline float TimeMsToCoef(float timeMs, float sampleRate)
{
    // One-pole smoothing coefficient per-sample.
    const float t = (timeMs <= 0.0f) ? 0.0f : (timeMs / 1000.0f);
    if (t <= 0.0f || sampleRate <= 0.0f) {
        return 0.0f;
    }
    return std::exp(-1.0f / (t * sampleRate));
}

}

DrcProcessor::DrcProcessor()
    : ready_(false), enabled_(false), sampleRate_(0), channelCount_(0), thresholdDb_(-20.0f), ratio_(4.0f),
      attackMs_(10.0f), releaseMs_(100.0f), makeupGainDb_(0.0f), currentGain_(1.0f), attackCoef_(0.0f),
      releaseCoef_(0.0f), lastLevelDb_(-120.0f), lastGainDb_(0.0f), lastGrDb_(0.0f)
{
}

void DrcProcessor::Reset()
{
    currentGain_ = 1.0f;
    lastLevelDb_ = -120.0f;
    lastGainDb_ = 0.0f;
    lastGrDb_ = 0.0f;
}

void DrcProcessor::Init(int32_t sampleRate, int32_t channelCount)
{
    sampleRate_ = sampleRate;
    channelCount_ = channelCount;
    ready_ = (sampleRate_ > 0) && (channelCount_ == 1 || channelCount_ == 2);
    attackCoef_ = TimeMsToCoef(attackMs_, static_cast<float>(sampleRate_));
    releaseCoef_ = TimeMsToCoef(releaseMs_, static_cast<float>(sampleRate_));
    Reset();
}

void DrcProcessor::SetEnabled(bool enabled)
{
    enabled_ = enabled;
}

void DrcProcessor::SetParams(float thresholdDb, float ratio, float attackMs, float releaseMs, float makeupGainDb)
{
    thresholdDb_ = ClampFloat(thresholdDb, -60.0f, 0.0f);
    ratio_ = ClampFloat(ratio, 1.0f, 20.0f);
    attackMs_ = ClampFloat(attackMs, 0.1f, 200.0f);
    releaseMs_ = ClampFloat(releaseMs, 5.0f, 2000.0f);
    makeupGainDb_ = ClampFloat(makeupGainDb, -12.0f, 24.0f);

    attackCoef_ = TimeMsToCoef(attackMs_, static_cast<float>(sampleRate_));
    releaseCoef_ = TimeMsToCoef(releaseMs_, static_cast<float>(sampleRate_));
}

bool DrcProcessor::IsReady() const
{
    return ready_;
}

bool DrcProcessor::IsEnabled() const
{
    return enabled_;
}

float DrcProcessor::ComputeTargetGain(float level) const
{
    // level is linear amplitude in [0..1].
    const float inDb = LinToDb(level);

    float gainDb = makeupGainDb_;
    if (inDb > thresholdDb_ && ratio_ > 1.0f) {
        const float over = inDb - thresholdDb_;
        const float outOver = over / ratio_;
        const float outDb = thresholdDb_ + outOver;
        gainDb += (outDb - inDb);
    }

    // Constrain gain to avoid extreme explosion.
    gainDb = ClampFloat(gainDb, -48.0f, 24.0f);
    return DbToLin(gainDb);
}

float DrcProcessor::SmoothGain(float targetGain)
{
    // When targetGain < currentGain, we need to reduce faster (attack).
    const float coef = (targetGain < currentGain_) ? attackCoef_ : releaseCoef_;
    currentGain_ = (coef * currentGain_) + ((1.0f - coef) * targetGain);
    return currentGain_;
}

void DrcProcessor::Process(int16_t* samples, size_t frameCount)
{
    if (!ready_ || !enabled_ || samples == nullptr || frameCount == 0) {
        return;
    }

    const float kNorm = 1.0f / 32768.0f;

    float maxLevel = 0.0f;

    if (channelCount_ == 1) {
        for (size_t i = 0; i < frameCount; i++) {
            const float x = std::fabs(static_cast<float>(samples[i]) * kNorm);
            if (x > maxLevel) {
                maxLevel = x;
            }
            const float target = ComputeTargetGain(x);
            const float g = SmoothGain(target);
            const float y = static_cast<float>(samples[i]) * g;
            samples[i] = ClampS16(y);
        }

        lastLevelDb_ = LinToDb(maxLevel);
        lastGainDb_ = LinToDb(currentGain_);
        const float gr = makeupGainDb_ - lastGainDb_;
        lastGrDb_ = (gr > 0.0f) ? gr : 0.0f;
        return;
    }

    // stereo interleaved (linked)
    for (size_t i = 0; i < frameCount; i++) {
        const float xl = std::fabs(static_cast<float>(samples[i * 2]) * kNorm);
        const float xr = std::fabs(static_cast<float>(samples[i * 2 + 1]) * kNorm);
        const float level = (xl > xr) ? xl : xr;
        if (level > maxLevel) {
            maxLevel = level;
        }
        const float target = ComputeTargetGain(level);
        const float g = SmoothGain(target);

        const float yl = static_cast<float>(samples[i * 2]) * g;
        const float yr = static_cast<float>(samples[i * 2 + 1]) * g;
        samples[i * 2] = ClampS16(yl);
        samples[i * 2 + 1] = ClampS16(yr);
    }

    lastLevelDb_ = LinToDb(maxLevel);
    lastGainDb_ = LinToDb(currentGain_);
    const float gr = makeupGainDb_ - lastGainDb_;
    lastGrDb_ = (gr > 0.0f) ? gr : 0.0f;
}

void DrcProcessor::Process(int32_t* samples, size_t frameCount)
{
    if (!ready_ || !enabled_ || samples == nullptr || frameCount == 0) {
        return;
    }

    // OpenHarmony decoders sometimes output S32LE samples that are effectively
    // in a 16-bit scale (i.e. values roughly in [-32768, 32767]) rather than
    // full Q31 scale. If we normalize by 2^31 in that case, the signal level
    // looks like ~-90dB to -140dB and compression never triggers.
    //
    // Heuristic: probe a small prefix of frames to decide normalization.
    int64_t maxAbs = 0;
    const size_t ch = static_cast<size_t>(channelCount_);
    const size_t probeFrames = (frameCount < 256) ? frameCount : 256;
    for (size_t i = 0; i < probeFrames * ch; i++) {
        int64_t v = static_cast<int64_t>(samples[i]);
        if (v < 0) {
            v = -v;
        }
        if (v > maxAbs) {
            maxAbs = v;
        }
    }

    const float kNorm = (maxAbs <= (1 << 20)) ? (1.0f / 32768.0f) : (1.0f / 2147483648.0f);

    float maxLevel = 0.0f;

    if (channelCount_ == 1) {
        for (size_t i = 0; i < frameCount; i++) {
            const float x = std::fabs(static_cast<float>(samples[i]) * kNorm);
            if (x > maxLevel) {
                maxLevel = x;
            }
            const float target = ComputeTargetGain(x);
            const float g = SmoothGain(target);
            const float y = static_cast<float>(samples[i]) * g;
            samples[i] = ClampS32(y);
        }

        lastLevelDb_ = LinToDb(maxLevel);
        lastGainDb_ = LinToDb(currentGain_);
        const float gr = makeupGainDb_ - lastGainDb_;
        lastGrDb_ = (gr > 0.0f) ? gr : 0.0f;
        return;
    }

    for (size_t i = 0; i < frameCount; i++) {
        const float xl = std::fabs(static_cast<float>(samples[i * 2]) * kNorm);
        const float xr = std::fabs(static_cast<float>(samples[i * 2 + 1]) * kNorm);
        const float level = (xl > xr) ? xl : xr;
        if (level > maxLevel) {
            maxLevel = level;
        }
        const float target = ComputeTargetGain(level);
        const float g = SmoothGain(target);

        const float yl = static_cast<float>(samples[i * 2]) * g;
        const float yr = static_cast<float>(samples[i * 2 + 1]) * g;
        samples[i * 2] = ClampS32(yl);
        samples[i * 2 + 1] = ClampS32(yr);
    }

    lastLevelDb_ = LinToDb(maxLevel);
    lastGainDb_ = LinToDb(currentGain_);
    const float gr = makeupGainDb_ - lastGainDb_;
    lastGrDb_ = (gr > 0.0f) ? gr : 0.0f;
}
