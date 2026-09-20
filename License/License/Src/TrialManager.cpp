#include "License/TrialManager.h"
#include "MachineFingerprint.h"

#include <openssl/evp.h>

#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

// Windows 注册表
#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
    #include <shlobj.h>
#endif

namespace
{
    // 配置目录（需要从外部注入）
    std::string s_configDir;

    // 设置配置目录
    void setConfigDir(const std::string& dir)
    {
        s_configDir = dir;
    }

    // 获取配置目录
    std::string getConfigDir()
    {
        if (!s_configDir.empty())
        {
            return s_configDir;
        }

    #ifdef _WIN32
        wchar_t* appData = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appData)))
        {
            std::wstring path(appData);
            CoTaskMemFree(appData);
            // 转换为 UTF-8
            int len = WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, nullptr, 0, nullptr, nullptr);
            std::string result;
            result.resize(len - 1);
            WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, &result[0], len, nullptr, nullptr);
            result += "\\SanYiCAD";
            return result;
        }
    #elif defined(__APPLE__)
        const char* home = getenv("HOME");
        if (home)
        {
            return std::string(home) + "/Library/Application Support/SanYiCAD";
        }
    #else  // Linux
        const char* home = getenv("HOME");
        if (home)
        {
            return std::string(home) + "/.config/sanyicad";
        }
    #endif
        return ".";
    }

    // SHA1 hash (for storage only, not for security)
    std::string sha1Hex(const std::string& input)
    {
        unsigned char hash[20];
        EVP_MD_CTX* ctx = EVP_MD_CTX_new();
        EVP_DigestInit_ex(ctx, EVP_sha1(), nullptr);
        EVP_DigestUpdate(ctx, input.data(), input.size());
        EVP_DigestFinal_ex(ctx, hash, nullptr);
        EVP_MD_CTX_free(ctx);

        std::ostringstream oss;
        for (int i = 0; i < 20; ++i)
        {
            oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
        }
        return oss.str();
    }
}  // anonymous namespace

// ============================================================
// 公开接口实现
// ============================================================

#ifdef __cplusplus
extern "C"
{
#endif

    void Trial_SetConfigDir(const char* dir)
    {
        if (dir)
        {
            setConfigDir(dir);
        }
    }

    int Trial_Check(int* remainingDays)
    {
        try
        {
            static bool checked = false;
            static int cachedResult = 0;
            static int cachedDays = 0;

            if (!checked)
            {
                TrialManager& mgr = TrialManager::instance();
                TrialManager::Status status = mgr.check();
                checked = true;

                // 只有明确过期的才返回 -1，其他情况（包括出错）都允许继续
                if (status == TrialManager::Status::Expired)
                {
                    cachedResult = -1;
                }
                else
                {
                    cachedResult = 0;  // Active 或其他状态都允许继续
                }
                cachedDays = mgr.remainingDays();
            }

            if (remainingDays)
            {
                *remainingDays = cachedDays;
            }

            return cachedResult;
        }
        catch (...)
        {
            // 任何异常都允许继续启动，避免阻塞用户
            if (remainingDays)
            {
                *remainingDays = 0;
            }
            return 0;
        }
    }

    void Trial_Reset()
    {
        // 删除试用文件
        std::error_code ec;
        std::filesystem::path trialFile = std::filesystem::path(getConfigDir()) / "trial.info";
        std::filesystem::remove(trialFile, ec);

    #ifdef _WIN32
        // 删除注册表
        RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\SanYiCAD\\Trial");
    #endif
    }

#ifdef __cplusplus
}
#endif

// ============================================================
// C++ 类实现
// ============================================================

TrialManager& TrialManager::instance()
{
    static TrialManager mgr;
    return mgr;
}

TrialManager::Status TrialManager::check()
{
    if (!m_initialized)
    {
        init();
        m_initialized = true;
    }
    return m_status;
}

