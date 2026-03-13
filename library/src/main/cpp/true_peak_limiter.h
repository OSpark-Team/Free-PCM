#ifndef TRUE_PEAK_LIMITER_H
#define TRUE_PEAK_LIMITER_H

#include <stddef.h>
#include <stdint.h>
#include <vector>

class TruePeakLimiter {
public:
    TruePeakLimiter();

    void Reset();
    void Init(int32_t sampleRate, int32_t channelCount);
    void SetEnabled(bool enabled);
    bool IsReady() const;

    void SetParams(float ceilingDbtp, float lookaheadMs, float attackMs, float releaseMs);
    void ProcessFloat(float* samples, size_t frameCount);

    float GetLastGainDb() const { return lastGainDb_; }
    float GetLastGrDb() const { return lastGrDb_; }

private:
    static float DbToLin(float db);
    static float LinToDb(float lin);
    static float TimeMsToCoef(float timeMs, float sampleRate);
    static float ClampFloat(float v, float lo, float hi);
    static float CatmullRom(float p0, float p1, float p2, float p3, float t);

    float SegmentTruePeak4x(float p0, float p1, float p2, float p3) const;

    bool ready_;
    bool enabled_;
    int32_t sampleRate_;
    int32_t channelCount_;

    float ceilingDbtp_;
    float lookaheadMs_;
    float attackMs_;
    float releaseMs_;

    float ceilingLin_;
    float attackCoef_;
    float releaseCoef_;
    float currentGain_;

    size_t lookaheadFrames_;
    std::vector<float> history_;   // interleaved, size = lookaheadFrames_ * ch

    std::vector<float> workBuf_;
    std::vector<float> workGReq_;
    std::vector<float> workGTgt_;
    float lastGainDb_;
    float lastGrDb_;
};

#endif
