#include "BossAiModel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <string>
#include <utility>

#include <shlobj.h>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

namespace
{
    constexpr wchar_t kModelFileName[] = L"kafka_boss_ai_model.json";
    constexpr char kExpectedFormat[] = "kafka_boss_tabular_q_v1";
    constexpr std::size_t kMaximumModelBytes = 8u * 1024u * 1024u;
    constexpr int kMaximumJsonDepth = 32;

    // 只读 JSON 游标会验证完整语法，但不会为 q_values/visit_counts 建立对象树。
    // 这让游戏可以安全跳过训练数据，只把 7776 项 policy_flat 留在内存中。
    class JsonReader
    {
    public:
        explicit JsonReader(const std::string& source)
            : source_(source)
        {
            // UTF-8 BOM 不是 JSON 正文，但部分文本编辑器保存时会自动添加。
            if (source_.size() >= 3u &&
                static_cast<unsigned char>(source_[0]) == 0xefu &&
                static_cast<unsigned char>(source_[1]) == 0xbbu &&
                static_cast<unsigned char>(source_[2]) == 0xbfu)
            {
                position_ = 3u;
            }
        }

        bool Consume(char expected)
        {
            SkipWhitespace();
            if (position_ >= source_.size() || source_[position_] != expected)
            {
                return false;
            }
            ++position_;
            return true;
        }

        bool TryConsume(char expected)
        {
            SkipWhitespace();
            if (position_ < source_.size() && source_[position_] == expected)
            {
                ++position_;
                return true;
            }
            return false;
        }

        bool ReadString(std::string& value)
        {
            SkipWhitespace();
            if (position_ >= source_.size() || source_[position_] != '"')
            {
                return false;
            }
            ++position_;
            value.clear();
            while (position_ < source_.size())
            {
                const unsigned char character = static_cast<unsigned char>(source_[position_++]);
                if (character == '"')
                {
                    return true;
                }
                if (character < 0x20u)
                {
                    return false;
                }
                if (character != '\\')
                {
                    value.push_back(static_cast<char>(character));
                    continue;
                }
                if (position_ >= source_.size())
                {
                    return false;
                }
                const char escape = source_[position_++];
                switch (escape)
                {
                case '"': value.push_back('"'); break;
                case '\\': value.push_back('\\'); break;
                case '/': value.push_back('/'); break;
                case 'b': value.push_back('\b'); break;
                case 'f': value.push_back('\f'); break;
                case 'n': value.push_back('\n'); break;
                case 'r': value.push_back('\r'); break;
                case 't': value.push_back('\t'); break;
                case 'u':
                {
                    // 解码 ASCII 范围，保证 f\u006frmat 与 format 被视为同一键；
                    // 非 ASCII 内容无需参与 ABI 比较，但仍严格验证代理对结构。
                    unsigned int codeUnit = 0u;
                    for (int digit = 0; digit < 4; ++digit)
                    {
                        if (position_ >= source_.size() || !IsHexDigit(source_[position_]))
                        {
                            return false;
                        }
                        codeUnit = codeUnit * 16u + HexValue(source_[position_]);
                        ++position_;
                    }
                    if (codeUnit >= 0xd800u && codeUnit <= 0xdbffu)
                    {
                        if (position_ + 6u > source_.size() || source_[position_] != '\\' ||
                            source_[position_ + 1u] != 'u')
                        {
                            return false;
                        }
                        position_ += 2u;
                        unsigned int lowSurrogate = 0u;
                        for (int digit = 0; digit < 4; ++digit)
                        {
                            if (position_ >= source_.size() || !IsHexDigit(source_[position_]))
                            {
                                return false;
                            }
                            lowSurrogate = lowSurrogate * 16u + HexValue(source_[position_]);
                            ++position_;
                        }
                        if (lowSurrogate < 0xdc00u || lowSurrogate > 0xdfffu)
                        {
                            return false;
                        }
                        value.push_back('?');
                    }
                    else if (codeUnit >= 0xdc00u && codeUnit <= 0xdfffu)
                    {
                        return false;
                    }
                    else if (codeUnit <= 0x7fu)
                    {
                        value.push_back(static_cast<char>(codeUnit));
                    }
                    else
                    {
                        value.push_back('?');
                    }
                    break;
                }
                default:
                    return false;
                }
            }
            return false;
        }

