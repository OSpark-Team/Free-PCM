#include "pcm_pitch_shifter.h"
#include <algorithm>
#include <cmath>

// Static member definitions
constexpr int PcmPitchShifter::kMinSemitones;
constexpr int PcmPitchShifter::kMaxSemitones;
constexpr size_t PcmPitchShifter::kMaxChannels;

PcmPitchShifter::PcmPitchShifter()
    : ready_(false)
    , enabled_(false)
    , sampleRate_(0)
    , channelCount_(0)
    , halfSemitones_(0)
    , pitchRatio_(1.0f)
    , delayLineSize_(0)
    , writePos_(0)
    , readPos0_(0.0f)
    , readPos1_(0.0f)
    , fadePos_(0.0f)
    , fadeLen_(0)
    , activeLine_(0)
{
}

PcmPitchShifter::~PcmPitchShifter() = default;

void PcmPitchShifter::Reset() {
    delayLine_.clear();
    writePos_ = 0;
    readPos0_ = 0.0f;
    readPos1_ = 0.0f;
    fadePos_ = 0.0f;
    activeLine_ = 0;
}

void PcmPitchShifter::Init(int32_t sampleRate, int32_t channelCount) {
    if (sampleRate <= 0 || channelCount <= 0) {
        ready_ = false;
        return;
    }

    sampleRate_ = sampleRate;
    channelCount_ = std::min(channelCount, static_cast<int32_t>(kMaxChannels));

    Reset();

    // Calculate delay line size - window size for pitch shifting
    delayLineSize_ = static_cast<size_t>(sampleRate_ * kDelayLineSizeSec);
    if (delayLineSize_ < 4096) delayLineSize_ = 4096;
    if (delayLineSize_ > 32768) delayLineSize_ = 32768;

    // Calculate crossfade length
    fadeLen_ = static_cast<size_t>(sampleRate_ * kFadeLengthSec);
    if (fadeLen_ < 512) fadeLen_ = 512;
    if (fadeLen_ > 4096) fadeLen_ = 4096;

    size_t channels = static_cast<size_t>(channelCount_);
    delayLine_.resize(delayLineSize_ * channels, 0.0f);

    // Initialize read positions
    // readPos0_ starts at 0, readPos1_ starts at half window
    readPos0_ = 0.0f;
    readPos1_ = static_cast<float>(delayLineSize_) * 0.5f;
    fadePos_ = 0.0f;
    activeLine_ = 0;

    ready_ = true;
}

void PcmPitchShifter::SetSemitones(int halfSemitones) {
    halfSemitones = std::max(kMinSemitones, std::min(kMaxSemitones, halfSemitones));

    if (halfSemitones_ != halfSemitones) {
        halfSemitones_ = halfSemitones;
        pitchRatio_ = SemitonesToRatio(halfSemitones_);
    }
}

void PcmPitchShifter::SetEnabled(bool enabled) {
    if (enabled_ != enabled) {
        enabled_ = enabled;
        if (!enabled) {
            Reset();
        }
    }
}

bool PcmPitchShifter::IsReady() const {
    return ready_;
}

bool PcmPitchShifter::IsEnabled() const {
    return enabled_;
}

int PcmPitchShifter::GetSemitones() const {
    return halfSemitones_;
}

// Convert half-semitones to pitch ratio
// Each half-semitone = 50 cents = 2^(1/24)
float PcmPitchShifter::SemitonesToRatio(int halfSemitones) {
    return std::pow(2.0f, static_cast<float>(halfSemitones) / 24.0f);
}

float PcmPitchShifter::readDelay(float pos, size_t channel) const {
    const size_t channels = static_cast<size_t>(channelCount_);

    // Wrap position
    while (pos < 0) pos += static_cast<float>(delayLineSize_);
    while (pos >= static_cast<float>(delayLineSize_)) {
        pos -= static_cast<float>(delayLineSize_);
    }

    // Linear interpolation
    size_t idx0 = static_cast<size_t>(pos);
    size_t idx1 = (idx0 + 1) % delayLineSize_;
    float frac = pos - static_cast<float>(idx0);

    float v0 = delayLine_[idx0 * channels + channel];
    float v1 = delayLine_[idx1 * channels + channel];

    return v0 + frac * (v1 - v0);
}

