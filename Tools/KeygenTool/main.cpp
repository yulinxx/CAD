#include <iostream>
#include <string>

#include "License/LicenseKeygen.h"

void PrintUsage()
{
    std::cout << "SanYiCAD License Key Generator\n"
              << "Usage:\n"
              << "  Generate key pair:\n"
              << "    KeygenTool genkey <private.pem> <public.pem>\n\n"
              << "  Generate registration code:\n"
              << "    KeygenTool genreg <machine_code> <expiry_date> <features> <issue_date> <customer> <private.pem>\n\n"
              << "Examples:\n"
              << "  KeygenTool genkey private.pem public.pem\n"
              << "  KeygenTool genreg ABC123DEF 2028-12-31 all 2026-07-04 CustomerX private.pem\n";
}

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        PrintUsage();
        return 1;
    }

    const std::string cmd = argv[1];

    if (cmd == "genkey")
    {
        if (argc < 4)
        {
            std::cerr << "Error: genkey requires <private.pem> <public.pem>\n";
            return 1;
        }

        if (LicenseKeygen_GenerateKeyPair(argv[2], argv[3]) == LICENSE_OK)
        {
            std::cout << "Key pair generated:\n"
                      << "  Private key: " << argv[2] << "\n"
                      << "  Public key:  " << argv[3] << "\n";
            return 0;
        }

        std::cerr << "Failed to generate key pair\n";
        return 1;
    }

    if (cmd == "genreg")
    {
        if (argc < 8)
        {
            std::cerr << "Error: genreg requires <machine_code> <expiry> <features> <issue_date> <customer> <private.pem>\n";
            return 1;
        }

        char regCode[4096] = {};
        const int result = LicenseKeygen_GenerateRegCode(
            argv[2],
            argv[3],
            argv[4],
            argv[5],
            argv[6],
            argv[7],
            regCode,
            sizeof(regCode));

        if (result == LICENSE_OK)
        {
            std::cout << "Registration code:\n" << regCode << "\n";
            return 0;
        }

        std::cerr << "Failed to generate registration code (error=" << result << ")\n";
        return 1;
    }

    PrintUsage();
    return 1;
}
