#ifndef LIGHTHOST_AUDIO_PLAYER_H
#define LIGHTHOST_AUDIO_PLAYER_H

#include <atomic>
#include <cmath>

// Audio callbacks only update counters. All diagnostic file I/O runs on the message thread.
class AudioPlayer : public AudioProcessorPlayer, private Timer
{
public:
    explicit AudioPlayer(const File& logFile)
        : logger(logFile, "Light Host audio diagnostics", maxLogBytes)
    {
        startTimer(5000);
    }

    ~AudioPlayer() override
    {
        stopTimer();
        timerCallback();
    }

    void audioDeviceAboutToStart(AudioIODevice* device) override
    {
        AudioProcessorPlayer::audioDeviceAboutToStart(device);
        sampleRate.store(device->getCurrentSampleRate(), std::memory_order_relaxed);
        const ScopedLock eventGuard(eventLock);
        pendingEvent = "Stream started: " + device->getTypeName() + ", " + device->getName()
            + ", " + String(device->getCurrentSampleRate()) + " Hz, "
            + String(device->getCurrentBufferSizeSamples()) + " samples, "
            + String(device->getActiveInputChannels().countNumberOfSetBits()) + " inputs, "
            + String(device->getActiveOutputChannels().countNumberOfSetBits()) + " outputs";
    }

    void audioDeviceStopped() override
    {
        AudioProcessorPlayer::audioDeviceStopped();
        sampleRate.store(0.0, std::memory_order_relaxed);
    }

    void audioDeviceError(const String& message) override
    {
        const ScopedLock eventGuard(eventLock);
        pendingError = message;
    }

    void audioDeviceIOCallback(const float** inputs, int inputCount, float** outputs,
                               int outputCount, int sampleCount) override
    {
        const double started = Time::getMillisecondCounterHiRes();
        AudioProcessorPlayer::audioDeviceIOCallback(inputs, inputCount, outputs, outputCount, sampleCount);

        bool invalid = false;
        for (int channel = 0; channel < outputCount; ++channel)
            for (int sample = 0; sample < sampleCount; ++sample)
                if (!std::isfinite(outputs[channel][sample]))
                {
                    outputs[channel][sample] = 0.0f;
                    invalid = true;
                }
        if (invalid)
            invalidBuffers.fetch_add(1, std::memory_order_relaxed);

        const double rate = sampleRate.load(std::memory_order_relaxed);
        if (rate > 0.0 && Time::getMillisecondCounterHiRes() - started > 1000.0 * sampleCount / rate)
            missedDeadlines.fetch_add(1, std::memory_order_relaxed);
    }

private:
    void timerCallback() override
    {
        String event, error;
        {
            const ScopedLock eventGuard(eventLock);
            event.swapWith(pendingEvent);
            error.swapWith(pendingError);
        }
        const int late = missedDeadlines.exchange(0, std::memory_order_relaxed);
        const int invalid = invalidBuffers.exchange(0, std::memory_order_relaxed);
        if (event.isEmpty() && error.isEmpty() && late == 0 && invalid == 0)
            return;

        FileLogger::trimFileSize(logger.getLogFile(), maxLogBytes);
        const String timestamp = Time::getCurrentTime().toString(true, true) + " ";
        if (event.isNotEmpty())
            logger.logMessage(timestamp + event);
        if (error.isNotEmpty())
            logger.logMessage(timestamp + "Device error: " + error);
        if (late > 0 || invalid > 0)
            logger.logMessage(timestamp + "Callbacks exceeding buffer duration: " + String(late)
                + "; buffers containing non-finite output: " + String(invalid));
    }

    static const int maxLogBytes = 128 * 1024;
    FileLogger logger;
    std::atomic<double> sampleRate { 0.0 };
    std::atomic<int> missedDeadlines { 0 }, invalidBuffers { 0 };
    CriticalSection eventLock;
    String pendingEvent, pendingError;
};

#endif
