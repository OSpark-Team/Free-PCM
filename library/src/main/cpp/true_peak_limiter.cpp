#include "true_peak_limiter.h"

#include <algorithm>
#include <cmath>

TruePeakLimiter::TruePeakLimiter()
    : ready_(false), enabled_(true), sampleRate_(0), channelCount_(0), ceilingDbtp_(-1.0f), lookaheadMs_(5.0f),
      attackMs_(1.0f), releaseMs_(80.0f), ceilingLin_(DbToLin(-1.0f)), attackCoef_(0.0f), releaseCoef_(0.0f),
      currentGain_(1.0f), lookaheadFrames_(0), lastGainDb_(0.0f), lastGrDb_(0.0f)
{
}

bool TruePeakLimiter::IsReady() const
{
    return ready_ && (channelCount_ >= 1) && (channelCount_ <= 8) && sampleRate_ > 0 && lookaheadFrames_ > 0;
}

void TruePeakLimiter::Reset()
{
    currentGain_ = 1.0f;
    lastGainDb_ = 0.0f;
    lastGrDb_ = 0.0f;
    std::fill(history_.begin(), history_.end(), 0.0f);
}

void TruePeakLimiter::Init(int32_t sampleRate, int32_t channelCount)
{
    sampleRate_ = sampleRate;
    channelCount_ = channelCount;
    ready_ = (sampleRate_ > 0) && (channelCount_ >= 1) && (channelCount_ <= 8);
    SetParams(ceilingDbtp_, lookaheadMs_, attackMs_, releaseMs_);
}

void TruePeakLimiter::SetEnabled(bool enabled)
{
    enabled_ = enabled;
}

void TruePeakLimiter::SetParams(float ceilingDbtp, float lookaheadMs, float attackMs, float releaseMs)
{
    ceilingDbtp_ = ClampFloat(ceilingDbtp, -20.0f, 0.0f);
    lookaheadMs_ = ClampFloat(lookaheadMs, 0.0f, 50.0f);
    attackMs_ = ClampFloat(attackMs, 0.1f, 50.0f);
    releaseMs_ = ClampFloat(releaseMs, 5.0f, 2000.0f);

    ceilingLin_ = DbToLin(ceilingDbtp_);
    attackCoef_ = TimeMsToCoef(attackMs_, static_cast<float>(sampleRate_));
    releaseCoef_ = TimeMsToCoef(releaseMs_, static_cast<float>(sampleRate_));

    if (sampleRate_ > 0) {
        const float framesF = (lookaheadMs_ / 1000.0f) * static_cast<float>(sampleRate_);
        lookaheadFrames_ = static_cast<size_t>(std::lround(framesF));
        if (lookaheadFrames_ < 1) lookaheadFrames_ = 1;
    } else {
        lookaheadFrames_ = 0;
    }

    if (ready_ && lookaheadFrames_ > 0) {
        history_.assign(lookaheadFrames_ * static_cast<size_t>(channelCount_), 0.0f);
        currentGain_ = 1.0f;
    } else {
        history_.clear();
    }
}