size_t PcmPitchShifter::ProcessFloat(float* samples, size_t frameCount) {
    if (!ready_ || !enabled_ || frameCount == 0 || halfSemitones_ == 0) {
        return frameCount;
    }

    const size_t channels = static_cast<size_t>(channelCount_);
    const float delayLineSizeF = static_cast<float>(delayLineSize_);
    const float fadeLenF = static_cast<float>(fadeLen_);

    // Target distance from write position to read position
    // This is the "safe zone" where we don't need to crossfade
    const float targetDistance = delayLineSizeF * 0.25f;
    const float minDistance = delayLineSizeF * 0.08f;  // For pitch up (read catches up to write)
    const float maxDistance = delayLineSizeF * 0.42f;  // For pitch down (read falls behind write)

    for (size_t frame = 0; frame < frameCount; frame++) {
        // Write input to delay line
        for (size_t ch = 0; ch < channels; ch++) {
            delayLine_[writePos_ * channels + ch] = samples[frame * channels + ch];
        }

        // Get active and inactive read positions
        float* activePos = (activeLine_ == 0) ? &readPos0_ : &readPos1_;
        float* inactivePos = (activeLine_ == 0) ? &readPos1_ : &readPos0_;

        // Read output with crossfade
        if (fadePos_ > 0.0f) {
            // During crossfade: blend between two read positions
            // fadeProgress: 1.0 at start (old position), 0.0 at end (new position)
            float fadeProgress = fadePos_ / fadeLenF;

            // Equal-power crossfade for smooth transition
            float gainOld = std::sin(static_cast<float>(M_PI) * fadeProgress / 2.0f);
            float gainNew = std::cos(static_cast<float>(M_PI) * fadeProgress / 2.0f);

            for (size_t ch = 0; ch < channels; ch++) {
                float sampleOld = readDelay(*inactivePos, ch);
                float sampleNew = readDelay(*activePos, ch);
                samples[frame * channels + ch] = sampleOld * gainOld + sampleNew * gainNew;
            }

            fadePos_ -= 1.0f;
        } else {
            // Normal reading from active line
            for (size_t ch = 0; ch < channels; ch++) {
                samples[frame * channels + ch] = readDelay(*activePos, ch);
            }
        }

        // Advance both read positions at pitch-shifted rate
        readPos0_ += pitchRatio_;
        readPos1_ += pitchRatio_;

        // Wrap read positions
        while (readPos0_ >= delayLineSizeF) {
            readPos0_ -= delayLineSizeF;
        }
        while (readPos1_ >= delayLineSizeF) {
            readPos1_ -= delayLineSizeF;
        }

        // Calculate distance from write position to active read position
        // Distance = how far behind the read position is from the write position
        float distance = static_cast<float>(writePos_) - *activePos;
        while (distance < 0) distance += delayLineSizeF;

        // Check if we need to crossfade to reset the read position
        // - Pitch UP (ratio > 1): read catches up to write, distance decreases
        // - Pitch DOWN (ratio < 1): read falls behind write, distance increases
        bool needCrossfade = false;
        if (distance < minDistance || distance > maxDistance) {
            needCrossfade = true;
        }

        if (needCrossfade && fadePos_ <= 0.0f) {
            // Set inactive position to target distance behind write position
            *inactivePos = static_cast<float>(writePos_) - targetDistance;
            while (*inactivePos < 0) *inactivePos += delayLineSizeF;

            // Switch active line and start crossfade
            activeLine_ = 1 - activeLine_;
            fadePos_ = fadeLenF;
        }

        // Advance write position
        writePos_ = (writePos_ + 1) % delayLineSize_;
    }

    return frameCount;
}
