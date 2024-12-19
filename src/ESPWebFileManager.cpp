/* 
 * ESPWebFileManager Library
 * Copyright (C) 2024 Jobit Joseph
 * Licensed under the Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International (CC BY-NC-SA 4.0)
 * You may not use this work for commercial purposes. Modifications must credit the original author.
 * See the LICENSE file for more details.
 * Project Name: ESPWebFileManager Library
 * Project Brief: ESPWebFileManager Library
 * Author: Jobit Joseph @ https://github.com/jobitjoseph
 * IDE: Arduino IDE 2.x.x
 * Arduino Core: ESP32 Arduino Core V 3.0.7
 * GitHub: https://github.com/jobitjoseph/ESPWebFileManager
 * Dependencies : 
 *                Async TCP Library for ESP32 V 3.2.14 @ https://github.com/mathieucarbou/AsyncTCP
 *                ESPAsyncWebServer Library V 2.2.6 @ https://github.com/mathieucarbou/ESPAsyncWebServer
 * Copyright © Jobit Joseph
 * 
 * This code is licensed under the following conditions:
 *
 * 1. Non-Commercial Use:
 * This program is free software: you can redistribute it and/or modify it
 * for personal or educational purposes under the condition that credit is given 
 * to the original author. Attribution is required, and the original author 
 * must be credited in any derivative works or distributions.
 *
 * 2. Commercial Use:
 * For any commercial use of this software, you must obtain a separate license
 * from the original author. Contact the author for permissions or licensing
 * options before using this software for commercial purposes.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
 * FITNESS FOR A PARTICULAR PURPOSE, AND NONINFRINGEMENT. IN NO EVENT SHALL 
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES, OR OTHER 
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT, OR OTHERWISE, ARISING 
 * FROM, OUT OF, OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER 
 * DEALINGS IN THE SOFTWARE.
 *
 * Author: Jobit Joseph
 * Date: 19 December 2024
 *
 * For commercial use or licensing requests, please contact [jobitjoseph1@gmail.com].
 */
#include "ESPWebFileManager.h"

#include "SecData.h"

#include <FS.h>

#include <SPIFFS.h>

#include <LittleFS.h>

#include <FFat.h> // FATFS support

#include <SPI.h>

#include <SD.h>

// Constructor for SPIFFS, FATFS, or LittleFS
ESPWebFileManager::ESPWebFileManager(int fsType, bool formatOnFailFlag): _fsType(fsType), _formatOnFailFlag(formatOnFailFlag), _SPIreconfig(false), _csPin(-1) {
  for (int i = 0; i < 4; i++) _args[i] = -1;
}

// Constructor for SD Card with optional SPI remapping
ESPWebFileManager::ESPWebFileManager(int fsType, bool formatOnFailFlag, bool SPIreconfig, int csPin, int mosi, int miso, int sck): _fsType(fsType), _formatOnFailFlag(formatOnFailFlag), _SPIreconfig(SPIreconfig), _csPin(csPin) {
  _args[0] = csPin; // CS pin
  _args[1] = mosi; // MOSI pin
  _args[2] = miso; // MISO pin
  _args[3] = sck; // SCK pin
}

// Public begin function to initialize the chosen file system
bool ESPWebFileManager::begin() {
  switch (_fsType) {
  case FS_SPIFFS:
    return initFileSystem(SPIFFS, "SPIFFS", [] {
      return SPIFFS.begin();
    }, [] {
      return SPIFFS.format();
    });
  case FS_LITTLEFS:
    return initFileSystem(LittleFS, "LittleFS", [] {
      return LittleFS.begin();
    }, [] {
      return LittleFS.format();
    });
  case FS_FATFS:
    return initFileSystem(FFat, "FATFS", [] {
      return FFat.begin();
    }, [] {
      return FFat.format();
    });
  case FS_SD:
    return initSD(); // SD initialization remains separate due to SPI handling
  default:
    DEBUG_PRINTLN("Invalid file system type.");
    return false;
  }
}

