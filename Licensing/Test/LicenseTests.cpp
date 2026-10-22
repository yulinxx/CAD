#include <gtest/gtest.h>

#include "License/LicenseDLL.h"
#include "License/LicenseKeygen.h"
#include "License/LicenseTestHooks.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>

namespace
{
    std::string UniqueTempDir(const char* prefix)
    {
        const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        std::ostringstream oss;
        oss << prefix << "_" << now;
        return (std::filesystem::temp_directory_path() / oss.str()).string();
    }

    std::string ReadFileText(const std::filesystem::path& path)
    {
        std::ifstream file(path);
        if (!file)
        {
            return {};
        }

        return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    }

    class LicenseApiTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            License_SetCheckEnabled(1);
            License_GuardMarkInvalid();
        }

        void TearDown() override
        {
#ifdef LICENSE_TEST_HOOKS
            LicenseTest_ClearPublicKeyPem();
            LicenseTest_ResetThrottleConfig();
#endif
        }
    };

    class LicenseFixture : public LicenseApiTest
    {
    protected:
        void SetUp() override
        {
            LicenseApiTest::SetUp();
            m_tempDir = UniqueTempDir("sanyi_license_test");
            std::filesystem::create_directories(m_tempDir);
        }

        void TearDown() override
        {
            LicenseApiTest::TearDown();
            std::error_code ec;
            std::filesystem::remove_all(m_tempDir, ec);
        }

        LicenseContext* CreateContext() const
        {
            LicenseConfig config{};
            License_ConfigInit(&config);
            std::string configDirStr = m_tempDir.string();
            config.configDir = configDirStr.c_str();
            return License_Create(&config);
        }

        bool SetupTestKeyPair()
        {
            const auto privateKeyPath = m_tempDir / "private.pem";
            const auto publicKeyPath = m_tempDir / "public.pem";

            if (LicenseKeygen_GenerateKeyPair(privateKeyPath.string().c_str(), publicKeyPath.string().c_str()) !=
                LICENSE_OK)
            {
                return false;
            }

#ifdef LICENSE_TEST_HOOKS
            const std::string publicPem = ReadFileText(publicKeyPath);
            if (publicPem.empty())
            {
                return false;
            }
            return LicenseTest_SetPublicKeyPem(publicPem.c_str()) == LICENSE_OK;
#else
            return false;
#endif
        }

        std::string GenerateRegCodeForMachine(
            const std::string& machineCode, const std::string& expiryDate, const std::string& issueDate = "2026-01-01")
        {
            const auto privateKeyPath = m_tempDir / "private.pem";
            char regCode[4096] = {};
            const int result = LicenseKeygen_GenerateRegCode(machineCode.c_str(),
                expiryDate.c_str(),
                "all",
                issueDate.c_str(),
                "TestCustomer",
                privateKeyPath.string().c_str(),
                regCode,
                sizeof(regCode));

            if (result != LICENSE_OK)
            {
                return {};
            }
            return regCode;
        }

        std::filesystem::path m_tempDir;
    };
}  // namespace

// ========== C API 基础契约 ==========

TEST_F(LicenseApiTest, GetVersion)
{
    EXPECT_EQ(License_GetVersion(), LICENSE_VERSION);
}

TEST_F(LicenseApiTest, GetVersionString)
{
    const char* version = License_GetVersionString();
    ASSERT_NE(version, nullptr);
    EXPECT_STREQ(version, "1.0.0");
}

TEST_F(LicenseApiTest, ConfigInitDefaults)
{
    LicenseConfig config{};
    License_ConfigInit(&config);
    EXPECT_EQ(config.structSize, sizeof(LicenseConfig));
    // -1 = 不覆盖运行期开关；由编译期 SANYI_ENABLE_LICENSE 决定默认值
    EXPECT_EQ(config.enableCheck, -1);
}

TEST_F(LicenseApiTest, ConfigInitDoesNotOverrideRuntimeGate)
{
    LicenseConfig config{};
    License_ConfigInit(&config);
    config.configDir = ".";

    License_SetCheckEnabled(0);
    LicenseContext* disabledCtx = License_Create(&config);
    ASSERT_NE(disabledCtx, nullptr);
    EXPECT_EQ(License_IsCheckEnabled(), 0);
    License_Destroy(disabledCtx);

    License_SetCheckEnabled(1);
    LicenseContext* enabledCtx = License_Create(&config);
    ASSERT_NE(enabledCtx, nullptr);
    EXPECT_EQ(License_IsCheckEnabled(), 1);
    License_Destroy(enabledCtx);
}

