/*  LOOT

    A load order optimisation tool for
    Morrowind, Oblivion, Skyrim, Skyrim Special Edition, Skyrim VR,
    Fallout 3, Fallout: New Vegas, Fallout 4 and Fallout 4 VR.

    Copyright (C) 2014 WrinklyNinja

    This file is part of LOOT.

    LOOT is free software: you can redistribute
    it and/or modify it under the terms of the GNU General Public License
    as published by the Free Software Foundation, either version 3 of
    the License, or (at your option) any later version.

    LOOT is distributed in the hope that it will
    be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with LOOT.  If not, see
    <https://www.gnu.org/licenses/>.
    */

#include "gui/helpers.h"

#ifdef _WIN32
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <shlobj.h>
#include <shlwapi.h>
#include <windows.h>
#else
#include <mntent.h>
#include <unicode/uchar.h>
#include <unicode/unistr.h>

#include <cstdio>

using icu::UnicodeString;
#endif

#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>
#include <boost/locale.hpp>
#include <fstream>

#include "gui/state/logging.h"

namespace loot {
void OpenInDefaultApplication(const std::filesystem::path& file) {
#ifdef _WIN32
  HINSTANCE ret =
      ShellExecute(0, NULL, file.wstring().c_str(), NULL, NULL, SW_SHOWNORMAL);
  if (reinterpret_cast<uintptr_t>(ret) <= 32)
    throw std::system_error(GetLastError(),
                            std::system_category(),
                            "Failed to open file in its default application.");
#else
  if (system(("/usr/bin/xdg-open " + file.u8string()).c_str()) != 0)
    throw std::system_error(errno,
                            std::system_category(),
                            "Failed to open file in its default application.");
#endif
}

#ifdef _WIN32
std::wstring ToWinWide(const std::string& str) {
  size_t len = MultiByteToWideChar(
      CP_UTF8, 0, str.c_str(), static_cast<int>(str.length()), 0, 0);
  std::wstring wstr(len, 0);
  MultiByteToWideChar(CP_UTF8,
                      0,
                      str.c_str(),
                      static_cast<int>(str.length()),
                      &wstr[0],
                      static_cast<int>(len));
  return wstr;
}

std::string FromWinWide(const std::wstring& wstr) {
  size_t len = WideCharToMultiByte(CP_UTF8,
                                   0,
                                   wstr.c_str(),
                                   static_cast<int>(wstr.length()),
                                   NULL,
                                   0,
                                   NULL,
                                   NULL);
  std::string str(len, 0);
  WideCharToMultiByte(CP_UTF8,
                      0,
                      wstr.c_str(),
                      static_cast<int>(wstr.length()),
                      &str[0],
                      static_cast<int>(len),
                      NULL,
                      NULL);
  return str;
}

HKEY GetRegistryRootKey(const std::string& rootKey) {
  if (rootKey == "HKEY_CLASSES_ROOT")
    return HKEY_CLASSES_ROOT;
  else if (rootKey == "HKEY_CURRENT_CONFIG")
    return HKEY_CURRENT_CONFIG;
  else if (rootKey == "HKEY_CURRENT_USER")
    return HKEY_CURRENT_USER;
  else if (rootKey == "HKEY_LOCAL_MACHINE")
    return HKEY_LOCAL_MACHINE;
  else if (rootKey == "HKEY_USERS")
    return HKEY_USERS;
  else
    throw std::invalid_argument("Invalid registry key given.");
}

std::string RegKeyStringValue(const std::string& rootKey,
                              const std::string& subkey,
                              const std::string& value) {
  HKEY hKey = GetRegistryRootKey(rootKey);
  DWORD len = MAX_PATH;
  std::wstring wstr(MAX_PATH, 0);

  auto logger = getLogger();
  if (logger) {
    logger->trace(
        "Getting string for registry key, subkey and value: {}, {}, "
        "{}",
        rootKey,
        subkey,
        value);
  }

  LONG ret = RegGetValue(hKey,
                         ToWinWide(subkey).c_str(),
                         ToWinWide(value).c_str(),
                         RRF_RT_REG_SZ | RRF_SUBKEY_WOW6432KEY,
                         NULL,
                         &wstr[0],
                         &len);

  if (ret != ERROR_SUCCESS) {
    // Try again using the native registry view. On 32-bit Windows
    // this just does the same thing again. I don't think it's worth
    // trying to skip for the few 32-bit Windows users that remain.
    logger->info(
        "Failed to get string value from 32-bit Registry view, trying 64-bit "
        "Registry view.");
    ret = RegGetValue(hKey,
                      ToWinWide(subkey).c_str(),
                      ToWinWide(value).c_str(),
                      RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY,
                      NULL,
                      &wstr[0],
                      &len);
  }

  if (ret == ERROR_SUCCESS) {
    // Passing c_str() cuts off any unused buffer.
    std::string stringValue = FromWinWide(wstr.c_str());
    if (logger) {
      logger->info("Found string: {}", stringValue);
    }
    return stringValue;
  } else {
    if (logger) {
      logger->info("Failed to get string value.");
    }
    return "";
  }
}

std::vector<std::string> GetRegistrySubKeys(const std::string& rootKey,
                                            const std::string& subKey) {
  const auto logger = getLogger();
  if (logger) {
    logger->trace(
        "Getting subkey names for registry key and subkey: {}, {}",
        rootKey,
        subKey);
  }

  HKEY hKey;
  auto status = RegOpenKeyEx(GetRegistryRootKey(rootKey),
                             ToWinWide(subKey).c_str(),
                             0,
                             KEY_ENUMERATE_SUB_KEYS,
                             &hKey);

  if (status != ERROR_SUCCESS) {
    if (logger) {
      // system_error gets the details from Windows.
      const auto error =
          std::system_error(GetLastError(), std::system_category());

      logger->warn("Failed to open the Registry key \"{}\": {}",
                   rootKey + "\\" + subKey,
                   error.what());
    }

    // Don't throw because failure could be because the key simply does not
    // exist, which is an unexceptional failure state.
    return {};
  }

  std::vector<std::string> subKeyNames;
  DWORD subKeyIndex = 0;
  DWORD len = MAX_PATH;
  std::wstring subKeyName(MAX_PATH, 0);
  status = ERROR_SUCCESS;

  while (status == ERROR_SUCCESS) {
    status = RegEnumKeyEx(hKey,
                          subKeyIndex,
                          &subKeyName[0],
                          &len,
                          nullptr,
                          nullptr,
                          nullptr,
                          nullptr);

    if (status != ERROR_SUCCESS && status != ERROR_NO_MORE_ITEMS) {
      RegCloseKey(hKey);

      throw std::system_error(
          GetLastError(),
          std::system_category(),
          "Failed to get the subkeys of the Registry key: " + rootKey + "\\" +
              subKey);
    }

    subKeyNames.push_back(FromWinWide(subKeyName.c_str()));

    subKeyIndex += 1;
  }

  RegCloseKey(hKey);

  return subKeyNames;
}
#endif

std::vector<std::filesystem::path> GetDriveRootPaths() {
#ifdef _WIN32
  const auto maxBufferLength = GetLogicalDriveStrings(0, nullptr);

  if (maxBufferLength == 0) {
    throw std::system_error(
        GetLastError(),
        std::system_category(),
        "Failed to length of the buffer needed to hold all drive root paths");
  }

  // Add space for the terminating null character.
  std::vector<wchar_t> buffer(size_t{maxBufferLength} + 1);

  const size_t stringsLength =
      GetLogicalDriveStrings(static_cast<DWORD>(buffer.size()), buffer.data());

  // Trim any unused buffer bytes.
  buffer.resize(stringsLength);

  std::vector<std::filesystem::path> paths;

  auto stringStartIt = buffer.begin();
  for (auto it = buffer.begin(); it != buffer.end(); ++it) {
    if (*it == 0) {
      const std::wstring drive(stringStartIt, it);
      paths.push_back(std::filesystem::path(drive));

      stringStartIt = std::next(it);
    }
  }

  return paths;
#else
  FILE* mountsFile = setmntent("/proc/self/mounts", "r");

  if (mountsFile == nullptr) {
    throw std::runtime_error("Failed to open /proc/self/mounts");
  }

  // Java increased their buffer size to 4 KiB:
  // <https://bugs.openjdk.java.net/browse/JDK-8229872>
  // .NET uses 8KiB:
  // <https://github.com/dotnet/runtime/blob/7414af2a5f6d8d99efc27d3f5ef7a394e0b23c42/src/native/libs/System.Native/pal_mount.c#L24>
  static constexpr size_t BUFFER_SIZE = 8192;
  struct mntent entry {};
  std::array<char, BUFFER_SIZE> stringsBuffer{};
  std::vector<std::filesystem::path> paths;

  while (getmntent_r(
             mountsFile, &entry, stringsBuffer.data(), stringsBuffer.size()) !=
         nullptr) {
    paths.push_back(entry.mnt_dir);
  }

  endmntent(mountsFile);

  return paths;
#endif
}

std::optional<std::filesystem::path> FindXboxGamingRootPath(
    const std::filesystem::path& driveRootPath) {
  const auto logger = getLogger();
  const auto gamingRootFilePath = driveRootPath / ".GamingRoot";

  std::vector<char> bytes;

  try {
    if (!std::filesystem::is_regular_file(gamingRootFilePath)) {
      return std::nullopt;
    }

    std::ifstream in(gamingRootFilePath, std::ios::binary);

    std::copy(std::istreambuf_iterator<char>(in),
              std::istreambuf_iterator<char>(),
              std::back_inserter(bytes));
  } catch (const std::exception& e) {
    if (logger) {
      logger->error("Failed to read file at {}: {}",
                    gamingRootFilePath.u8string(),
                    e.what());
    }

    // Don't propagate this error as it could be due to a legitimate failure
    // case like the drive not being ready (e.g. a removable disk drive with
    // nothing in it).
    return std::nullopt;
  }

  if (logger) {
    // Log the contents of .GamingRoot because I'm not sure of the format and
    // this would help debugging.
    std::vector<std::string> hexBytes;
    std::transform(bytes.begin(),
                   bytes.end(),
                   std::back_inserter(hexBytes),
                   [](char byte) {
                     std::stringstream stream;
                     stream << "0x" << std::hex << int{byte};
                     return stream.str();
                   });

    logger->debug("Read the following bytes from {}: {}",
                  gamingRootFilePath.u8string(),
                  boost::join(hexBytes, " "));
  }

  // The content of .GamingRoot is the byte sequence 52 47 42 58 01 00 00 00
  // followed by the null-terminated UTF-16LE location of the Xbox games folder
  // on the same drive.

  if (bytes.size() % 2 != 0) {
    logger->error(
        "Found a non-even number of bytes in the file at {}, cannot interpret "
        "it as UTF-16LE",
        gamingRootFilePath.u8string());
    throw std::runtime_error(
        "Found a non-even number of bytes in the file at \"" +
        gamingRootFilePath.u8string() + "\"");
  }

  std::vector<char16_t> content;
  for (size_t i = 0; i < bytes.size(); i += 2) {
    // char16_t is little-endian on all platforms LOOT runs on.
    char16_t highByte = bytes.at(i);
    char16_t lowByte = bytes.at(i + 1);
    char16_t value = highByte | (lowByte << CHAR_BIT);
    content.push_back(value);
  }

  static constexpr size_t CHAR16_PATH_OFFSET = 4;
  if (content.size() < CHAR16_PATH_OFFSET + 1) {
    if (logger) {
      logger->error(
          ".GamingRoot content was unexpectedly short at {} char16_t long",
          content.size());
    }

    throw std::runtime_error("The file at \"" + gamingRootFilePath.u8string() +
                             "\" is shorter than expected.");
  }

  // Cut off the null char16_t at the end.
  const std::u16string relativePath(content.begin() + CHAR16_PATH_OFFSET,
                                    content.end() - 1);

  if (logger) {
    logger->debug("Read the following relative path from .GamingRoot: {}",
                  std::filesystem::path(relativePath).u8string());
  }

  return driveRootPath / relativePath;
}

int CompareFilenames(const std::string& lhs, const std::string& rhs) {
#ifdef _WIN32
  // On Windows, use CompareStringOrdinal as that will perform case conversion
  // using the operating system uppercase table information, which (I think)
  // will give results that match the filesystem, and is not locale-dependent.
  int result = CompareStringOrdinal(
      ToWinWide(lhs).c_str(), -1, ToWinWide(rhs).c_str(), -1, true);
  switch (result) {
    case CSTR_LESS_THAN:
      return -1;
    case CSTR_EQUAL:
      return 0;
    case CSTR_GREATER_THAN:
      return 1;
    default:
      throw std::invalid_argument(
          "One of the filenames to compare was invalid.");
  }
#else
  auto unicodeLhs = UnicodeString::fromUTF8(lhs);
  auto unicodeRhs = UnicodeString::fromUTF8(rhs);
  return unicodeLhs.caseCompare(unicodeRhs, U_FOLD_CASE_DEFAULT);
#endif
}

std::filesystem::path getExecutableDirectory() {
#ifdef _WIN32
  // Despite its name, paths can be longer than MAX_PATH, just not by default.
  // FIXME: Make this work with long paths.
  std::wstring executablePathString(MAX_PATH, 0);

  if (GetModuleFileName(NULL, &executablePathString[0], MAX_PATH) == 0) {
    auto logger = getLogger();
    if (logger) {
      logger->error("Failed to get LOOT executable path.");
    }
    throw std::system_error(GetLastError(),
                            std::system_category(),
                            "Failed to get LOOT executable path.");
  }

  return std::filesystem::path(executablePathString).parent_path();
#else
  std::array<char, PATH_MAX> result{};

  ssize_t count = readlink("/proc/self/exe", result.data(), result.size());
  if (count < 0) {
    auto logger = getLogger();
    if (logger) {
      logger->error("Failed to get LOOT executable path.");
    }
    throw std::system_error(
        count, std::system_category(), "Failed to get LOOT executable path.");
  }

  return std::filesystem::u8path(std::string(result.data(), count))
      .parent_path();
#endif
}

std::filesystem::path getLocalAppDataPath() {
#ifdef _WIN32
  PWSTR path;

  if (SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, NULL, &path) != S_OK)
    throw std::system_error(GetLastError(),
                            std::system_category(),
                            "Failed to get %LOCALAPPDATA% path.");