// Generic file system initialization
bool ESPWebFileManager::initFileSystem(fs::FS & fs,
  const char * fsName, std:: function < bool() > beginFn, std:: function < bool() > formatFn) {
  DEBUG_PRINTX("Initializing %s...\n", fsName);

  if (beginFn()) {
    DEBUG_PRINTX("%s Mounted Successfully!\n", fsName);
    memory_ready = true;
    current_fs = & fs;
    return true;
  }

  DEBUG_PRINTX("%s Mount Failed!\n", fsName);

  if (_formatOnFailFlag) {
    DEBUG_PRINTX("Formatting %s...\n", fsName);
    if (formatFn()) {
      DEBUG_PRINTX("%s Format Successful. Retrying mount...\n", fsName);
      if (beginFn()) {
        DEBUG_PRINTX("%s Mounted Successfully after format!\n", fsName);
        memory_ready = true;
        current_fs = & fs;
        return true;
      }
    }
    DEBUG_PRINTX("%s Format Failed!\n", fsName);
  } else {
    DEBUG_PRINTX("%s initialization failed and formatting skipped.\n", fsName);
  }

  memory_ready = false;
  return false;
}

// Initialize SD card with optional SPI remapping

bool ESPWebFileManager::initSD() {
  DEBUG_PRINTLN("Initializing SD Card...");

  if (_SPIreconfig) {
    DEBUG_PRINTLN("Remapping SPI pins...");
    SPI.begin(_args[3], _args[2], _args[1], _args[0]); // SCK, MISO, MOSI, CS
  }

  bool sdMounted = (_csPin == -1) ? SD.begin() : SD.begin(_csPin);
  if (!sdMounted) {
    DEBUG_PRINTLN("SD Card Mount Failed!");
    if (_formatOnFailFlag) {
      DEBUG_PRINTLN("Attempting to format SD Card... (Not supported)");
    } else {
      DEBUG_PRINTLN("SD Card initialization failed and formatting skipped.");
    }
    memory_ready = false;
    return false;
  }

  DEBUG_PRINTLN("SD Card Mounted Successfully!");
  memory_ready = true;
  current_fs = & SD;
  return true;
}
int ESPWebFileManager::getFsType() {
  return _fsType;
}

String ESPWebFileManager::sanitizePath(const String & path) {
  String sanitized = path;
  while (sanitized.indexOf("//") >= 0) {
    sanitized.replace("//", "/");
  }
  if (sanitized.endsWith("/")) {
    sanitized.remove(sanitized.length() - 1); // Remove trailing slash
  }
  return sanitized;
}

void ESPWebFileManager::listDir(const char * dirname, uint8_t levels) {
  if (!memory_ready) {
    DEBUG_PRINTLN("File system not initialized.");
    return;
  }

  File root = current_fs -> open(dirname);
  if (!root || !root.isDirectory()) {
    DEBUG_PRINTLN("Failed to open directory.");
    return;
  }

  str_data = ""; // Clear the response string
  File file = root.openNextFile();

  while (file) {
    if (!str_data.isEmpty()) {
      str_data += ":"; // Separate entries with a colon
    }

    if (file.isDirectory()) {
      str_data += "1," + String(file.name()) + ",-"; // Folders don't have sizes
    } else {
      str_data += "0," + String(file.name()) + "," + String(file.size());
    }
    file = root.openNextFile();
  }

  file.close();
}