TEST_F(LicenseApiTest, CreateRejectsNullConfig)
{
    EXPECT_EQ(License_Create(nullptr), nullptr);
}

TEST_F(LicenseApiTest, CreateRejectsEmptyConfigDir)
{
    LicenseConfig config{};
    License_ConfigInit(&config);
    config.configDir = "";
    EXPECT_EQ(License_Create(&config), nullptr);
}

TEST_F(LicenseApiTest, DestroyAcceptsNull)
{
    License_Destroy(nullptr);
    SUCCEED();
}

TEST_F(LicenseApiTest, GetLastErrorMessageRejectsNullBuffer)
{
    EXPECT_EQ(License_GetLastErrorMessage(nullptr, 64), LICENSE_ERR_NULL_POINTER);
}

TEST_F(LicenseApiTest, ActivateRejectsNullContext)
{
    EXPECT_EQ(License_Activate(nullptr, "code"), LICENSE_ERR_NULL_POINTER);
}

TEST_F(LicenseApiTest, ActivateRejectsEmptyRegCode)
{
    LicenseConfig config{};
    License_ConfigInit(&config);
    config.configDir = ".";
    LicenseContext* ctx = License_Create(&config);
    ASSERT_NE(ctx, nullptr);

    EXPECT_EQ(License_Activate(ctx, ""), LICENSE_ERR_INVALID_ARG);
    EXPECT_EQ(License_Activate(ctx, nullptr), LICENSE_ERR_INVALID_ARG);

    License_Destroy(ctx);
}

TEST_F(LicenseApiTest, GetMachineCodeRejectsSmallBuffer)
{
    LicenseConfig config{};
    License_ConfigInit(&config);
    config.configDir = ".";
    LicenseContext* ctx = License_Create(&config);
    ASSERT_NE(ctx, nullptr);

    char tiny[4] = {};
    EXPECT_EQ(License_GetMachineCode(ctx, tiny, sizeof(tiny)), LICENSE_ERR_BUFFER_TOO_SMALL);

    License_Destroy(ctx);
}

// ========== Guard 散射逻辑 ==========

TEST_F(LicenseApiTest, GuardMarkValidAndCheckStartup)
{
    EXPECT_EQ(License_GuardMarkValid(), LICENSE_OK);
    EXPECT_EQ(License_GuardCheck(LICENSE_GUARD_STARTUP), LICENSE_OK);
    EXPECT_EQ(License_GuardIsQuickValid(), 1);
}

TEST_F(LicenseApiTest, GuardMarkInvalidFailsCheck)
{
    EXPECT_EQ(License_GuardMarkInvalid(), LICENSE_OK);
    EXPECT_NE(License_GuardCheck(LICENSE_GUARD_STARTUP), LICENSE_OK);
    EXPECT_EQ(License_GuardIsQuickValid(), 0);
}

TEST_F(LicenseApiTest, GuardDifferentFlavors)
{
    EXPECT_EQ(License_GuardMarkValid(), LICENSE_OK);
    EXPECT_EQ(License_GuardCheck(LICENSE_GUARD_SAVE), LICENSE_OK);
    EXPECT_EQ(License_GuardCheck(LICENSE_GUARD_EXPORT), LICENSE_OK);
    EXPECT_EQ(License_GuardCheck(LICENSE_GUARD_RENDER), LICENSE_OK);
    EXPECT_EQ(License_GuardCheck(LICENSE_GUARD_GENERIC), LICENSE_OK);
}

TEST_F(LicenseApiTest, GuardRefreshClearsState)
{
    EXPECT_EQ(License_GuardMarkValid(), LICENSE_OK);
    EXPECT_EQ(License_GuardRefresh(), LICENSE_OK);
    EXPECT_NE(License_GuardCheck(LICENSE_GUARD_GENERIC), LICENSE_OK);
}

// ========== 机器码 ==========

