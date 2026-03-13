#ifndef PCM_PITCH_SHIFTER_H
#define PCM_PITCH_SHIFTER_H

#include <cstddef>
#include <cstdint>
#include <vector>

static constexpr float kFadeLengthSec = 0.025f;

static constexpr float kDelayLineSizeSec = 0.12f;

class PcmPitchShifter {
public:
    static constexpr int kMinSemitones = -12;
    static constexpr int kMaxSemitones = 12;
    static constexpr size_t kMaxChannels = 8;

    PcmPitchShifter();
    ~PcmPitchShifter();

    void Reset();
    void Init(int32_t sampleRate, int32_t channelCount);
    void SetSemitones(int semitones);
    void SetEnabled(bool enabled);

    bool IsReady() const;
    bool IsEnabled() const;
    int GetSemitones() const;

    size_t ProcessFloat(float *samples, size_t frameCount);

private:
    static float SemitonesToRatio(int halfSemitones);
    float readDelay(float pos, size_t channel) const;

    bool ready_;
    bool enabled_;
    int32_t sampleRate_;
    int32_t channelCount_;
    int halfSemitones_;
    float pitchRatio_;

    std::vector<float> delayLine_;
    size_t delayLineSize_;
    size_t writePos_;
    float readPos0_;
    float readPos1_;

    float fadePos_;
    size_t fadeLen_;
    int activeLine_;
};

#endif
