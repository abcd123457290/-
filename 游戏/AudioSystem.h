#pragma once

#include <memory>
#include <vector>

// 游戏音效管理器。
//
// 当前音效由代码实时合成 PCM 波形，而不是从磁盘加载音频文件。每次播放
// 都会创建一个 Voice；Update 会回收已经播放完毕的 Voice，Stop 则立即释放
// 全部系统音频资源。
class AudioSystem
{
public:
    AudioSystem();
    ~AudioSystem();

    // 初始化系统音频后端。可重复调用，后续调用不会重复创建引擎。
    void Start();
    // 停止所有在播声音并关闭后端；程序退出或重新加载内容时调用。
    void Stop();
    // 清理已经播放结束的临时 Voice。deltaSeconds 预留给按时间更新的音频效果。
    void Update(float deltaSeconds);
    // 设置全局音量和音效分类音量，输入值会限制到 [0, 1]。
    void SetMasterVolume(float volume);
    void SetEffectsVolume(float volume);
    // 切换普通/困难模式循环 BGM；restart=true 用于新回合从曲首重新播放。
    void SetHardModeMusic(bool enabled, bool restart = false);

    // 根据攻击类型、连段阶段和是否命中，合成不同力度的反馈音。
    void PlayHitSound(bool heavy = false);
    // 合成高频金属/护盾格挡音，与普通受伤和重击的低频撞击声明确区分。
    void PlayBlockSound();
    void PlayEmpoweredAttackSound(int comboIndex, bool hitTarget, bool finisher);
    void PlaySuperBatExplosionSound(bool hitTarget);
    void PlayUltimateActivationVoice();
    // 卡芙卡困难模式语音使用独立 MCI 通道，可与循环 BGM 同时播放。
    void PlayKafkaBattleStartVoice();
    void PlayKafkaTwoThirdsVoice();
    void PlayKafkaOneThirdVoice();

private:
    // 封装单次播放需要的 WAVEHDR、样本缓冲和 waveOut 句柄。
    // 具体定义放在 .cpp 中，避免把 Windows 多媒体细节暴露给使用者。
    struct Voice;

    // 接管 samples 的所有权并异步播放；音量缩放在提交给设备前完成。
    void PlayPcmSamples(std::vector<short> samples);
    // 根据 hardModeMusic_ 关闭并重新打开对应的 MP3 循环轨道。
    void RestartBackgroundMusic();
    void CloseBackgroundMusic();
    // 单条 MP3 语音共用固定别名；新语音开始前会关闭上一条，防止台词重叠。
    void PlayMciVoice(const wchar_t* relativePath);
    void CloseBossVoice();
    // 将主音量同步到独立循环播放的背景音乐设备。
    void UpdateBackgroundMusicVolume();
    void UpdateBossVoiceVolume();

    // 最终播放增益为 masterVolume_ * effectsVolume_。
    float masterVolume_ = 0.8f;
    float effectsVolume_ = 0.9f;
    bool backgroundMusicOpen_ = false;
    bool hardModeMusic_ = false;
    bool bossVoiceOpen_ = false;
    // 同时在播以及等待 Update 回收的声音实例。
    std::vector<std::unique_ptr<Voice>> voices_;
};
