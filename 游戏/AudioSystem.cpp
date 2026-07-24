#include "AudioSystem.h"

#include "AssetPaths.h"

#include <windows.h>
#include <mmsystem.h>

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <string>
#include <utility>

#pragma comment(lib, "winmm.lib")

namespace
{
    // 所有合成音和外部语音统一使用 44.1 kHz、16 位、单声道，避免运行时重采样。
    constexpr int kSampleRate = 44100;
    constexpr float kPi = 3.14159265358979323846f;
    constexpr std::uint64_t kMaximumWaveFileBytes = 64ull * 1024ull * 1024ull;
    constexpr float kBackgroundMusicGain = 0.42f;
    const wchar_t* kUltimateActivationVoicePath =
        L"\u6e38\u620f\\assets\\audio\\ultimate_activation\\v1\\ultimate_activation_voice.wav";
    const wchar_t* kDefaultBackgroundMusicPath = L"\u6e38\u620f\\assets\\audio\\bgm\\haggstrom.mp3";
    const wchar_t* kHardBackgroundMusicPath =
        L"\u6e38\u620f\\assets\\audio\\bgm\\kafka_hard_dramatic_irony.mp3";
    const wchar_t* kBackgroundMusicAlias = L"QiongBackgroundMusic";
    const wchar_t* kBossVoiceAlias = L"QiongKafkaVoice";
    const wchar_t* kKafkaBattleStartVoicePath =
        L"\u6e38\u620f\\assets\\audio\\kafka_voice\\v1\\kafka_battle_start.mp3";
    const wchar_t* kKafkaTwoThirdsVoicePath =
        L"\u6e38\u620f\\assets\\audio\\kafka_voice\\v1\\kafka_two_thirds.mp3";
    const wchar_t* kKafkaOneThirdVoicePath =
        L"\u6e38\u620f\\assets\\audio\\kafka_voice\\v1\\kafka_one_third.mp3";

    // WAV 数值采用小端序；显式拼接字节可避免未对齐读取和主机布局依赖。
    std::uint16_t ReadLittleEndian16(const std::vector<unsigned char>& bytes, std::size_t offset)
    {
        return static_cast<std::uint16_t>(bytes[offset]) |
            (static_cast<std::uint16_t>(bytes[offset + 1]) << 8);
    }

