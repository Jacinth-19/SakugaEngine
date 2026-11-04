#pragma once
// Actual wall-clock frame-interval statistics. No GPU or rendering dependency.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace sk {
struct BenchmarkResult {
    int frames = 0;
    double seconds = 0, averageFps = 0, p95Ms = 0, onePercentLowFps = 0;
    double overBudgetPercent = 0;
};
class FrameBenchmark {
public:
    enum class State { Idle, Warmup, Measuring, Complete, Cancelled };
    void start(double warmupSeconds = 3.0, double sampleSeconds = 20.0) {
        warmupTarget_ = std::max(0.0, warmupSeconds); sampleTarget_ = std::max(0.01, sampleSeconds);
        warmupElapsed_ = sampleElapsed_ = 0; intervals_.clear(); result_ = {};
        state_ = warmupTarget_ > 0 ? State::Warmup : State::Measuring;
    }
    void cancel() { if (running()) { state_ = State::Cancelled; intervals_.clear(); } }
    bool running() const { return state_ == State::Warmup || state_ == State::Measuring; }
    State state() const { return state_; }
    double elapsed() const { return state_ == State::Warmup ? warmupElapsed_ : sampleElapsed_; }
    double target() const { return state_ == State::Warmup ? warmupTarget_ : sampleTarget_; }
    const BenchmarkResult& result() const { return result_; }
    void record(double seconds) {
        if (!running() || !std::isfinite(seconds) || seconds <= 0) return;
        if (state_ == State::Warmup) {
            warmupElapsed_ += seconds;
            if (warmupElapsed_ >= warmupTarget_) state_ = State::Measuring;
            return; // never count a partial warmup frame
        }
        intervals_.push_back(seconds); sampleElapsed_ += seconds;
        if (sampleElapsed_ < sampleTarget_) return;
        std::sort(intervals_.begin(), intervals_.end());
        const size_t n = intervals_.size();
        result_.frames = (int)n; result_.seconds = sampleElapsed_;
        result_.averageFps = double(n) / sampleElapsed_;
        // Nearest-rank p95 and reciprocal of the mean of the slowest 1% intervals.
        size_t p95 = std::min(n - 1, size_t(std::ceil(n * .95)) - 1);
        result_.p95Ms = intervals_[p95] * 1000.0;
        size_t slowCount = std::max<size_t>(1, size_t(std::ceil(n * .01)));
        double slowTime = 0; for (size_t i = n - slowCount; i < n; ++i) slowTime += intervals_[i];
        result_.onePercentLowFps = slowCount / slowTime;
        auto over = std::upper_bound(intervals_.begin(), intervals_.end(), 1.0 / 60.0);
        result_.overBudgetPercent = 100.0 * (intervals_.end() - over) / n;
        state_ = State::Complete;
    }
private:
    State state_ = State::Idle;
    double warmupTarget_ = 3, sampleTarget_ = 20, warmupElapsed_ = 0, sampleElapsed_ = 0;
    std::vector<double> intervals_;
    BenchmarkResult result_;
};
}