        bool ReadUnsigned(std::size_t& value)
        {
            std::size_t begin = 0;
            std::size_t end = 0;
            if (!ScanNumber(begin, end) || begin == end)
            {
                return false;
            }
            value = 0;
            for (std::size_t index = begin; index < end; ++index)
            {
                const char character = source_[index];
                if (character < '0' || character > '9')
                {
                    return false;
                }
                const std::size_t digit = static_cast<std::size_t>(character - '0');
                if (value > ((std::numeric_limits<std::size_t>::max)() - digit) / 10u)
                {
                    return false;
                }
                value = value * 10u + digit;
            }
            return true;
        }

        bool SkipValue(int depth = 0)
        {
            if (depth > kMaximumJsonDepth)
            {
                return false;
            }
            SkipWhitespace();
            if (position_ >= source_.size())
            {
                return false;
            }
            if (source_[position_] == '"')
            {
                std::string ignored;
                return ReadString(ignored);
            }
            if (source_[position_] == '{')
            {
                ++position_;
                bool first = true;
                while (!TryConsume('}'))
                {
                    if (!first && !Consume(','))
                    {
                        return false;
                    }
                    std::string key;
                    if (!ReadString(key) || !Consume(':') || !SkipValue(depth + 1))
                    {
                        return false;
                    }
                    first = false;
                }
                return true;
            }
            if (source_[position_] == '[')
            {
                ++position_;
                bool first = true;
                while (!TryConsume(']'))
                {
                    if (!first && !Consume(','))
                    {
                        return false;
                    }
                    if (!SkipValue(depth + 1))
                    {
                        return false;
                    }
                    first = false;
                }
                return true;
            }
            if (MatchLiteral("true") || MatchLiteral("false") || MatchLiteral("null"))
            {
                return true;
            }
            std::size_t begin = 0;
            std::size_t end = 0;
            return ScanNumber(begin, end);
        }

        bool IsAtDocumentEnd()
        {
            SkipWhitespace();
            return position_ == source_.size();
        }

    private:
        static bool IsHexDigit(char character)
        {
            return (character >= '0' && character <= '9') ||
                (character >= 'a' && character <= 'f') ||
                (character >= 'A' && character <= 'F');
        }

        static unsigned int HexValue(char character)
        {
            if (character >= '0' && character <= '9')
            {
                return static_cast<unsigned int>(character - '0');
            }
            if (character >= 'a' && character <= 'f')
            {
                return 10u + static_cast<unsigned int>(character - 'a');
            }
            return 10u + static_cast<unsigned int>(character - 'A');
        }

        void SkipWhitespace()
        {
            while (position_ < source_.size() &&
                (source_[position_] == ' ' || source_[position_] == '\t' ||
                    source_[position_] == '\r' || source_[position_] == '\n'))
            {
                ++position_;
            }
        }

        bool MatchLiteral(const char* literal)
        {
            std::size_t length = 0;
            while (literal[length] != '\0')
            {
                ++length;
            }
            if (source_.compare(position_, length, literal) != 0)
            {
                return false;
            }
            position_ += length;
            return true;
        }

        bool ScanNumber(std::size_t& begin, std::size_t& end)
        {
            SkipWhitespace();
            begin = position_;
            if (position_ < source_.size() && source_[position_] == '-')
            {
                ++position_;
            }
            if (position_ >= source_.size())
            {
                position_ = begin;
                return false;
            }
            if (source_[position_] == '0')
            {
                ++position_;
                // JSON 不允许 01 这种带前导零的整数。
                if (position_ < source_.size() && source_[position_] >= '0' && source_[position_] <= '9')
                {
                    position_ = begin;
                    return false;
                }
            }
            else if (source_[position_] >= '1' && source_[position_] <= '9')
            {
                while (position_ < source_.size() &&
                    source_[position_] >= '0' && source_[position_] <= '9')
                {
                    ++position_;
                }
            }
            else
            {
                position_ = begin;
                return false;
            }
            if (position_ < source_.size() && source_[position_] == '.')
            {
                ++position_;
                const std::size_t fractionBegin = position_;
                while (position_ < source_.size() &&
                    source_[position_] >= '0' && source_[position_] <= '9')
                {
                    ++position_;
                }
                if (position_ == fractionBegin)
                {
                    position_ = begin;
                    return false;
                }
            }
            if (position_ < source_.size() &&
                (source_[position_] == 'e' || source_[position_] == 'E'))
            {
                ++position_;
                if (position_ < source_.size() &&
                    (source_[position_] == '+' || source_[position_] == '-'))
                {
                    ++position_;
                }
                const std::size_t exponentBegin = position_;
                while (position_ < source_.size() &&
                    source_[position_] >= '0' && source_[position_] <= '9')
                {
                    ++position_;
                }
                if (position_ == exponentBegin)
                {
                    position_ = begin;
                    return false;
                }
            }
            end = position_;
            return true;
        }