float TruePeakLimiter::ClampFloat(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

float TruePeakLimiter::DbToLin(float db)
{
    return std::pow(10.0f, db / 20.0f);
}

float TruePeakLimiter::LinToDb(float lin)
{
    const float x = (lin < 1e-12f) ? 1e-12f : lin;
    return 20.0f * std::log10(x);
}

float TruePeakLimiter::TimeMsToCoef(float timeMs, float sampleRate)
{
    const float t = (timeMs <= 0.0f) ? 0.0f : (timeMs / 1000.0f);
    if (t <= 0.0f || sampleRate <= 0.0f) return 0.0f;
    return std::exp(-1.0f / (t * sampleRate));
}

float TruePeakLimiter::CatmullRom(float p0, float p1, float p2, float p3, float t)
{
    const float t2 = t * t;
    const float t3 = t2 * t;
    return 0.5f * ((2.0f * p1) + (-p0 + p2) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
                   (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
}

float TruePeakLimiter::SegmentTruePeak4x(float p0, float p1, float p2, float p3) const
{
    float peak = std::max(std::fabs(p1), std::fabs(p2));
    const float y1 = CatmullRom(p0, p1, p2, p3, 0.25f);
    const float y2 = CatmullRom(p0, p1, p2, p3, 0.50f);
    const float y3 = CatmullRom(p0, p1, p2, p3, 0.75f);
    peak = std::max(peak, std::fabs(y1));
    peak = std::max(peak, std::fabs(y2));
    peak = std::max(peak, std::fabs(y3));
    return peak;
}

void TruePeakLimiter::ProcessFloat(float* samples, size_t frameCount)
{
    if (!enabled_ || !IsReady() || samples == nullptr || frameCount == 0) return;

    const size_t ch = static_cast<size_t>(channelCount_);
    const size_t la = lookaheadFrames_;
    const size_t histFrames = la;

    if (history_.size() != histFrames * ch) {
        history_.assign(histFrames * ch, 0.0f);
    }

    const size_t totalFrames = histFrames + frameCount;
    const size_t totalSamples = totalFrames * ch;
    if (workBuf_.size() < totalSamples) {
        workBuf_.resize(totalSamples);
    }
    float* buf = workBuf_.data();
    std::copy(history_.begin(), history_.end(), buf);
    std::copy(samples, samples + frameCount * ch, buf + history_.size());

    const size_t gainCount = totalFrames;
    if (workGReq_.size() < gainCount) {
        workGReq_.resize(gainCount);
    }
    if (workGTgt_.size() < gainCount) {
        workGTgt_.resize(gainCount);
    }
    float* gReq = workGReq_.data();
    float* gTgt = workGTgt_.data();
    std::fill(gReq, gReq + gainCount, 1.0f);
    std::fill(gTgt, gTgt + gainCount, 1.0f);

    for (size_t i = 0; i + 1 < totalFrames; i++) {
        const size_t i0 = (i == 0) ? 0 : (i - 1);
        const size_t i1 = i;
        const size_t i2 = i + 1;
        const size_t i3 = (i + 2 < totalFrames) ? (i + 2) : (i + 1);

        float peak = 0.0f;
        for (size_t c = 0; c < ch; c++) {
            const float p0 = buf[i0 * ch + c];
            const float p1 = buf[i1 * ch + c];
            const float p2 = buf[i2 * ch + c];
            const float p3 = buf[i3 * ch + c];
            peak = std::max(peak, SegmentTruePeak4x(p0, p1, p2, p3));
        }

        float g = 1.0f;
        if (peak > ceilingLin_) {
            g = ceilingLin_ / peak;
        }
        if (g < 0.0f) g = 0.0f;
        if (g > 1.0f) g = 1.0f;
        gReq[i] = g;
    }
    gReq[totalFrames - 1] = 1.0f;

    for (size_t i = 0; i < totalFrames; i++) {
        const size_t j = (i + la < totalFrames) ? (i + la) : (totalFrames - 1);
        gTgt[i] = gReq[j];
    }

    float gain = currentGain_;
    float lastApplied = 1.0f;
    for (size_t i = 0; i < totalFrames; i++) {
        const float target = gTgt[i];
        const float coef = (target < gain) ? attackCoef_ : releaseCoef_;
        gain = (coef * gain) + ((1.0f - coef) * target);
        if (gain > 1.0f) gain = 1.0f;
        if (gain < 0.0f) gain = 0.0f;

        const bool inOutput = (i < frameCount);
        if (inOutput) {
            const size_t outIdx = i * ch;
            for (size_t c = 0; c < ch; c++) {
                const float x = buf[outIdx + c];
                samples[outIdx + c] = x * gain;
            }
            lastApplied = gain;
        }
    }
    currentGain_ = gain;

    const size_t histStart = frameCount * ch;
    std::copy(buf + histStart, buf + histStart + history_.size(), history_.begin());

    lastGainDb_ = LinToDb(lastApplied);
    const float gr = -lastGainDb_;
    lastGrDb_ = (gr > 0.0f) ? gr : 0.0f;
}