TEST_F(LicenseFixture, MachineCodeIsNonEmpty)
{
    LicenseContext* ctx = CreateContext();
    ASSERT_NE(ctx, nullptr);

    char machineCode[128] = {};
    EXPECT_EQ(License_GetMachineCode(ctx, machineCode, sizeof(machineCode)), LICENSE_OK);
    EXPECT_GT(std::strlen(machineCode), 0u);

    License_Destroy(ctx);
}

// ========== 校验拒绝路径 ==========

TEST_F(LicenseFixture, CheckFailsWithoutLicenseFile)
{
    LicenseContext* ctx = CreateContext();
    ASSERT_NE(ctx, nullptr);

    EXPECT_NE(License_Check(ctx), LICENSE_OK);

    License_Destroy(ctx);
}

TEST_F(LicenseFixture, ActivateRejectsInvalidFormat)
{
    LicenseContext* ctx = CreateContext();
    ASSERT_NE(ctx, nullptr);

    EXPECT_NE(License_Activate(ctx, "not-a-valid-reg-code"), LICENSE_OK);

    License_Destroy(ctx);
}

TEST_F(LicenseFixture, ActivateRejectsWrongSignature)
{
    ASSERT_TRUE(SetupTestKeyPair());

    LicenseContext* ctx = CreateContext();
    ASSERT_NE(ctx, nullptr);

#ifdef LICENSE_TEST_HOOKS
    LicenseTest_ClearPublicKeyPem();
#endif

    char machineCode[128] = {};
    ASSERT_EQ(License_GetMachineCode(ctx, machineCode, sizeof(machineCode)), LICENSE_OK);

    const std::string regCode = GenerateRegCodeForMachine(machineCode, "2099-12-31");
    ASSERT_FALSE(regCode.empty());

    EXPECT_NE(License_Activate(ctx, regCode.c_str()), LICENSE_OK);

    License_Destroy(ctx);
}

TEST_F(LicenseFixture, ActivateRejectsWrongMachine)
{
    ASSERT_TRUE(SetupTestKeyPair());

    LicenseContext* ctx = CreateContext();
    ASSERT_NE(ctx, nullptr);

    const std::string regCode = GenerateRegCodeForMachine("00000000000000000000000000000000", "2099-12-31");
    ASSERT_FALSE(regCode.empty());

    EXPECT_NE(License_Activate(ctx, regCode.c_str()), LICENSE_OK);

    LicenseInfo info{};
    info.structSize = sizeof(LicenseInfo);
    ASSERT_EQ(License_GetInfo(ctx, &info), LICENSE_OK);
    EXPECT_STRNE(info.errorMessage, "");

    License_Destroy(ctx);
}

TEST_F(LicenseFixture, ActivateRejectsExpiredLicense)
{
    ASSERT_TRUE(SetupTestKeyPair());

    LicenseContext* ctx = CreateContext();
    ASSERT_NE(ctx, nullptr);

    char machineCode[128] = {};
    ASSERT_EQ(License_GetMachineCode(ctx, machineCode, sizeof(machineCode)), LICENSE_OK);

    const std::string regCode = GenerateRegCodeForMachine(machineCode, "2000-01-01", "1999-01-01");
    ASSERT_FALSE(regCode.empty());

    EXPECT_NE(License_Activate(ctx, regCode.c_str()), LICENSE_OK);

    License_Destroy(ctx);
}

// ========== 激活闭环 ==========

TEST_F(LicenseFixture, ActivateAndCheckRoundTrip)
{
    ASSERT_TRUE(SetupTestKeyPair());

    LicenseContext* ctx = CreateContext();
    ASSERT_NE(ctx, nullptr);

    char machineCode[128] = {};
    ASSERT_EQ(License_GetMachineCode(ctx, machineCode, sizeof(machineCode)), LICENSE_OK);

    const std::string regCode = GenerateRegCodeForMachine(machineCode, "2099-12-31");
    ASSERT_FALSE(regCode.empty());

    ASSERT_EQ(License_Activate(ctx, regCode.c_str()), LICENSE_OK);
    EXPECT_EQ(License_Check(ctx), LICENSE_OK);

    LicenseInfo info{};
    info.structSize = sizeof(LicenseInfo);
    ASSERT_EQ(License_GetInfo(ctx, &info), LICENSE_OK);
    EXPECT_EQ(info.isValid, 1);
    EXPECT_STREQ(info.machineCode, machineCode);
    EXPECT_STREQ(info.expiryDate, "2099-12-31");
    EXPECT_STREQ(info.features, "all");
    EXPECT_STREQ(info.customerName, "TestCustomer");

    License_Destroy(ctx);
}