        const std::string& source_; // 完整 UTF-8 JSON 字节串，由 LoadFromDesktop 持有。
        std::size_t position_ = 0;  // 下一次读取的位置；解析失败时整份模型都会丢弃。
    };

    bool ParseExactStringArray(
        JsonReader& reader,
        const char* const* expected,
        std::size_t expectedCount)
    {
        if (!reader.Consume('['))
        {
            return false;
        }
        for (std::size_t index = 0; index < expectedCount; ++index)
        {
            if (index > 0u && !reader.Consume(','))
            {
                return false;
            }
            std::string value;
            if (!reader.ReadString(value) || value != expected[index])
            {
                return false;
            }
        }
        return reader.Consume(']');
    }

    bool ParseExactUnsignedArray(
        JsonReader& reader,
        const std::size_t* expected,
        std::size_t expectedCount)
    {
        if (!reader.Consume('['))
        {
            return false;
        }
        for (std::size_t index = 0; index < expectedCount; ++index)
        {
            if (index > 0u && !reader.Consume(','))
            {
                return false;
            }
            std::size_t value = 0;
            if (!reader.ReadUnsigned(value) || value != expected[index])
            {
                return false;
            }
        }
        return reader.Consume(']');
    }

    bool ParseActions(JsonReader& reader)
    {
        const char* expectedNames[] = { "katana", "summon", "smg" };
        const char* expectedSkills[] = { "KatanaWarning", "Summoning", "SmgWarning" };
        if (!reader.Consume('['))
        {
            return false;
        }
        for (std::size_t actionIndex = 0; actionIndex < 3u; ++actionIndex)
        {
            if (actionIndex > 0u && !reader.Consume(','))
            {
                return false;
            }
            if (!reader.Consume('{'))
            {
                return false;
            }
            bool sawId = false;
            bool sawName = false;
            bool sawCppSkill = false;
            bool firstProperty = true;
            std::size_t id = 0;
            std::string name;
            std::string cppSkill;
            while (!reader.TryConsume('}'))
            {
                if (!firstProperty && !reader.Consume(','))
                {
                    return false;
                }
                std::string key;
                if (!reader.ReadString(key) || !reader.Consume(':'))
                {
                    return false;
                }
                if (key == "id")
                {
                    if (sawId || !reader.ReadUnsigned(id)) return false;
                    sawId = true;
                }
                else if (key == "name")
                {
                    if (sawName || !reader.ReadString(name)) return false;
                    sawName = true;
                }
                else if (key == "cpp_skill")
                {
                    if (sawCppSkill || !reader.ReadString(cppSkill)) return false;
                    sawCppSkill = true;
                }
                else if (!reader.SkipValue(1))
                {
                    return false;
                }
                firstProperty = false;
            }
            if (!sawId || !sawName || !sawCppSkill || id != actionIndex ||
                name != expectedNames[actionIndex] || cppSkill != expectedSkills[actionIndex])
            {
                return false;
            }
        }
        return reader.Consume(']');
    }

    bool ParseStateSchema(JsonReader& reader)
    {
        const char* expectedFields[] = {
            "boss_hp_bucket",
            "player_hp_bucket",
            "abs_dx_bucket",
            "abs_dy_bucket",
            "minion_count",
            "player_status_mask",
            "rage_bucket"
        };
        const std::size_t expectedShape[] = { 3u, 3u, 3u, 3u, 4u, 8u, 3u };
        if (!reader.Consume('{'))
        {
            return false;
        }
        bool sawVersion = false;
        bool sawFieldOrder = false;
        bool sawShape = false;
        bool sawStateCount = false;
        bool firstProperty = true;
        while (!reader.TryConsume('}'))
        {
            if (!firstProperty && !reader.Consume(','))
            {
                return false;
            }
            std::string key;
            if (!reader.ReadString(key) || !reader.Consume(':'))
            {
                return false;
            }
            if (key == "version")
            {
                std::size_t version = 0;
                if (sawVersion || !reader.ReadUnsigned(version) || version != 1u) return false;
                sawVersion = true;
            }
            else if (key == "field_order")
            {
                if (sawFieldOrder || !ParseExactStringArray(reader, expectedFields, 7u)) return false;
                sawFieldOrder = true;
            }
            else if (key == "shape")
            {
                if (sawShape || !ParseExactUnsignedArray(reader, expectedShape, 7u)) return false;
                sawShape = true;
            }
            else if (key == "state_count")
            {
                std::size_t count = 0;
                if (sawStateCount || !reader.ReadUnsigned(count) || count != BossAiModel::StateCount) return false;
                sawStateCount = true;
            }
            else if (!reader.SkipValue(1))
            {
                return false;
            }
            firstProperty = false;
        }
        return sawVersion && sawFieldOrder && sawShape && sawStateCount;
    }

