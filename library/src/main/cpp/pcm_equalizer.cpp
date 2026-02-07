#include "pcm_equalizer.h"

#include <cmath>

namespace {

static constexpr float kPi = 3.14159265358979323846f;

}

PcmEqualizer::PcmEqualizer()
    : ready_(false), enabled_(false), sampleRate_(0), channelCount_(0)
{
    freqsHz_ = {31.0f, 62.0f, 125.0f, 250.0f, 500.0f, 1000.0f, 2000.0f, 4000.0f, 8000.0f, 16000.0f};
    gainsDb_.fill(0.0f);
    Reset();
}

void PcmEqualizer::Reset()
{
    for (size_t b = 0; b < kBandCount; b++) {
        stateMono_[b] = {0, 0, 0, 0};
        for (size_t c = 0; c < 2; c++) {
            stateStereo_[b][c] = {0, 0, 0, 0};
        }
        biquads_[b] = {1, 0, 0, 0, 0};
    }
}

void PcmEqualizer::Init(int32_t sampleRate, int32_t channelCount)
{
    sampleRate_ = sampleRate;
    channelCount_ = channelCount;
    ready_ = (sampleRate_ > 0) && (channelCount_ == 1 || channelCount_ == 2);
    Reset();
    if (ready_) {
        RecalcBiquads();
    }
}

void PcmEqualizer::SetGainsDb(const std::array<float, kBandCount>& gainsDb)
{
    gainsDb_ = gainsDb;
    if (ready_) {
        RecalcBiquads();
    }
}

void PcmEqualizer::SetEnabled(bool enabled)
{
    enabled_ = enabled;
}

bool PcmEqualizer::IsReady() const
{
    return ready_;
}

bool PcmEqualizer::IsEnabled() const
{
    return enabled_;
}

