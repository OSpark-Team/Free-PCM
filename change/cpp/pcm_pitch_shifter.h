#ifndef PCM_PITCH_SHIFTER_H
#define PCM_PITCH_SHIFTER_H

#include <cstddef>
#include <cstdint>
#include <vector>

// ============================================================================
// TUNABLE PARAMETERS - Adjust these for different quality/performance tradeoffs
// ============================================================================

// Fade length in seconds for crossfade between two read positions
// Longer = smoother transitions, especially for low frequencies
// For good low frequency response, this should be at least 2-3 cycles of the lowest frequency
// At 50Hz (lowest bass), one cycle = 20ms, so 30ms covers ~1.5 cycles
// Recommended range: 0.020 - 0.040
static constexpr float kFadeLengthSec = 0.025f;

// Delay line size in seconds: determines the window size for pitch shifting
// Larger = better low frequency response, but more memory and latency
// Should be at least 3-4x the fade length for smooth operation
// Recommended range: 0.10 - 0.20
static constexpr float kDelayLineSizeSec = 0.12f;

// ============================================================================

/**
 * @brief Pitch shifter using dual read positions with smooth crossfade.
 *
 * This implementation uses a single delay line with two read positions.
 * The two read positions are offset by half the window size. When one
 * position drifts too far from the write position, it crossfades to
 * the other position. This provides smooth pitch shifting without
 * the "jump" artifacts of single-read-position algorithms.
 *
 * Each unit represents a half-semitone (50 cents).
 * Range: -12 to +12 (equivalent to -6 to +6 semitones)
 */
class PcmPitchShifter {
public:
    // Each unit = half semitone, range -12 to +12
    static constexpr int kMinSemitones = -12;
    static constexpr int kMaxSemitones = 12;
    static constexpr size_t kMaxChannels = 8;

    PcmPitchShifter();
    ~PcmPitchShifter();

    void Reset();
    void Init(int32_t sampleRate, int32_t channelCount);
    void SetSemitones(int semitones); // Input is in half-semitones
    void SetEnabled(bool enabled);

    bool IsReady() const;
    bool IsEnabled() const;
    int GetSemitones() const; // Returns half-semitones

    size_t ProcessFloat(float *samples, size_t frameCount);

private:
    static float SemitonesToRatio(int halfSemitones);
    float readDelay(float pos, size_t channel) const;

    bool ready_;
    bool enabled_;
    int32_t sampleRate_;
    int32_t channelCount_;
    int halfSemitones_; // Stored in half-semitones
    float pitchRatio_;

    // Single delay line with two read positions
    std::vector<float> delayLine_;
    size_t delayLineSize_;
    size_t writePos_;
    float readPos0_;  // First read position
    float readPos1_;  // Second read position (offset by half window)

    // Crossfade state
    float fadePos_;   // Remaining fade samples (0 = no fade in progress)
    size_t fadeLen_;  // Total fade length in samples
    int activeLine_;  // Which read position is currently active (0 or 1)
};

#endif // PCM_PITCH_SHIFTER_H