    bool ParsePolicy(JsonReader& reader, std::vector<std::uint8_t>& policy)
    {
        if (!reader.Consume('['))
        {
            return false;
        }
        policy.clear();
        policy.reserve(BossAiModel::StateCount);
        for (std::size_t state = 0; state < BossAiModel::StateCount; ++state)
        {
            if (state > 0u && !reader.Consume(','))
            {
                policy.clear();
                return false;
            }
            std::size_t action = 0;
            if (!reader.ReadUnsigned(action) || action >= BossAiModel::ActionCount)
            {
                policy.clear();
                return false;
            }
            policy.push_back(static_cast<std::uint8_t>(action));
        }
        if (!reader.Consume(']'))
        {
            policy.clear();
            return false;
        }
        return true;
    }

    bool ParseModelDocument(const std::string& json, std::vector<std::uint8_t>& policy)
    {
        JsonReader reader(json);
        if (!reader.Consume('{'))
        {
            return false;
        }
        bool sawFormat = false;
        bool sawActions = false;
        bool sawStateSchema = false;
        bool sawPolicy = false;
        bool firstProperty = true;
        while (!reader.TryConsume('}'))
        {
            if (!firstProperty && !reader.Consume(','))
            {
                return false;
            }
            std::string key;
            if (!reader.ReadString(key) || !reader.Consume(':'))
            {
                return false;
            }
            if (key == "format")
            {
                std::string format;
                if (sawFormat || !reader.ReadString(format) || format != kExpectedFormat) return false;
                sawFormat = true;
            }
            else if (key == "actions")
            {
                if (sawActions || !ParseActions(reader)) return false;
                sawActions = true;
            }
            else if (key == "state_schema")
            {
                if (sawStateSchema || !ParseStateSchema(reader)) return false;
                sawStateSchema = true;
            }
            else if (key == "policy_flat")
            {
                if (sawPolicy || !ParsePolicy(reader, policy)) return false;
                sawPolicy = true;
            }
            else if (!reader.SkipValue(1))
            {
                return false;
            }
            firstProperty = false;
        }
        return sawFormat && sawActions && sawStateSchema && sawPolicy && reader.IsAtDocumentEnd();
    }

    bool ReadWholeFile(const std::wstring& path, std::string& contents)
    {
        FILE* rawFile = nullptr;
        if (_wfopen_s(&rawFile, path.c_str(), L"rb") != 0 || rawFile == nullptr)
        {
            return false;
        }
        std::unique_ptr<FILE, decltype(&std::fclose)> file(rawFile, &std::fclose);
        if (std::fseek(file.get(), 0, SEEK_END) != 0)
        {
            return false;
        }
        const long length = std::ftell(file.get());
        if (length <= 0 || static_cast<std::size_t>(length) > kMaximumModelBytes ||
            std::fseek(file.get(), 0, SEEK_SET) != 0)
        {
            return false;
        }
        contents.resize(static_cast<std::size_t>(length));
        const std::size_t bytesRead = std::fread(&contents[0], 1u, contents.size(), file.get());
        if (bytesRead != contents.size())
        {
            contents.clear();
            return false;
        }
        return true;
    }

    int HealthBucket(int health, int maximumHealth, int lowPercent, int middlePercent)
    {
        if (maximumHealth <= 0)
        {
            return 2;
        }
        const int safeHealth = (std::max)(0, (std::min)(maximumHealth, health));
        const long long scaledHealth = static_cast<long long>(safeHealth) * 100ll;
        const long long maximum = static_cast<long long>(maximumHealth);
        if (scaledHealth <= maximum * lowPercent)
        {
            return 0;
        }
        if (scaledHealth <= maximum * middlePercent)
        {
            return 1;
        }
        return 2;
    }

    // minion_count 是状态编码中的倒数第三维；反解该维用于验证训练时的动作遮罩。
    int DecodeMinionCount(std::size_t stateIndex)
    {
        stateIndex /= 3u; // rage_bucket
        stateIndex /= 8u; // player_status_mask
        return static_cast<int>(stateIndex % 4u);
    }

    struct CoTaskMemStringDeleter
    {
        void operator()(wchar_t* value) const
        {
            CoTaskMemFree(value);
        }
    };
}

