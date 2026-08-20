#include "MachineFingerprint.h"

#include <openssl/evp.h>

#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

// ============================================================
// 平台特定头文件
// ============================================================
#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <iphlpapi.h>
    #include <windows.h>
#else
    #include <unistd.h>
    #if defined(__APPLE__)
        #include <IOKit/IOKitLib.h>
        #include <sys/socket.h>
        #include <sys/stat.h>
        #include <sys/statvfs.h>
        #include <sys/mount.h>
        #include <net/if.h>
        #include <ifaddrs.h>
    #elif defined(__linux__)
        #include <sys/socket.h>
        #include <sys/stat.h>
        #include <net/if.h>
        #include <ifaddrs.h>
        #include <fstream>
    #endif
#endif

// ============================================================
// SHA256 哈希（跨平台，仅依赖 OpenSSL）
// ============================================================
static std::string Sha256Hex(const std::string& input)
{
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hashLen = 0;
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);
    EVP_DigestUpdate(ctx, input.data(), input.size());
    EVP_DigestFinal_ex(ctx, hash, &hashLen);
    EVP_MD_CTX_free(ctx);

    std::ostringstream oss;
    for (unsigned int i = 0; i < hashLen; ++i)
    {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    }
    return oss.str();
}

// ============================================================
// 机器唯一标识（平台特定）
// ============================================================

#ifdef _WIN32

static std::string GetMachineGuid()
{
    HKEY hKey;
    std::string guid;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Cryptography", 0, KEY_READ | KEY_WOW64_64KEY, &hKey) ==
        ERROR_SUCCESS)
    {
        wchar_t buffer[256];
        DWORD size = sizeof(buffer);
        if (RegQueryValueExW(hKey, L"MachineGuid", nullptr, nullptr, reinterpret_cast<LPBYTE>(buffer), &size) ==
            ERROR_SUCCESS)
        {
            int len = WideCharToMultiByte(CP_UTF8, 0, buffer, -1, nullptr, 0, nullptr, nullptr);
            if (len > 0)
            {
                guid.resize(len - 1);
                WideCharToMultiByte(CP_UTF8, 0, buffer, -1, &guid[0], len, nullptr, nullptr);
            }
        }
        RegCloseKey(hKey);
    }
    return guid;
}

#elif defined(__APPLE__)

static std::string GetMachineGuid()
{
    // macOS 12.0+ 使用 kIOMainPortDefault 替代已废弃的 kIOMasterPortDefault
    #if defined(__MAC_OS_X_VERSION_MIN_REQUIRED) && __MAC_OS_X_VERSION_MIN_REQUIRED >= 120000
    mach_port_t mainPort = kIOMainPortDefault;
    #else
    mach_port_t mainPort = kIOMasterPortDefault;
    #endif
    io_registry_entry_t ioPort = IOServiceGetMatchingService(mainPort, IOServiceMatching("IOPlatformExpertDevice"));
    if (ioPort == IO_OBJECT_NULL)
    {
        return {};
    }

    CFTypeRef serialCF = IORegistryEntryCreateCFProperty(ioPort, CFSTR(kIOPlatformUUIDKey), kCFAllocatorDefault, 0);
    IOObjectRelease(ioPort);
    if (!serialCF)
    {
        return {};
    }

    char buffer[256];
    Boolean ok = CFStringGetCString(static_cast<CFStringRef>(serialCF), buffer, sizeof(buffer), kCFStringEncodingUTF8);
    CFRelease(serialCF);
    return ok ? buffer : "";
}

#elif defined(__linux__)

static std::string GetMachineGuid()
{
    // Linux: /etc/machine-id（systemd 生成，每次安装唯一）
    std::ifstream file("/etc/machine-id");
    std::string id;
    if (std::getline(file, id) && !id.empty())
    {
        return id;
    }
    // 回退：/var/lib/dbus/machine-id
    std::ifstream file2("/var/lib/dbus/machine-id");
    if (std::getline(file2, id) && !id.empty())
    {
        return id;
    }
    return {};
}

#else
static std::string GetMachineGuid()
{
    return {};
}
#endif

// ============================================================
// 卷序列号 / 磁盘标识（平台特定）
// ============================================================

#ifdef _WIN32

static std::string GetVolumeSerial()
{
    DWORD serial = 0;
    if (GetVolumeInformationW(L"C:\\", nullptr, 0, &serial, nullptr, nullptr, nullptr, 0))
    {
        std::ostringstream oss;
        oss << std::hex << serial;
        return oss.str();
    }
    return {};
}