int TrialManager::remainingDays() const
{
    if (m_status != Status::Active)
    {
        return -1;
    }

    const std::string today = getCurrentDate();
    const int used = daysBetween(m_startDate, today);
    const int remaining = m_trialDays - used;
    return remaining > 0 ? remaining : 0;
}

bool TrialManager::isActive() const
{
    return m_status == Status::Active;
}

std::string TrialManager::startDate() const
{
    return m_startDate;
}

void TrialManager::init()
{
    const std::string configDir = getConfigDir();
    const std::filesystem::path trialFile = std::filesystem::path(configDir) / "trial.info";

    // 尝试加载现有记录
    bool loaded = false;

    // 先尝试文件
    if (std::filesystem::exists(trialFile))
    {
        loaded = loadFromFile(configDir);
    }

    // Windows: 再检查注册表
    #ifdef _WIN32
    if (!loaded)
    {
        loaded = loadFromRegistry();
    }
    #endif

    if (loaded)
    {
        // 验证有效性
        if (validate())
        {
            const int remaining = remainingDays();
            if (remaining <= 0)
            {
                m_status = Status::Expired;
            }
            else
            {
                m_status = Status::Active;
            }
            return;
        }
    }

    // 首次启动或验证失败，创建新记录
    m_startDate = getCurrentDate();
    m_machineHash = sha1Hex(MachineFingerprint::Generate());
    m_trialDays = kDefaultTrialDays;

    // 确保目录存在
    std::error_code ec;
    std::filesystem::create_directories(trialFile.parent_path(), ec);

    // 保存
    saveToFile(configDir);

    #ifdef _WIN32
    saveToRegistry();
    #endif

    m_status = Status::Active;
}

bool TrialManager::validate() const
{
    // 检查日期格式
    if (m_startDate.size() != 10 || m_startDate[4] != '-' || m_startDate[7] != '-')
    {
        return false;
    }

    // 检查机器指纹匹配
    if (!checkMachineMatch())
    {
        return false;
    }

    return true;
}

bool TrialManager::checkMachineMatch() const
{
    const std::string currentHash = sha1Hex(MachineFingerprint::Generate());
    return currentHash == m_machineHash;
}

bool TrialManager::saveToFile(const std::string& configDir) const
{
    const std::filesystem::path path = std::filesystem::path(configDir) / "trial.info";
    std::ofstream file(path, std::ios::out | std::ios::trunc);
    if (!file)
    {
        return false;
    }

    file << "start_date=" << m_startDate << "\n";
    file << "machine_hash=" << m_machineHash << "\n";
    file << "trial_days=" << m_trialDays << "\n";

    return file.good();
}

bool TrialManager::loadFromFile(const std::string& configDir)
{
    const std::filesystem::path path = std::filesystem::path(configDir) / "trial.info";
    std::ifstream file(path);
    if (!file)
    {
        return false;
    }

    std::string line;
    while (std::getline(file, line))
    {
        const auto eqPos = line.find('=');
        if (eqPos == std::string::npos)
        {
            continue;
        }

        const std::string key = line.substr(0, eqPos);
        const std::string value = line.substr(eqPos + 1);

        if (key == "start_date")
        {
            m_startDate = value;
        }
        else if (key == "machine_hash")
        {
            m_machineHash = value;
        }
        else if (key == "trial_days")
        {
            m_trialDays = std::stoi(value);
        }
    }

    return !m_startDate.empty() && !m_machineHash.empty();
}

#ifdef _WIN32