TEST_F(LicenseFixture, ClearLicenseRemovesActivation)
{
    ASSERT_TRUE(SetupTestKeyPair());

    LicenseContext* ctx = CreateContext();
    ASSERT_NE(ctx, nullptr);

    char machineCode[128] = {};
    ASSERT_EQ(License_GetMachineCode(ctx, machineCode, sizeof(machineCode)), LICENSE_OK);

    const std::string regCode = GenerateRegCodeForMachine(machineCode, "2099-12-31");
    ASSERT_EQ(License_Activate(ctx, regCode.c_str()), LICENSE_OK);
    ASSERT_EQ(License_Clear(ctx), LICENSE_OK);
    EXPECT_NE(License_Check(ctx), LICENSE_OK);

    License_Destroy(ctx);
}

// ========== 校验开关 ==========

TEST_F(LicenseFixture, CheckEnabledBypassesVerification)
{
    LicenseConfig config{};
    License_ConfigInit(&config);
    config.enableCheck = 0;
    std::string configDirStr = m_tempDir.string();
    config.configDir = configDirStr.c_str();

    LicenseContext* ctx = License_Create(&config);
    ASSERT_NE(ctx, nullptr);

    EXPECT_EQ(License_Check(ctx), LICENSE_OK);
    EXPECT_EQ(License_GuardIsQuickValid(), 1);

    License_SetCheckEnabled(1);
    License_Destroy(ctx);
}

// ========== Keygen API ==========

TEST_F(LicenseFixture, KeygenGenerateKeyPair)
{
    const auto privateKeyPath = m_tempDir / "private.pem";
    const auto publicKeyPath = m_tempDir / "public.pem";

    EXPECT_EQ(
        LicenseKeygen_GenerateKeyPair(privateKeyPath.string().c_str(), publicKeyPath.string().c_str()), LICENSE_OK);
    EXPECT_TRUE(std::filesystem::exists(privateKeyPath));
    EXPECT_TRUE(std::filesystem::exists(publicKeyPath));
}

TEST_F(LicenseFixture, KeygenSignOnly)
{
    const auto privateKeyPath = m_tempDir / "private.pem";
    const auto publicKeyPath = m_tempDir / "public.pem";
    ASSERT_EQ(
        LicenseKeygen_GenerateKeyPair(privateKeyPath.string().c_str(), publicKeyPath.string().c_str()), LICENSE_OK);

    char regCode[4096] = {};
    EXPECT_EQ(LicenseKeygen_GenerateRegCode("test_machine_code",
                  "2099-12-31",
                  "all",
                  "2026-01-01",
                  "TestCustomer",
                  privateKeyPath.string().c_str(),
                  regCode,
                  sizeof(regCode)),
        LICENSE_OK);
    EXPECT_NE(std::strlen(regCode), 0u);
}

TEST_F(LicenseFixture, KeygenGenerateRegCodeFormat)
{
    ASSERT_TRUE(SetupTestKeyPair());

    const std::string regCode = GenerateRegCodeForMachine("test_machine_code", "2099-12-31");
    ASSERT_FALSE(regCode.empty());
    EXPECT_NE(regCode.find('.'), std::string::npos);
}

// ========== 激活失败节流 ==========

#ifdef LICENSE_TEST_HOOKS

TEST_F(LicenseFixture, ActivateRateLimitedAfterFreeAttempts)
{
    // 默认策略：前 3 次失败不惩罚，第 4 次进入退避期被拒绝
    LicenseContext* ctx = CreateContext();
    ASSERT_NE(ctx, nullptr);

    for (int i = 0; i < 3; ++i)
    {
        EXPECT_EQ(License_Activate(ctx, "garbage-code"), LICENSE_ERR_VERIFY_FAILED);
    }

    EXPECT_EQ(License_Activate(ctx, "garbage-code"), LICENSE_ERR_LOCKED);

    LicenseInfo info{};
    info.structSize = sizeof(LicenseInfo);
    ASSERT_EQ(License_GetInfo(ctx, &info), LICENSE_OK);
    EXPECT_NE(std::strlen(info.errorMessage), 0u);

    License_Destroy(ctx);
}