  std::filesystem::path localAppDataPath(path);
  CoTaskMemFree(path);

  return localAppDataPath;
#else
  // Use XDG_CONFIG_HOME environmental variable if it's available.
  const char* xdgConfigHome = getenv("XDG_CONFIG_HOME");

  if (xdgConfigHome != nullptr)
    return std::filesystem::u8path(xdgConfigHome);

  // Otherwise, use the HOME env. var. if it's available.
  xdgConfigHome = getenv("HOME");

  if (xdgConfigHome != nullptr)
    return std::filesystem::u8path(xdgConfigHome) / ".config";

  // If somehow both are missing, use the executable's directory.
  return getExecutableDirectory();
#endif
}

MessageType mapMessageType(const std::string& type) {
  if (type == "say") {
    return MessageType::say;
  } else if (type == "warn") {
    return MessageType::warn;
  } else {
    return MessageType::error;
  }
}

void CopyToClipboard(const std::string& text) {
#ifdef _WIN32
  if (!OpenClipboard(NULL)) {
    throw std::system_error(GetLastError(),
                            std::system_category(),
                            "Failed to open the Windows clipboard.");
  }

  if (!EmptyClipboard()) {
    throw std::system_error(GetLastError(),
                            std::system_category(),
                            "Failed to empty the Windows clipboard.");
  }

  // The clipboard takes a Unicode (ie. UTF-16) string that it then owns and
  // must not be destroyed by LOOT. Convert the string, then copy it into a
  // new block of memory for the clipboard.
  std::wstring wtext = ToWinWide(text);
  size_t wcstrLength = wtext.length() + 1;
  wchar_t* wcstr = new wchar_t[wcstrLength];
  wcscpy_s(wcstr, wcstrLength, wtext.c_str());

  if (SetClipboardData(CF_UNICODETEXT, wcstr) == NULL) {
    throw std::system_error(
        GetLastError(),
        std::system_category(),
        "Failed to copy metadata to the Windows clipboard.");
  }

  if (!CloseClipboard()) {
    throw std::system_error(GetLastError(),
                            std::system_category(),
                            "Failed to close the Windows clipboard.");
  }
#else
  std::string copyCommand = "echo '" + text + "' | xclip -selection clipboard";
  int returnCode = system(copyCommand.c_str());

  if (returnCode != 0) {
    throw std::system_error(
        returnCode,
        std::system_category(),
        "Failed to run clipboard copy command: " + copyCommand);
  }
#endif
}

std::string crcToString(uint32_t crc) {
  return (boost::format("%08X") % crc).str();
}

std::string messagesAsMarkdown(const std::vector<SimpleMessage>& messages) {
  if (messages.empty()) {
    return "";
  }

  std::string content = "## Messages\n\n";

  for (const auto& message : messages) {
    content += "- ";

    if (message.type == MessageType::warn) {
      content += "Warning: ";
    } else if (message.type == MessageType::error) {
      content += "Error: ";
    } else {
      content += "Note: ";
    }

    content += message.text + "\n";
  }

  return content;
}
}