float PcmEqualizer::ClampFloat(float v, float lo, float hi)
{
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

int16_t PcmEqualizer::ClampS16(float v)
{
    if (v > 32767.0f) {
        return 32767;
    }
    if (v < -32768.0f) {
        return -32768;
    }
    return static_cast<int16_t>(v);
}

int32_t PcmEqualizer::ClampS32(float v)
{
    if (v > 2147483520.0f) {  // slightly less than INT32_MAX to avoid overflow
        return 2147483647;
    }
    if (v < -2147483648.0f) {
        return -2147483648;
    }
    return static_cast<int32_t>(v);
}

PcmEqualizer::Biquad PcmEqualizer::MakePeaking(float sampleRate, float freqHz, float q, float gainDb)
{
    if (sampleRate <= 0.0f) {
        return {1, 0, 0, 0, 0};
    }

    // Keep freq in (0, Nyquist)
    const float nyquist = sampleRate * 0.5f;
    const float f = ClampFloat(freqHz, 1.0f, nyquist - 1.0f);

    const float A = std::pow(10.0f, gainDb / 40.0f);
    const float w0 = 2.0f * kPi * (f / sampleRate);
    const float cosw0 = std::cos(w0);
    const float sinw0 = std::sin(w0);
    const float alpha = sinw0 / (2.0f * q);

    float b0 = 1.0f + alpha * A;
    float b1 = -2.0f * cosw0;
    float b2 = 1.0f - alpha * A;
    float a0 = 1.0f + alpha / A;
    float a1 = -2.0f * cosw0;
    float a2 = 1.0f - alpha / A;

    // normalize
    b0 /= a0;
    b1 /= a0;
    b2 /= a0;
    a1 /= a0;
    a2 /= a0;

    return {b0, b1, b2, a1, a2};
}

void PcmEqualizer::RecalcBiquads()
{
    // Q is a tradeoff; 1.0 is a reasonable graphic EQ approximation.
    const float q = 1.0f;
    for (size_t b = 0; b < kBandCount; b++) {
        biquads_[b] = MakePeaking(static_cast<float>(sampleRate_), freqsHz_[b], q, gainsDb_[b]);
    }
}

void PcmEqualizer::Process(int16_t* samples, size_t frameCount)
{
    if (!ready_ || !enabled_ || samples == nullptr || frameCount == 0) {
        return;
    }

    if (channelCount_ == 1) {
        for (size_t i = 0; i < frameCount; i++) {
            float x = static_cast<float>(samples[i]);
            for (size_t b = 0; b < kBandCount; b++) {
                const Biquad& q = biquads_[b];
                State& s = stateMono_[b];
                const float y = (q.b0 * x) + (q.b1 * s.x1) + (q.b2 * s.x2) - (q.a1 * s.y1) - (q.a2 * s.y2);
                s.x2 = s.x1;
                s.x1 = x;
                s.y2 = s.y1;
                s.y1 = y;
                x = y;
            }
            samples[i] = ClampS16(x);
        }
        return;
    }

    // stereo interleaved
    for (size_t i = 0; i < frameCount; i++) {
        float xl = static_cast<float>(samples[i * 2]);
        float xr = static_cast<float>(samples[i * 2 + 1]);

        for (size_t b = 0; b < kBandCount; b++) {
            const Biquad& q = biquads_[b];
            State& sl = stateStereo_[b][0];
            State& sr = stateStereo_[b][1];

            const float yl = (q.b0 * xl) + (q.b1 * sl.x1) + (q.b2 * sl.x2) - (q.a1 * sl.y1) - (q.a2 * sl.y2);
            sl.x2 = sl.x1;
            sl.x1 = xl;
            sl.y2 = sl.y1;
            sl.y1 = yl;
            xl = yl;

            const float yr = (q.b0 * xr) + (q.b1 * sr.x1) + (q.b2 * sr.x2) - (q.a1 * sr.y1) - (q.a2 * sr.y2);
            sr.x2 = sr.x1;
            sr.x1 = xr;
            sr.y2 = sr.y1;
            sr.y1 = yr;
            xr = yr;
        }

        samples[i * 2] = ClampS16(xl);
        samples[i * 2 + 1] = ClampS16(xr);
    }
}

void PcmEqualizer::Process(int32_t* samples, size_t frameCount)
{
    if (!ready_ || !enabled_ || samples == nullptr || frameCount == 0) {
        return;
    }

    const float kNorm = 1.0f / 2147483648.0f;  // 1 / 2^31

    if (channelCount_ == 1) {
        for (size_t i = 0; i < frameCount; i++) {
            float x = static_cast<float>(samples[i]) * kNorm;
            for (size_t b = 0; b < kBandCount; b++) {
                const Biquad& q = biquads_[b];
                State& s = stateMono_[b];
                const float y = (q.b0 * x) + (q.b1 * s.x1) + (q.b2 * s.x2) - (q.a1 * s.y1) - (q.a2 * s.y2);
                s.x2 = s.x1;
                s.x1 = x;
                s.y2 = s.y1;
                s.y1 = y;
                x = y;
            }
            samples[i] = ClampS32(x / kNorm);
        }
        return;
    }

    // stereo interleaved
    for (size_t i = 0; i < frameCount; i++) {
        float xl = static_cast<float>(samples[i * 2]) * kNorm;
        float xr = static_cast<float>(samples[i * 2 + 1]) * kNorm;

        for (size_t b = 0; b < kBandCount; b++) {
            const Biquad& q = biquads_[b];
            State& sl = stateStereo_[b][0];
            State& sr = stateStereo_[b][1];

            const float yl = (q.b0 * xl) + (q.b1 * sl.x1) + (q.b2 * sl.x2) - (q.a1 * sl.y1) - (q.a2 * sl.y2);
            sl.x2 = sl.x1;
            sl.x1 = xl;
            sl.y2 = sl.y1;
            sl.y1 = yl;
            xl = yl;

            const float yr = (q.b0 * xr) + (q.b1 * sr.x1) + (q.b2 * sr.x2) - (q.a1 * sr.y1) - (q.a2 * sr.y2);
            sr.x2 = sr.x1;
            sr.x1 = xr;
            sr.y2 = sr.y1;
            sr.y1 = yr;
            xr = yr;
        }

        samples[i * 2] = ClampS32(xl / kNorm);
        samples[i * 2 + 1] = ClampS32(xr / kNorm);
    }
}
