#ifndef PCM_EQUALIZER_H
#define PCM_EQUALIZER_H

#include <array>
#include <cstddef>
#include <cstdint>

// 10-band graphic EQ for interleaved S16LE/S32LE PCM.
// Bands: 31, 62, 125, 250, 500, 1k, 2k, 4k, 8k, 16k.
// Implementation: RBJ peaking EQ biquads (Q ~ 1.0).
class PcmEqualizer {
public:
    static constexpr size_t kBandCount = 10;

    PcmEqualizer();

    void Reset();
    void Init(int32_t sampleRate, int32_t channelCount);
    void SetGainsDb(const std::array<float, kBandCount>& gainsDb);

    // Set independent gains for L/R channels (stereo only).
    // For mono, left gains are used.
    void SetGainsDbStereo(const std::array<float, kBandCount>& gainsLeftDb,
                          const std::array<float, kBandCount>& gainsRightDb);

    // Set gains for a specific channel.
    // channelIndex: 0=left/mono, 1=right.
    void SetGainsDbForChannel(int32_t channelIndex, const std::array<float, kBandCount>& gainsDb);
    void SetEnabled(bool enabled);

    bool IsReady() const;
    bool IsEnabled() const;

    // Process in-place (S16LE).
    // samples: interleaved int16 PCM.
    // frameCount: number of frames (a frame contains channelCount samples).
    void Process(int16_t* samples, size_t frameCount);

    // Process in-place (S32LE).
    // samples: interleaved int32 PCM.
    // frameCount: number of frames (a frame contains channelCount samples).
    void Process(int32_t* samples, size_t frameCount);

private:
    struct Biquad {
        float b0;
        float b1;
        float b2;
        float a1;
        float a2;
    };

    struct State {
        float x1;
        float x2;
        float y1;
        float y2;
    };

    static float ClampFloat(float v, float lo, float hi);
    static int16_t ClampS16(float v);
    static int32_t ClampS32(float v);
    static Biquad MakePeaking(float sampleRate, float freqHz, float q, float gainDb);

    void RecalcBiquads();

    bool ready_;
    bool enabled_;
    int32_t sampleRate_;
    int32_t channelCount_;

    // gainsDbStereo_[0]=left/mono, gainsDbStereo_[1]=right.
    std::array<std::array<float, kBandCount>, 2> gainsDbStereo_;
    std::array<float, kBandCount> freqsHz_;

    // biquadsByCh_[channel][band]
    std::array<std::array<Biquad, kBandCount>, 2> biquadsByCh_;

    // state_[band][channel]
    std::array<std::array<State, 2>, kBandCount> stateStereo_;
    std::array<State, kBandCount> stateMono_;
};

#endif // PCM_EQUALIZER_H
