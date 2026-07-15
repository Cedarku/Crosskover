#pragma once

class HardcoverCredentialStore;

namespace HardcoverJsonIO {
bool save(const HardcoverCredentialStore& store, const char* path);
bool load(HardcoverCredentialStore& store, const char* json);
}  // namespace HardcoverJsonIO
