#ifndef LIGHTHOST_AUDIO_SETUP_H
#define LIGHTHOST_AUDIO_SETUP_H

namespace AudioSetup
{
// Keep the user's first selected channels, including a pair other than channels 1/2.
// Missing masks use the stereo defaults passed to AudioDeviceManager::initialise.
inline void limitSavedChannelsToStereo(XmlElement& settings)
{
    for (const char* attribute : { "audioDeviceInChans", "audioDeviceOutChans" })
    {
        if (!settings.hasAttribute(attribute))
            continue;

        BigInteger channels;
        channels.parseString(settings.getStringAttribute(attribute), 2);
        int channel = channels.findNextSetBit(0);
        for (int count = 0; channel >= 0; channel = channels.findNextSetBit(channel + 1))
            if (++count > 2)
                channels.clearBit(channel);
        settings.setAttribute(attribute, channels.toString(2));
    }
}
}

#endif