TEST_F(LicenseFixture, ThrottleStatePersistsAcrossContexts)
{
    ASSERT_EQ(LicenseTest_SetThrottleConfig(1, 0, 0, 3, 3600), LICENSE_OK);

    LicenseContext* ctx1 = CreateContext();
    ASSERT_NE(ctx1, nullptr);
    for (int i = 0; i < 3; ++i)
    {
        EXPECT_EQ(License_Activate(ctx1, "bad-code"), LICENSE_ERR_VERIFY_FAILED);
    }
    EXPECT_EQ(License_Activate(ctx1, "bad-code"), LICENSE_ERR_LOCKED);
    License_Destroy(ctx1);

    // 重建上下文（模拟进程重启），硬锁定状态应从持久化文件恢复
    LicenseContext* ctx2 = CreateContext();
    ASSERT_NE(ctx2, nullptr);
    EXPECT_EQ(License_Activate(ctx2, "bad-code"), LICENSE_ERR_LOCKED);
    License_Destroy(ctx2);
}

TEST_F(LicenseFixture, HardLockExpiryStartsNewAttemptCycle)
{
    // 锁定过期后计数从零开始：每个锁定周期恰好允许 hardLockFailures 次尝试
    // 锁定秒数取 2 并留足余量，规避 time() 秒级截断导致的过期竞态
    ASSERT_EQ(LicenseTest_SetThrottleConfig(0, 0, 0, 2, 2), LICENSE_OK);

    LicenseContext* ctx = CreateContext();
    ASSERT_NE(ctx, nullptr);

    EXPECT_EQ(License_Activate(ctx, "bad-code"), LICENSE_ERR_VERIFY_FAILED);
    EXPECT_EQ(License_Activate(ctx, "bad-code"), LICENSE_ERR_VERIFY_FAILED);
    EXPECT_EQ(License_Activate(ctx, "bad-code"), LICENSE_ERR_LOCKED);

    std::this_thread::sleep_for(std::chrono::milliseconds(2500));

    // 新一轮周期：再次允许 2 次失败尝试，第 3 次重新触发锁定
    EXPECT_EQ(License_Activate(ctx, "bad-code"), LICENSE_ERR_VERIFY_FAILED);
    EXPECT_EQ(License_Activate(ctx, "bad-code"), LICENSE_ERR_VERIFY_FAILED);
    EXPECT_EQ(License_Activate(ctx, "bad-code"), LICENSE_ERR_LOCKED);

    License_Destroy(ctx);
}

TEST_F(LicenseFixture, HardLockExpiresAndSuccessResetsCount)
{
    ASSERT_TRUE(SetupTestKeyPair());
    ASSERT_EQ(LicenseTest_SetThrottleConfig(0, 0, 0, 3, 2), LICENSE_OK);

    LicenseContext* ctx = CreateContext();
    ASSERT_NE(ctx, nullptr);

    char machineCode[128] = {};
    ASSERT_EQ(License_GetMachineCode(ctx, machineCode, sizeof(machineCode)), LICENSE_OK);
    const std::string validCode = GenerateRegCodeForMachine(machineCode, "2099-12-31");
    ASSERT_FALSE(validCode.empty());

    for (int i = 0; i < 3; ++i)
    {
        EXPECT_EQ(License_Activate(ctx, "bad-code"), LICENSE_ERR_VERIFY_FAILED);
    }
    EXPECT_EQ(License_Activate(ctx, "bad-code"), LICENSE_ERR_LOCKED);

    // 等待硬锁定过期后，有效注册码应能正常激活
    std::this_thread::sleep_for(std::chrono::milliseconds(2500));
    EXPECT_EQ(License_Activate(ctx, validCode.c_str()), LICENSE_OK);

    // 成功后计数清零：再次连续失败 3 次才触发硬锁定
    for (int i = 0; i < 3; ++i)
    {
        EXPECT_EQ(License_Activate(ctx, "bad-code"), LICENSE_ERR_VERIFY_FAILED);
    }
    EXPECT_EQ(License_Activate(ctx, "bad-code"), LICENSE_ERR_LOCKED);

    License_Destroy(ctx);
}

#endif  // LICENSE_TEST_HOOKS