#elif defined(__linux__)

static std::string GetVolumeSerial()
{
    // Linux: 读取根分区的 st_dev（设备号）作为简单标识
    struct stat st;
    if (stat("/", &st) == 0)
    {
        std::ostringstream oss;
        oss << std::hex << st.st_dev;
        return oss.str();
    }
    return {};
}

#else
// macOS: 使用 volume UUID（通过 diskutil 或 statfs）
static std::string GetVolumeSerial()
{
    struct statfs buf;
    if (statfs("/", &buf) == 0)
    {
        std::ostringstream oss;
        oss << std::hex << buf.f_fsid.val[0] << buf.f_fsid.val[1];
        return oss.str();
    }
    return {};
}
#endif

// ============================================================
// MAC 地址（跨平台：getifaddrs 在 Linux/macOS 通用）
// ============================================================

#ifdef _WIN32

static std::string GetMacAddress()
{
    ULONG outBufLen = 0;
    if (GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX, nullptr, nullptr, &outBufLen) != ERROR_BUFFER_OVERFLOW)
    {
        return {};
    }

    std::vector<BYTE> buf(outBufLen);
    PIP_ADAPTER_ADDRESSES adapters = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buf.data());
    if (GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX, nullptr, adapters, &outBufLen) != NO_ERROR)
    {
        return {};
    }

    for (PIP_ADAPTER_ADDRESSES adapter = adapters; adapter; adapter = adapter->Next)
    {
        if (adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK)
        {
            continue;
        }
        if (adapter->PhysicalAddressLength == 6)
        {
            std::ostringstream oss;
            for (UINT i = 0; i < adapter->PhysicalAddressLength; ++i)
            {
                oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(adapter->PhysicalAddress[i]);
            }
            return oss.str();
        }
    }
    return {};
}

#elif defined(__APPLE__)
// macOS: getifaddrs + AF_LINK + sockaddr_dl

    #include <net/if_dl.h>

static std::string GetMacAddress()
{
    struct ifaddrs* ifaddr = nullptr;
    if (getifaddrs(&ifaddr) == -1)
    {
        return {};
    }

    std::string result;
    for (struct ifaddrs* ifa = ifaddr; ifa; ifa = ifa->ifa_next)
    {
        if (!ifa->ifa_addr)
        {
            continue;
        }
        if (ifa->ifa_addr->sa_family != AF_LINK)
        {
            continue;
        }
        if (std::string(ifa->ifa_name).find("lo") == 0)
        {
            continue;
        }

        auto* sdl = reinterpret_cast<struct sockaddr_dl*>(ifa->ifa_addr);
        if (sdl->sdl_alen == 6)
        {
            const unsigned char* mac = reinterpret_cast<const unsigned char*>(sdl->sdl_data + sdl->sdl_nlen);
            std::ostringstream oss;
            for (int i = 0; i < 6; ++i)
            {
                oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(mac[i]);
            }
            result = oss.str();
            break;
        }
    }
    freeifaddrs(ifaddr);
    return result;
}

#elif defined(__linux__)
// Linux: getifaddrs + AF_PACKET + sockaddr_ll

    #include <net/if_packet.h>
    #include <sys/ioctl.h>

static std::string GetMacAddress()
{
    struct ifaddrs* ifaddr = nullptr;
    if (getifaddrs(&ifaddr) == -1)
    {
        return {};
    }

    std::string result;
    for (struct ifaddrs* ifa = ifaddr; ifa; ifa = ifa->ifa_next)
    {
        if (!ifa->ifa_addr)
        {
            continue;
        }
        if (ifa->ifa_addr->sa_family != AF_PACKET)
        {
            continue;
        }
        if (std::string(ifa->ifa_name).find("lo") == 0)
        {
            continue;
        }

        auto* sll = reinterpret_cast<struct sockaddr_ll*>(ifa->ifa_addr);
        if (sll->sll_halen == 6)
        {
            const unsigned char* mac = sll->sll_addr;
            std::ostringstream oss;
            for (int i = 0; i < 6; ++i)
            {
                oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(mac[i]);
            }
            result = oss.str();
            break;
        }
    }
    freeifaddrs(ifaddr);
    return result;
}

#endif

// ============================================================
// 生成机器指纹
// ============================================================

std::string MachineFingerprint::Generate()
{
    std::string raw = GetMachineGuid() + "|" + GetVolumeSerial() + "|" + GetMacAddress();
    if (raw == "||")
    {
        return {};
    }
    return Sha256Hex(raw);
}