bool BossAiModel::LoadFromDesktop()
{
    loaded_ = false;
    policy_.clear();
    modelPath_.clear();
    loadMessage_.clear();

    try
    {
        PWSTR rawDesktopPath = nullptr;
        const HRESULT desktopResult = SHGetKnownFolderPath(
            FOLDERID_Desktop,
            KF_FLAG_DEFAULT,
            nullptr,
            &rawDesktopPath);
        std::unique_ptr<wchar_t, CoTaskMemStringDeleter> desktopPath(rawDesktopPath);
        if (FAILED(desktopResult) || desktopPath == nullptr || desktopPath.get()[0] == L'\0')
        {
            loadMessage_ = L"无法取得桌面路径，已使用固定轮换";
            return false;
        }
        modelPath_ = desktopPath.get();
        if (!modelPath_.empty() && modelPath_.back() != L'\\')
        {
            modelPath_.push_back(L'\\');
        }
        modelPath_ += kModelFileName;

        std::string json;
        if (!ReadWholeFile(modelPath_, json))
        {
            loadMessage_ = L"桌面模型不存在或无法读取，已使用固定轮换";
            return false;
        }

        std::vector<std::uint8_t> candidate;
        if (!ParseModelDocument(json, candidate))
        {
            loadMessage_ = L"桌面模型 JSON 或状态架构错误，已使用固定轮换";
            return false;
        }

        // 即使模型元数据声明了动作遮罩，也对全部状态重新验证一次，防止被
        // 替换的策略在满三只小怪时选择没有收益且无自然超时的响指阶段。
        for (std::size_t state = 0; state < candidate.size(); ++state)
        {
            if (DecodeMinionCount(state) >= 3 && candidate[state] == 1u)
            {
                loadMessage_ = L"桌面模型违反三只小怪动作限制，已使用固定轮换";
                return false;
            }
        }

        policy_ = std::move(candidate);
        loaded_ = true;
        loadMessage_ = L"Q-learning 模型已启用";
        return true;
    }
    catch (...)
    {
        // 文件大小有上限，但字符串/vector 仍可能在极低内存下抛出异常。
        // 任何加载异常都只关闭 AI 模型，不让 Boss 初始化中止整个游戏。
        loaded_ = false;
        policy_.clear();
        try
        {
            loadMessage_ = L"模型加载发生异常，已使用固定轮换";
        }
        catch (...)
        {
        }
        return false;
    }
}

bool BossAiModel::IsLoaded() const
{
    return loaded_ && policy_.size() == StateCount;
}

int BossAiModel::SelectAction(const BossAiObservation& observation) const
{
    if (!IsLoaded())
    {
        return InvalidAction;
    }
    const std::size_t state = EncodeState(observation);
    if (state >= policy_.size())
    {
        return InvalidAction;
    }
    return static_cast<int>(policy_[state]);
}

const std::wstring& BossAiModel::GetModelPath() const
{
    return modelPath_;
}

const std::wstring& BossAiModel::GetLoadMessage() const
{
    return loadMessage_;
}

std::size_t BossAiModel::EncodeState(const BossAiObservation& observation)
{
    const int bossHealthBucket = HealthBucket(
        observation.bossHealth,
        observation.bossMaximumHealth,
        33,
        66);
    const int playerHealthBucket = HealthBucket(
        observation.playerHealth,
        observation.playerMaximumHealth,
        30,
        65);
    const int xBucket = observation.absoluteXDistance <= 210.0f
        ? 0
        : (observation.absoluteXDistance <= 360.0f ? 1 : 2);
    const int yBucket = observation.absoluteYDistance <= 44.0f
        ? 0
        : (observation.absoluteYDistance <= 62.0f ? 1 : 2);
    const int minionCount = (std::max)(0, (std::min)(3, observation.minionCount));
    int statusMask = 0;
    if (observation.directionConfused) statusMask |= 1;
    if (observation.lightningMarked) statusMask |= 2;
    if (observation.movementLocked) statusMask |= 4;
    const int rageBucket = (std::max)(0, (std::min)(2, observation.rageStacks));

    std::size_t state = static_cast<std::size_t>(bossHealthBucket);
    state = state * 3u + static_cast<std::size_t>(playerHealthBucket);
    state = state * 3u + static_cast<std::size_t>(xBucket);
    state = state * 3u + static_cast<std::size_t>(yBucket);
    state = state * 4u + static_cast<std::size_t>(minionCount);
    state = state * 8u + static_cast<std::size_t>(statusMask);
    state = state * 3u + static_cast<std::size_t>(rageBucket);
    return state;
}