    std::uint32_t ReadLittleEndian32(const std::vector<unsigned char>& bytes, std::size_t offset)
    {
        return static_cast<std::uint32_t>(bytes[offset]) |
            (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
            (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
            (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
    }

    bool HasChunkId(const std::vector<unsigned char>& bytes, std::size_t offset, const char* id)
    {
        return offset + 4 <= bytes.size() &&
            bytes[offset] == static_cast<unsigned char>(id[0]) &&
            bytes[offset + 1] == static_cast<unsigned char>(id[1]) &&
            bytes[offset + 2] == static_cast<unsigned char>(id[2]) &&
            bytes[offset + 3] == static_cast<unsigned char>(id[3]);
    }

    // 一次性读取 WAV。64 MiB 上限防止错误路径或损坏文件造成异常内存占用。
    bool ReadBinaryFile(const std::wstring& path, std::vector<unsigned char>& bytes)
    {
        const HANDLE file = CreateFileW(
            path.c_str(),
            GENERIC_READ,
            FILE_SHARE_READ,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            return false;
        }

        LARGE_INTEGER fileSize{};
        if (!GetFileSizeEx(file, &fileSize) ||
            fileSize.QuadPart <= 0 ||
            static_cast<std::uint64_t>(fileSize.QuadPart) > kMaximumWaveFileBytes)
        {
            CloseHandle(file);
            return false;
        }

        bytes.resize(static_cast<std::size_t>(fileSize.QuadPart));
        std::size_t totalRead = 0;
        // ReadFile 的单次请求长度是 DWORD，按 1 MiB 分块也让错误处理更明确。
        while (totalRead < bytes.size())
        {
            const std::size_t remaining = bytes.size() - totalRead;
            const DWORD requestSize = static_cast<DWORD>((std::min<std::size_t>)(remaining, 1024u * 1024u));
            DWORD bytesRead = 0;
            if (!ReadFile(file, bytes.data() + totalRead, requestSize, &bytesRead, nullptr) || bytesRead == 0)
            {
                CloseHandle(file);
                bytes.clear();
                return false;
            }
            totalRead += bytesRead;
        }

        CloseHandle(file);
        return true;
    }

    // 解析 RIFF chunk，并严格接受系统播放管线支持的 PCM 格式。
    bool LoadPcmMonoWave(const std::wstring& path, std::vector<short>& samples)
    {
        std::vector<unsigned char> bytes;
        if (!ReadBinaryFile(path, bytes) ||
            bytes.size() < 12 ||
            !HasChunkId(bytes, 0, "RIFF") ||
            !HasChunkId(bytes, 8, "WAVE"))
        {
            return false;
        }

        bool formatFound = false;
        bool dataFound = false;
        std::size_t dataOffset = 0;
        std::size_t dataSize = 0;
        std::size_t cursor = 12;
        // WAV 可在 fmt/data 之间插入未知 chunk；逐块扫描而不能假设固定偏移。
        while (cursor + 8 <= bytes.size())
        {
            const std::uint32_t chunkSize = ReadLittleEndian32(bytes, cursor + 4);
            const std::size_t chunkDataOffset = cursor + 8;
            if (static_cast<std::size_t>(chunkSize) > bytes.size() - chunkDataOffset)
            {
                return false;
            }

            if (HasChunkId(bytes, cursor, "fmt "))
            {
                if (chunkSize < 16)
                {
                    return false;
                }
                const std::uint16_t formatTag = ReadLittleEndian16(bytes, chunkDataOffset);
                const std::uint16_t channels = ReadLittleEndian16(bytes, chunkDataOffset + 2);
                const std::uint32_t sampleRate = ReadLittleEndian32(bytes, chunkDataOffset + 4);
                const std::uint16_t blockAlign = ReadLittleEndian16(bytes, chunkDataOffset + 12);
                const std::uint16_t bitsPerSample = ReadLittleEndian16(bytes, chunkDataOffset + 14);
                if (formatTag != WAVE_FORMAT_PCM ||
                    channels != 1 ||
                    sampleRate != kSampleRate ||
                    bitsPerSample != 16 ||
                    blockAlign != sizeof(short))
                {
                    return false;
                }
                formatFound = true;
            }
            else if (HasChunkId(bytes, cursor, "data") && !dataFound)
            {
                dataOffset = chunkDataOffset;
                dataSize = chunkSize;
                dataFound = true;
            }

            // RIFF chunk 按偶数字节对齐，奇数长度后有一个不计入 chunkSize 的填充字节。
            const std::size_t paddedChunkSize = static_cast<std::size_t>(chunkSize) + (chunkSize & 1u);
            if (paddedChunkSize > bytes.size() - chunkDataOffset)
            {
                break;
            }
            cursor = chunkDataOffset + paddedChunkSize;
        }

        if (!formatFound || !dataFound || dataSize == 0 || (dataSize % sizeof(short)) != 0)
        {
            return false;
        }

        const std::size_t sampleCount = dataSize / sizeof(short);
        samples.resize(sampleCount);
        for (std::size_t index = 0; index < sampleCount; ++index)
        {
            const std::uint16_t rawSample = ReadLittleEndian16(bytes, dataOffset + index * sizeof(short));
            const int signedSample = rawSample <= 0x7fffu
                ? static_cast<int>(rawSample)
                : static_cast<int>(rawSample) - 0x10000;
            samples[index] = static_cast<short>(signedSample);
        }
        return true;
    }
}

struct AudioSystem::Voice
{
    // samples 必须一直存活到 WHDR_DONE，否则 waveOut 会读取失效内存。
    HWAVEOUT device = nullptr;
    WAVEHDR header{};
    std::vector<short> samples;
    bool prepared = false;
};

AudioSystem::AudioSystem() = default;

AudioSystem::~AudioSystem()
{
    Stop();
}

void AudioSystem::Start()
{
    if (backgroundMusicOpen_)
    {
        return;
    }
    RestartBackgroundMusic();
}

void AudioSystem::Stop()
{
    CloseBossVoice();
    CloseBackgroundMusic();

    // reset 先让驱动归还仍在队列中的缓冲，之后才能安全 unprepare/close。
    for (const std::unique_ptr<Voice>& voice : voices_)
    {
        if (voice->device == nullptr)
        {
            continue;
        }
        waveOutReset(voice->device);
        if (voice->prepared)
        {
            waveOutUnprepareHeader(voice->device, &voice->header, sizeof(WAVEHDR));
        }
        waveOutClose(voice->device);
    }
    voices_.clear();
}

void AudioSystem::Update(float)
{
    // MP3 台词播放结束后关闭 MCI 设备；循环 BGM 使用独立别名，不受影响。
    if (bossVoiceOpen_)
    {
        wchar_t mode[32]{};
        const std::wstring statusCommand = L"status " + std::wstring(kBossVoiceAlias) + L" mode";
        if (mciSendStringW(statusCommand.c_str(), mode, static_cast<UINT>(_countof(mode)), nullptr) != 0 ||
            lstrcmpiW(mode, L"stopped") == 0)
        {
            CloseBossVoice();
        }
    }

    // erase-remove 同时检查完成状态并释放每个 Voice 的原生句柄。
    voices_.erase(
        std::remove_if(
            voices_.begin(),
            voices_.end(),
            [](const std::unique_ptr<Voice>& voice)
            {
                if ((voice->header.dwFlags & WHDR_DONE) == 0)
                {
                    return false;
                }
                if (voice->prepared)
                {
                    waveOutUnprepareHeader(voice->device, &voice->header, sizeof(WAVEHDR));
                }
                waveOutClose(voice->device);
                return true;
            }),
        voices_.end());
}

void AudioSystem::SetMasterVolume(float volume)
{
    masterVolume_ = (std::max)(0.0f, (std::min)(1.0f, volume));
    UpdateBackgroundMusicVolume();
    UpdateBossVoiceVolume();
}

void AudioSystem::SetEffectsVolume(float volume)
{
    effectsVolume_ = (std::max)(0.0f, (std::min)(1.0f, volume));
    UpdateBossVoiceVolume();
}

void AudioSystem::SetHardModeMusic(bool enabled, bool restart)
{
    const bool trackChanged = hardModeMusic_ != enabled;
    hardModeMusic_ = enabled;
    if (restart)
    {
        CloseBossVoice();
    }
    if (trackChanged || restart || !backgroundMusicOpen_)
    {
        RestartBackgroundMusic();
    }
}

void AudioSystem::CloseBackgroundMusic()
{
    if (!backgroundMusicOpen_)
    {
        return;
    }
    const std::wstring stopCommand = L"stop " + std::wstring(kBackgroundMusicAlias);
    const std::wstring closeCommand = L"close " + std::wstring(kBackgroundMusicAlias);
    mciSendStringW(stopCommand.c_str(), nullptr, 0, nullptr);
    mciSendStringW(closeCommand.c_str(), nullptr, 0, nullptr);
    backgroundMusicOpen_ = false;
}

void AudioSystem::RestartBackgroundMusic()
{
    CloseBackgroundMusic();

    // MCI 原生流式播放 MP3。困难资源异常时退回默认曲目，避免整个游戏静音。
    const auto openTrack = [this](const wchar_t* relativePath)
    {
        const std::wstring path = FindAssetPath(relativePath);
        const std::wstring openCommand =
            L"open \"" + path + L"\" type mpegvideo alias " + kBackgroundMusicAlias;
        if (mciSendStringW(openCommand.c_str(), nullptr, 0, nullptr) != 0)
        {
            return false;
        }

        backgroundMusicOpen_ = true;
        UpdateBackgroundMusicVolume();
        const std::wstring playCommand = L"play " + std::wstring(kBackgroundMusicAlias) + L" repeat";
        if (mciSendStringW(playCommand.c_str(), nullptr, 0, nullptr) == 0)
        {
            return true;
        }
        CloseBackgroundMusic();
        return false;
    };

    const wchar_t* selectedPath = hardModeMusic_
        ? kHardBackgroundMusicPath
        : kDefaultBackgroundMusicPath;
    if (!openTrack(selectedPath) && hardModeMusic_)
    {
        openTrack(kDefaultBackgroundMusicPath);
    }
}

void AudioSystem::CloseBossVoice()
{
    if (!bossVoiceOpen_)
    {
        return;
    }
    const std::wstring stopCommand = L"stop " + std::wstring(kBossVoiceAlias);
    const std::wstring closeCommand = L"close " + std::wstring(kBossVoiceAlias);
    mciSendStringW(stopCommand.c_str(), nullptr, 0, nullptr);
    mciSendStringW(closeCommand.c_str(), nullptr, 0, nullptr);
    bossVoiceOpen_ = false;
}

void AudioSystem::PlayMciVoice(const wchar_t* relativePath)
{
    if (relativePath == nullptr || masterVolume_ * effectsVolume_ <= 0.001f)
    {
        return;
    }
    CloseBossVoice();
    const std::wstring path = FindAssetPath(relativePath);
    const std::wstring openCommand =
        L"open \"" + path + L"\" type mpegvideo alias " + kBossVoiceAlias;
    if (mciSendStringW(openCommand.c_str(), nullptr, 0, nullptr) != 0)
    {
        return;
    }

    bossVoiceOpen_ = true;
    UpdateBossVoiceVolume();
    const std::wstring playCommand = L"play " + std::wstring(kBossVoiceAlias) + L" from 0";
    if (mciSendStringW(playCommand.c_str(), nullptr, 0, nullptr) != 0)
    {
        CloseBossVoice();
    }
}

void AudioSystem::UpdateBackgroundMusicVolume()
{
    if (!backgroundMusicOpen_)
    {
        return;
    }

    const int volume = static_cast<int>(
        (std::max)(0.0f, (std::min)(1.0f, masterVolume_ * kBackgroundMusicGain)) * 1000.0f + 0.5f);
    const std::wstring command =
        L"setaudio " + std::wstring(kBackgroundMusicAlias) + L" volume to " + std::to_wstring(volume);
    mciSendStringW(command.c_str(), nullptr, 0, nullptr);
}

void AudioSystem::UpdateBossVoiceVolume()
{
    if (!bossVoiceOpen_)
    {
        return;
    }
    const int volume = static_cast<int>(
        (std::max)(0.0f, (std::min)(1.0f, masterVolume_ * effectsVolume_)) * 1000.0f + 0.5f);
    const std::wstring command =
        L"setaudio " + std::wstring(kBossVoiceAlias) + L" volume to " + std::to_wstring(volume);
    mciSendStringW(command.c_str(), nullptr, 0, nullptr);
}

void AudioSystem::PlayHitSound(bool heavy)
{
    const float volume = (std::max)(0.0f, (std::min)(1.0f, masterVolume_ * effectsVolume_));
    if (volume <= 0.001f)
    {
        return;
    }

    const float duration = heavy ? 0.18f : 0.12f;
    const int sampleCount = static_cast<int>(duration * static_cast<float>(kSampleRate));
    std::vector<short> samples(sampleCount);

    // 低频正弦提供撞击主体，高频 snap 提供瞬态，伪随机噪声提供材质感。
    // 固定种子保证相同音效每次播放都一致。
    unsigned int noiseState = heavy ? 0x8f32a1u : 0x41c64eu;
    for (int index = 0; index < sampleCount; ++index)
    {
        const float time = static_cast<float>(index) / static_cast<float>(kSampleRate);
        const float progress = time / duration;
        const float envelope = std::exp(-(heavy ? 18.0f : 27.0f) * time) * (1.0f - progress);
        const float baseFrequency = heavy
            ? 118.0f - progress * 46.0f
            : 205.0f - progress * 82.0f;
        noiseState = noiseState * 1664525u + 1013904223u;
        const float noise = (static_cast<float>((noiseState >> 9) & 0x7fffu) / 16383.5f) - 1.0f;
        const float body = std::sin(2.0f * kPi * baseFrequency * time);
        const float snap = std::sin(2.0f * kPi * (heavy ? 720.0f : 1280.0f) * time) * std::exp(-55.0f * time);
        const float value = (body * (heavy ? 0.68f : 0.52f) + snap * 0.34f + noise * 0.24f) * envelope * volume;
        const float clamped = (std::max)(-1.0f, (std::min)(1.0f, value));
        samples[index] = static_cast<short>(clamped * 32767.0f);
    }

    PlayPcmSamples(std::move(samples));
}

void AudioSystem::PlayBlockSound()
{
    const float volume = (std::max)(0.0f, (std::min)(1.0f, masterVolume_ * effectsVolume_));
    if (volume <= 0.001f)
    {
        return;
    }

    constexpr float duration = 0.17f;
    const int sampleCount = static_cast<int>(duration * static_cast<float>(kSampleRate));
    std::vector<short> samples(sampleCount);
    unsigned int noiseState = 0x7f4a21d3u;
    for (int index = 0; index < sampleCount; ++index)
    {
        const float time = static_cast<float>(index) / static_cast<float>(kSampleRate);
        const float progress = time / duration;
        const float envelope = std::exp(-18.0f * time) * (1.0f - progress);
        noiseState = noiseState * 1664525u + 1013904223u;
        const float noise = (static_cast<float>((noiseState >> 9) & 0x7fffu) / 16383.5f) - 1.0f;
        const float metallic =
            std::sin(2.0f * kPi * 910.0f * time) * 0.58f +
            std::sin(2.0f * kPi * 1460.0f * time) * 0.34f +
            std::sin(2.0f * kPi * 2190.0f * time) * 0.18f;
        const float shieldPulse = std::sin(2.0f * kPi * 330.0f * time) * std::exp(-31.0f * time) * 0.32f;
        const float transient = noise * std::exp(-76.0f * time) * 0.25f;
        const float value = (metallic * envelope + shieldPulse + transient) * volume * 0.72f;
        const float clamped = (std::max)(-1.0f, (std::min)(1.0f, value));
        samples[index] = static_cast<short>(clamped * 32767.0f);
    }

    PlayPcmSamples(std::move(samples));
}

void AudioSystem::PlayEmpoweredAttackSound(int comboIndex, bool hitTarget, bool finisher)
{
    const float volume = (std::max)(0.0f, (std::min)(1.0f, masterVolume_ * effectsVolume_));
    if (volume <= 0.001f)
    {
        return;
    }

    // 连段越靠后越响、越长；终结段额外叠加低音和延迟能量环。
    const int stage = (std::max)(0, (std::min)(4, comboIndex));
    const float stageStrength = 0.76f + static_cast<float>(stage) * 0.06f;
    const float duration = finisher ? 0.34f : 0.21f + static_cast<float>(stage) * 0.014f;
    const int sampleCount = static_cast<int>(duration * static_cast<float>(kSampleRate));
    std::vector<short> samples(sampleCount);

    unsigned int noiseState = 0x91e10da5u ^ (static_cast<unsigned int>(stage) * 0x45d9f3bu);
    const float sweepStart = 930.0f + static_cast<float>(stage) * 105.0f;
    const float sweepEnd = 185.0f + static_cast<float>(stage) * 22.0f;
    for (int index = 0; index < sampleCount; ++index)
    {
        const float time = static_cast<float>(index) / static_cast<float>(kSampleRate);
        const float progress = time / duration;
        const float sweepPhase = 2.0f * kPi *
            (sweepStart * time + 0.5f * (sweepEnd - sweepStart) * time * time / duration);
        const float whooshEnvelope =
            std::sin(kPi * (std::min)(1.0f, progress * 1.18f)) * std::exp(-1.55f * progress);

        noiseState = noiseState * 1664525u + 1013904223u;
        const float noise = (static_cast<float>((noiseState >> 8) & 0xffffu) / 32767.5f) - 1.0f;
        const float energySweep =
            std::sin(sweepPhase) * 0.48f +
            std::sin(sweepPhase * 1.57f + 0.35f) * 0.22f +
            noise * (0.20f + 0.10f * progress);

        float value = energySweep * whooshEnvelope * stageStrength;
        // 未命中时只保留挥动声，命中时才叠加快速衰减的撞击分量。
        if (hitTarget)
        {
            const float impactEnvelope = std::exp(-(finisher ? 20.0f : 31.0f) * time);
            const float impactBody =
                std::sin(2.0f * kPi * (finisher ? 78.0f : 126.0f) * time) * 0.70f +
                std::sin(2.0f * kPi * (finisher ? 214.0f : 310.0f) * time) * 0.28f +
                noise * 0.28f;
            value += impactBody * impactEnvelope;
        }
        if (finisher)
        {
            const float bassEnvelope = std::exp(-8.0f * time) * (1.0f - progress);
            const float bass =
                std::sin(2.0f * kPi * (72.0f - progress * 24.0f) * time) * 0.58f +
                std::sin(2.0f * kPi * 144.0f * time) * 0.18f;
            const float ringDelay = (std::max)(0.0f, time - 0.055f);
            const float energyRing =
                std::sin(2.0f * kPi * (520.0f - ringDelay * 620.0f) * ringDelay) *
                std::exp(-13.0f * ringDelay) * (time >= 0.055f ? 0.34f : 0.0f);
            value += bass * bassEnvelope + energyRing;
        }

        value *= volume * 0.80f;
        const float clamped = (std::max)(-1.0f, (std::min)(1.0f, value));
        samples[index] = static_cast<short>(clamped * 32767.0f);
    }

    PlayPcmSamples(std::move(samples));
}

void AudioSystem::PlaySuperBatExplosionSound(bool hitTarget)
{
    const float volume = (std::max)(0.0f, (std::min)(1.0f, masterVolume_ * effectsVolume_));
    if (volume <= 0.001f)
    {
        return;
    }

    constexpr float duration = 0.48f;
    const int sampleCount = static_cast<int>(duration * static_cast<float>(kSampleRate));
    std::vector<short> samples(sampleCount);
    unsigned int noiseState = hitTarget ? 0xd1b54a35u : 0x94d049bbu;

    for (int index = 0; index < sampleCount; ++index)
    {
        const float time = static_cast<float>(index) / static_cast<float>(kSampleRate);
        const float progress = time / duration;
        noiseState = noiseState * 1664525u + 1013904223u;
        const float noise = (static_cast<float>((noiseState >> 8) & 0xffffu) / 32767.5f) - 1.0f;

        const float bassFrequency = 96.0f - progress * 48.0f;
        const float bass =
            std::sin(2.0f * kPi * bassFrequency * time) * 0.70f +
            std::sin(2.0f * kPi * bassFrequency * 0.5f * time) * 0.30f;
        const float blastEnvelope = std::exp(-9.0f * time) * (1.0f - progress);
        const float crack = noise * std::exp(-36.0f * time) * (hitTarget ? 0.58f : 0.42f);

        // 三个错开的下扫频脉冲让爆炸尾音呈现向外扩散的层次。
        float rings = 0.0f;
        constexpr float ringDelays[] = { 0.035f, 0.105f, 0.178f };
        for (int ring = 0; ring < 3; ++ring)
        {
            const float ringTime = time - ringDelays[ring];
            if (ringTime < 0.0f)
            {
                continue;
            }
            const float ringFrequency = 610.0f - static_cast<float>(ring) * 92.0f - ringTime * 660.0f;
            rings += std::sin(2.0f * kPi * ringFrequency * ringTime) *
                std::exp(-15.0f * ringTime) * (0.34f - static_cast<float>(ring) * 0.045f);
        }

        const float value = (bass * blastEnvelope + crack + rings) * volume * 0.86f;
        const float clamped = (std::max)(-1.0f, (std::min)(1.0f, value));
        samples[index] = static_cast<short>(clamped * 32767.0f);
    }

    PlayPcmSamples(std::move(samples));
}

void AudioSystem::PlayUltimateActivationVoice()
{
    const float volume = (std::max)(0.0f, (std::min)(1.0f, masterVolume_ * effectsVolume_));
    if (volume <= 0.001f)
    {
        return;
    }

    std::vector<short> samples;
    if (!LoadPcmMonoWave(FindAssetPath(kUltimateActivationVoicePath), samples))
    {
        return;
    }

    // 对外部语音逐样本应用当前分类音量，并饱和到 int16 范围。
    for (short& sample : samples)
    {
        const float scaled = static_cast<float>(sample) * volume;
        const float clamped = (std::max)(-32768.0f, (std::min)(32767.0f, scaled));
        sample = static_cast<short>(std::lround(clamped));
    }

    PlayPcmSamples(std::move(samples));
}

void AudioSystem::PlayKafkaBattleStartVoice()
{
    PlayMciVoice(kKafkaBattleStartVoicePath);
}

void AudioSystem::PlayKafkaTwoThirdsVoice()
{
    PlayMciVoice(kKafkaTwoThirdsVoicePath);
}

void AudioSystem::PlayKafkaOneThirdVoice()
{
    PlayMciVoice(kKafkaOneThirdVoicePath);
}

void AudioSystem::PlayPcmSamples(std::vector<short> samples)
{
    if (samples.empty())
    {
        return;
    }

    // 把样本移入 Voice 后地址保持稳定，直到设备通过 WHDR_DONE 宣告完成。
    std::unique_ptr<Voice> voice = std::make_unique<Voice>();
    voice->samples = std::move(samples);

    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 1;
    format.nSamplesPerSec = kSampleRate;
    format.wBitsPerSample = 16;
    format.nBlockAlign = format.nChannels * format.wBitsPerSample / 8;
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;

    // 任一步失败都在此处释放已经取得的资源；只有成功提交的 Voice 才进入容器。
    if (waveOutOpen(&voice->device, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR)
    {
        return;
    }

    voice->header.lpData = reinterpret_cast<LPSTR>(voice->samples.data());
    voice->header.dwBufferLength = static_cast<DWORD>(voice->samples.size() * sizeof(short));
    if (waveOutPrepareHeader(voice->device, &voice->header, sizeof(WAVEHDR)) != MMSYSERR_NOERROR)
    {
        waveOutClose(voice->device);
        return;
    }
    voice->prepared = true;
    if (waveOutWrite(voice->device, &voice->header, sizeof(WAVEHDR)) != MMSYSERR_NOERROR)
    {
        waveOutUnprepareHeader(voice->device, &voice->header, sizeof(WAVEHDR));
        waveOutClose(voice->device);
        return;
    }
    voices_.push_back(std::move(voice));
}
