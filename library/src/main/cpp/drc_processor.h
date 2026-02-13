#ifndef DRC_PROCESSOR_H
#define DRC_PROCESSOR_H

#include <cstddef>
#include <cstdint>

// A lightweight, streaming-friendly dynamic range compressor (DRC).
//
// - Works on interleaved mono/stereo PCM.
// - Stereo mode is linked: one gain computed from max(L,R) and applied to both.
// - Intended to run in the decode worker thread before pushing to ring buffer.
class DrcProcessor {
public:
  DrcProcessor();

  void Reset();
  void Init(int32_t sampleRate, int32_t channelCount);

  // All units are in the common audio engineering conventions.
  void SetEnabled(bool enabled);
  void SetParams(float thresholdDb, float ratio, float attackMs, float releaseMs, float makeupGainDb);

  bool IsReady() const;
  bool IsEnabled() const;

  void Process(int16_t* samples, size_t frameCount);
  void Process(int32_t* samples, size_t frameCount);

  // Float32 path (normalized roughly to [-1, 1]).
  void ProcessFloat(float* samples, size_t frameCount);

  // Meter (best-effort, updated during Process calls).
  // levelDb: input peak level in dBFS (<= 0)
  // gainDb: total applied gain in dB (includes makeup)
  // grDb: gain reduction in dB (>= 0, excludes makeup)
  float GetLastLevelDb() const { return lastLevelDb_; }
  float GetLastGainDb() const { return lastGainDb_; }
  float GetLastGrDb() const { return lastGrDb_; }

private:
  float ComputeTargetGain(float level) const;
  float SmoothGain(float targetGain);

  bool ready_;
  bool enabled_;
  int32_t sampleRate_;
  int32_t channelCount_;

  // parameters
  float thresholdDb_;
  float ratio_;
  float attackMs_;
  float releaseMs_;
  float makeupGainDb_;

  // runtime state
  float currentGain_;
  float attackCoef_;
  float releaseCoef_;

  // meters
  float lastLevelDb_;
  float lastGainDb_;
  float lastGrDb_;
};

#endif // DRC_PROCESSOR_H
