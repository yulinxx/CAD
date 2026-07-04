#include "KeygenTool.h"

#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/err.h>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <vector>

namespace
{

std::string Base64Encode(const unsigned char* data, size_t len)
{
    int encodedLen = static_cast<int>(EVP_EncodeBlock(nullptr, data, static_cast<int>(len)));
    std::string result(encodedLen, '\0');
    EVP_EncodeBlock(reinterpret_cast<unsigned char*>(&result[0]), data, static_cast<int>(len));
    return result;
}

std::string ToUrlSafe(const std::string& b64)
{
    std::string s = b64;
    while (!s.empty() && s.back() == '=')
        s.pop_back();
    std::replace(s.begin(), s.end(), '+', '-');
    std::replace(s.begin(), s.end(), '/', '_');
    return s;
}

} // anonymous namespace

bool KeygenTool::GenerateKeyPair(const std::string& privKeyFile, const std::string& pubKeyFile)
{
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
    if (!ctx)
        return false;

    if (EVP_PKEY_keygen_init(ctx) <= 0)
    {
        EVP_PKEY_CTX_free(ctx);
        return false;
    }

    if (EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, 2048) <= 0)
    {
        EVP_PKEY_CTX_free(ctx);
        return false;
    }

    EVP_PKEY* pkey = nullptr;
    if (EVP_PKEY_keygen(ctx, &pkey) <= 0)
    {
        EVP_PKEY_CTX_free(ctx);
        return false;
    }
    EVP_PKEY_CTX_free(ctx);

    bool ok = true;

    BIO* bioPriv = BIO_new_file(privKeyFile.c_str(), "w");
    if (!bioPriv || PEM_write_bio_PrivateKey(bioPriv, pkey, nullptr, nullptr, 0, nullptr, nullptr) <= 0)
        ok = false;
    BIO_free(bioPriv);

    BIO* bioPub = BIO_new_file(pubKeyFile.c_str(), "w");
    if (!bioPub || PEM_write_bio_PUBKEY(bioPub, pkey) <= 0)
        ok = false;
    BIO_free(bioPub);

    EVP_PKEY_free(pkey);
    return ok;
}

std::string KeygenTool::GenerateRegCode(const std::string& machineCode,
                                        const std::string& expiryDate,
                                        const std::string& features,
                                        const std::string& issueDate,
                                        const std::string& customerName,
                                        const std::string& privKeyFile)
{
    std::string payload = machineCode + "|" + expiryDate + "|" + features + "|" + issueDate + "|" + customerName;

    BIO* bio = BIO_new_file(privKeyFile.c_str(), "r");
    if (!bio)
        return {};

    EVP_PKEY* pkey = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);

    if (!pkey)
        return {};

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    int ok = EVP_DigestSignInit(ctx, nullptr, EVP_sha256(), nullptr, pkey);
    if (ok == 1)
        ok = EVP_DigestSignUpdate(ctx, payload.data(), payload.size());

    size_t sigLen = 0;
    if (ok == 1)
        ok = EVP_DigestSignFinal(ctx, nullptr, &sigLen);

    std::vector<unsigned char> sig(sigLen);
    if (ok == 1)
        ok = EVP_DigestSignFinal(ctx, sig.data(), &sigLen);

    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(pkey);

    if (ok <= 0)
        return {};

    std::string payloadB64 = Base64Encode(reinterpret_cast<const unsigned char*>(payload.data()), payload.size());
    std::string sigB64 = Base64Encode(sig.data(), sigLen);

    return ToUrlSafe(payloadB64) + "." + ToUrlSafe(sigB64);
}