bool TrialManager::saveToRegistry() const
{
    HKEY hKey;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\SanYiCAD\\Trial",
        0, nullptr, 0, KEY_WRITE, nullptr, &hKey, nullptr) != ERROR_SUCCESS)
    {
        return false;
    }

    // start_date
    std::wstring date = L"";
    for (char c : m_startDate)
    {
        date += wchar_t(c);
    }
    RegSetValueExW(hKey, L"start_date", 0, REG_SZ,
        reinterpret_cast<const BYTE*>(date.c_str()),
        static_cast<DWORD>((date.size() + 1) * sizeof(wchar_t)));

    // machine_hash
    std::wstring hash = L"";
    for (char c : m_machineHash)
    {
        hash += wchar_t(c);
    }
    RegSetValueExW(hKey, L"machine_hash", 0, REG_SZ,
        reinterpret_cast<const BYTE*>(hash.c_str()),
        static_cast<DWORD>((hash.size() + 1) * sizeof(wchar_t)));

    // trial_days
    DWORD days = static_cast<DWORD>(m_trialDays);
    RegSetValueExW(hKey, L"trial_days", 0, REG_DWORD,
        reinterpret_cast<const BYTE*>(&days), sizeof(days));

    RegCloseKey(hKey);
    return true;
}

bool TrialManager::loadFromRegistry()
{
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\SanYiCAD\\Trial",
        0, KEY_READ, &hKey) != ERROR_SUCCESS)
    {
        return false;
    }

    // 读取 start_date
    wchar_t dateBuf[64] = {};
    DWORD dateSize = sizeof(dateBuf);
    if (RegQueryValueExW(hKey, L"start_date", nullptr, nullptr,
        reinterpret_cast<LPBYTE>(dateBuf), &dateSize) == ERROR_SUCCESS)
    {
        char dateBufA[64] = {};
        WideCharToMultiByte(CP_UTF8, 0, dateBuf, -1, dateBufA, sizeof(dateBufA), nullptr, nullptr);
        m_startDate = dateBufA;
    }

    // 读取 machine_hash
    wchar_t hashBuf[128] = {};
    DWORD hashSize = sizeof(hashBuf);
    if (RegQueryValueExW(hKey, L"machine_hash", nullptr, nullptr,
        reinterpret_cast<LPBYTE>(hashBuf), &hashSize) == ERROR_SUCCESS)
    {
        char hashBufA[128] = {};
        WideCharToMultiByte(CP_UTF8, 0, hashBuf, -1, hashBufA, sizeof(hashBufA), nullptr, nullptr);
        m_machineHash = hashBufA;
    }

    // 读取 trial_days
    DWORD days = 0;
    DWORD daysSize = sizeof(days);
    if (RegQueryValueExW(hKey, L"trial_days", nullptr, nullptr,
        reinterpret_cast<LPBYTE>(&days), &daysSize) == ERROR_SUCCESS)
    {
        m_trialDays = static_cast<int>(days);
    }

    RegCloseKey(hKey);
    return !m_startDate.empty() && !m_machineHash.empty();
}

#endif  // _WIN32

// static
std::string TrialManager::getCurrentDate()
{
    time_t now = time(nullptr);
    struct tm tmInfo{};
#ifdef _WIN32
    localtime_s(&tmInfo, &now);
#else
    localtime_r(&now, &tmInfo);
#endif

    char buf[11] = {};
    strftime(buf, sizeof(buf), "%Y-%m-%d", &tmInfo);
    return buf;
}

// static
int TrialManager::daysBetween(const std::string& start, const std::string& end)
{
    if (start.empty() || end.empty())
    {
        return 0;
    }

    // 简单实现：按日历日计算
    // 解析 YYYY-MM-DD
    int sy = std::stoi(start.substr(0, 4));
    int sm = std::stoi(start.substr(5, 2));
    int sd = std::stoi(start.substr(8, 2));

    int ey = std::stoi(end.substr(0, 4));
    int em = std::stoi(end.substr(5, 2));
    int ed = std::stoi(end.substr(8, 2));

    // 转换为一年中的第几天
    int startDay = sy * 365 + sy / 4 - sy / 100 + sy / 400
                 + (sm > 1 ? (sm - 1) * 31 : 0)
                 + sd;

    int endDay = ey * 365 + ey / 4 - ey / 100 + ey / 400
               + (em > 1 ? (em - 1) * 31 : 0)
               + ed;

    return endDay - startDay;
}