void ESPWebFileManager::setServer(AsyncWebServer * server) {
  DEBUG_PRINTLN("Starting Server...!");
  if (server == nullptr) {
    DEBUG_PRINT("Server is null!");
    return;
  }
  _server = server;

  _server -> on("/file", HTTP_GET, [ & ](AsyncWebServerRequest * request) {
    AsyncWebServerResponse * response = request -> beginResponse_P(200, "text/html", SecData, SecData_len);
    response -> addHeader("Content-Encoding", "gzip");
    request -> send(response);
    // request->send(200, "text/html", html_page); 
    // request->send(200, "text/plain", "Test route working");
  });

  _server -> on("/get-fs-type", HTTP_GET, [ & ](AsyncWebServerRequest * request) {
    String fsTypeStr = String(_fsType);
    request -> send(200, "text/plain", fsTypeStr);
  });

  server -> on("/get-folder-contents", HTTP_GET, [ & ](AsyncWebServerRequest * request) {
    DEBUG_PRINT2("path:", request -> arg("path").c_str());
    listDir(request -> arg("path").c_str(), 0);
    request -> send(200, "text/plain", str_data);
  });

  server -> on("/upload", HTTP_POST, [ & ](AsyncWebServerRequest * request) {
    request -> send(200, "application/json", "{\"status\":\"success\",\"message\":\"File upload complete\"}");
  }, [ & ](AsyncWebServerRequest * request, String filename, size_t index, uint8_t * data, size_t len, bool final) {
    String dir_path = "/";
    if (request -> hasParam("path")) {
      dir_path = request -> getParam("path") -> value();
      //DEBUG_PRINT2("Received path parameter: ", dir_path);
    } else {
      //DEBUG_PRINTLN("No path parameter received, defaulting to root.");
    }

    // Sanitize and prepare the folder path
    dir_path = sanitizePath(dir_path);
    if (!dir_path.endsWith("/")) {
      dir_path += "/";
    }

    // Construct full file path
    String file_path = dir_path + filename;
    //DEBUG_PRINT2("Full file path: ", file_path);

    // Ensure directory exists
    if (!index) {
      if (!current_fs -> exists(dir_path)) {
        DEBUG_PRINTX("Creating directory: %s\n", dir_path.c_str());
        if (!current_fs -> mkdir(dir_path)) {
          DEBUG_PRINTLN("Failed to create directory.");
          request -> send(500, "application/json", "{\"status\":\"error\",\"message\":\"Failed to create directory\"}");
          return;
        }
      }

      // Remove existing file
      if (current_fs -> exists(file_path)) {
        current_fs -> remove(file_path);
      }
    }

    // Write file data
    File file = current_fs -> open(file_path, FILE_APPEND);
    if (file) {
      if (file.write(data, len) != len) {
        DEBUG_PRINTLN("File write failed.");
      }
      file.close();
    } else {
      DEBUG_PRINTLN("Failed to open file for writing.");
      request -> send(500, "application/json", "{\"status\":\"error\",\"message\":\"Failed to open file\"}");
      return;
    }

    if (final) {
      DEBUG_PRINTX("UploadEnd: %s, %u B\n", file_path.c_str(), index + len);
    }
  });

  server -> on("/upload", HTTP_POST, [ & ](AsyncWebServerRequest * request) {
    request -> send(200, "application/json", "{\"status\":\"success\",\"message\":\"File upload complete\"}");
  }, [ & ](AsyncWebServerRequest * request, String filename, size_t index, uint8_t * data, size_t len, bool final) {
    String dir_path = "/";
    if (request -> hasParam("path")) {
      dir_path = request -> getParam("path") -> value();
    }
    dir_path = sanitizePath(dir_path);
    if (!dir_path.endsWith("/")) {
      dir_path += "/";
    }

    String file_path = dir_path + filename;

    if (!index) {
      if (!current_fs -> exists(dir_path)) {
        DEBUG_PRINTX("Creating directory: %s\n", dir_path.c_str());
        if (!current_fs -> mkdir(dir_path)) {
          DEBUG_PRINTLN("Failed to create directory.");
          request -> send(500, "application/json", "{\"status\":\"error\",\"message\":\"Failed to create directory\"}");
          return;
        }
      }

      if (current_fs -> exists(file_path)) {
        current_fs -> remove(file_path);
      }
    }

    File file = current_fs -> open(file_path, FILE_APPEND);
    if (file) {
      if (file.write(data, len) != len) {
        DEBUG_PRINTLN("File write failed.");
      }
      file.close();
    } else {
      DEBUG_PRINTLN("Failed to open file for writing.");
      request -> send(500, "application/json", "{\"status\":\"error\",\"message\":\"Failed to open file\"}");
      return;
    }

    if (final) {
      DEBUG_PRINTX("UploadEnd: %s, %u B\n", file_path.c_str(), index + len);
    }
  });

  // Route to create a new folder
  server -> on("/create-folder", HTTP_GET, [ & ](AsyncWebServerRequest * request) {
    String path = request -> hasParam("path") ? request -> getParam("path") -> value() : "";
    path = sanitizePath(path); // Sanitize the folder path

    if (path.isEmpty()) {
      request -> send(400, "application/json", "{\"status\":\"error\",\"message\":\"Folder path not provided\"}");
      return;
    }

    DEBUG_PRINT2("Creating folder: ", path);

    if (!current_fs -> exists(path)) {
      if (current_fs -> mkdir(path)) {
        request -> send(200, "application/json", "{\"status\":\"success\",\"message\":\"Folder created successfully\"}");
      } else {
        request -> send(500, "application/json", "{\"status\":\"error\",\"message\":\"Failed to create folder\"}");
      }
    } else {
      request -> send(400, "application/json", "{\"status\":\"error\",\"message\":\"Folder already exists\"}");
    }
  });

  // Route to delete a folder
  server -> on("/delete-folder", HTTP_GET, [ & ](AsyncWebServerRequest * request) {
    String path = request -> hasParam("path") ? request -> getParam("path") -> value() : "";
    path = sanitizePath(path); // Sanitize the folder path

    if (path.isEmpty()) {
      request -> send(400, "application/json", "{\"status\":\"error\",\"message\":\"Folder path not provided\"}");
      return;
    }

    DEBUG_PRINT2("Deleting folder: ", path);

    if (current_fs -> exists(path)) {
      if (current_fs -> rmdir(path)) { // Use rmdir for directories
        request -> send(200, "application/json", "{\"status\":\"success\",\"message\":\"Folder deleted successfully\"}");
      } else {
        request -> send(500, "application/json", "{\"status\":\"error\",\"message\":\"Failed to delete folder\"}");
      }
    } else {
      request -> send(404, "application/json", "{\"status\":\"error\",\"message\":\"Folder not found\"}");
    }
  });

  server -> on("/delete", HTTP_GET, [ & ](AsyncWebServerRequest * request) {
    String path = request -> hasParam("path") ? request -> getParam("path") -> value() : "";
    path = sanitizePath(path); // Apply sanitization

    DEBUG_PRINT2("Deleting File: ", path);
    if (current_fs -> exists(path)) {
      current_fs -> remove(path);
      request -> send(200, "application/json", "{\"status\":\"success\",\"message\":\"File deleted successfully\"}");
    } else {
      request -> send(404, "application/json", "{\"status\":\"error\",\"message\":\"File not found\"}");
    }
  });

  server -> on("/download", HTTP_GET, [ & ](AsyncWebServerRequest * request) {
    String path;
    if (request -> hasParam("path")) {
      path = request -> getParam("path") -> value();
    } else {
      request -> send(400, "application/json", "{\"status\":\"error\",\"message\":\"Path not provided\"}");
      return;
    }
    path = sanitizePath(path); // Apply sanitization
    DEBUG_PRINT2("Downloading File: ", path);
    if (current_fs -> exists(path)) {
      request -> send( * current_fs, path, String(), true);
    } else {
      request -> send(404, "application/json", "{\"status\":\"error\",\"message\":\"File not found\"}");
    }
  });
}
