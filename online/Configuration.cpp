/* reaDIYmate AVR library
 * Written by Pierre Bouchet
 * Copyright (C) 2011-2012 reaDIYmate
 *
 * This file is part of the reaDIYmate library.
 *
 * This library is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */
#include <Configuration.h>
//------------------------------------------------------------------------------
#ifdef DEBUG
    #define DEBUG_LOG(str) Serial.println(F(str))
#else
    #define DEBUG_LOG(str)
#endif
//------------------------------------------------------------------------------
/** End character */
uint8_t const COMMAND_END_CHAR = '}';
/** UART timeout */
uint32_t const WIZARD_TIMEOUT = 5000;
//------------------------------------------------------------------------------
// Strings used to communication with the reaDIYmate Companion
/** SSID of the WLAN */
const char PROGMEM WIZARD_KEY_SSID[] = "ssid";
/** Passphrase for the WLAN */
const char PROGMEM WIZARD_KEY_PASSPHRASE[] = "phrase";
/** Connection mode */
const char PROGMEM WIZARD_KEY_MODE[] = "mode";
/** DHCP connection mode */
const char PROGMEM WIZARD_DHCP[] = "dhcp";
/** Static IP address */
const char PROGMEM WIZARD_KEY_IP[] = "ip";
/** Gateway IP */
const char PROGMEM WIZARD_KEY_GATEWAY[] = "gateway";
/** Network mask */
const char PROGMEM WIZARD_KEY_NETMASK[] = "mask";
/** User name of the reaDIYmate website */
const char PROGMEM WIZARD_KEY_USER[] = "user";
/** Password of the reaDIYmate website */
const char PROGMEM WIZARD_KEY_PASSWORD[] = "token";
/** Key for Pusher */
const char PROGMEM WIZARD_KEY_KEY[] = "key";
/** Secret key for Pusher */
const char PROGMEM WIZARD_KEY_SECRET[] = "secret";
/** Channel for Pusher */
const char PROGMEM WIZARD_KEY_CHANNEL[] = "channel";
/** Default position of the servo motor for this reaDIYmate */
const char PROGMEM WIZARD_KEY_ORIGIN[] = "origin";
/**  Key for the command */
const char PROGMEM COMMAND_TYPE[] = "cmd";
/** DeviceID mode */
const char PROGMEM COMMAND_DEVICEID[] = "id";
/** Configuration mode */
const char PROGMEM COMMAND_AUTH[] = "auth";
/** Configuration mode */
const char PROGMEM COMMAND_BOOTLOADER[] = "bootloader";
/** Factory mode */
const char PROGMEM COMMAND_FACTORY[] = "factory";
/** Update Wifly firmware mode */
const char PROGMEM COMMAND_UPDATE[] = "update";
/** Pusher mode */
const char PROGMEM COMMAND_PUSHER[] = "pusher";
/** WLAN config */
const char PROGMEM COMMAND_WLAN[] = "wlan";
/** Format SD card */
const char PROGMEM COMMAND_FORMAT[] = "format";
/** Set servo motor origin */
const char PROGMEM COMMAND_SERVO[] = "servo";
/** Act as a proxy for the RN171 */
const char PROGMEM COMMAND_PROXY[] = "proxy";
/** Start-up signal */
const char PROGMEM WIZARD_STARTUP[] = "absolutLabs\n";
/** Command confirmation */
const char PROGMEM WIZARD_AOK[] = "AOK\r\n";
/** Command error */
const char PROGMEM WIZARD_ERR[] = "ERR\r\n";
//------------------------------------------------------------------------------
/** Format string used to construct the credential for API calls */
const char API_CREDENTIAL_FORMAT[] PROGMEM = "id=%s&user=%s&token=%s";
//------------------------------------------------------------------------------
/**
 * Construct an instance of Configuration.
 *
 * \param[in] companion The serial port that is connected to the companion.
 * \param[in] wifly The Wifly object to use for communications.
 */
Configuration::Configuration(HardwareSerial &companion, Wifly &wifly,
    uint8_t sdChipSelectPin) :
    SerialStream(companion, WIZARD_TIMEOUT),
    wifly_(&wifly),
    sdChipSelectPin_(sdChipSelectPin),
    username_(NULL),
    password_(NULL),
    key_(NULL),
    secret_(NULL),
    channel_(NULL)
{
    restoreDeviceId();
    restoreUserAndPass();
    restorePusher();
}
//------------------------------------------------------------------------------
/** Free the memory allocated for application parameters */
Configuration::~Configuration() {
    if (username_ != NULL) {
        free(username_);
        username_ = NULL;
    }
    if (password_ != NULL) {
        free(password_);
        password_ = NULL;
    }
    if (key_ != NULL) {
        free(key_);
        key_ = NULL;
    }
    if (secret_ != NULL) {
        free(secret_);
        secret_ = NULL;
    }
    if (channel_ != NULL) {
        free(channel_);
        channel_ = NULL;
    }
}
//------------------------------------------------------------------------------
/*
 * This method writes the "magic" value to the last EEPROM word. The reaDIYboot
 * bootloader always checks this location at start-up. If the value is anything
 * else than 0x232e, reaDIYboot will act as a regular STK500 bootloader.
 */
 void Configuration::enableBootloader() {
    eeprom_write_word((uint16_t*)(0xFFF - 1), 0x232e);
}
//------------------------------------------------------------------------------
void Configuration::enterProxyMode() {
    while (true) {
        if (wifly_->available()) {
            write(wifly_->read());
        }
        else if (available()) {
            uint8_t ch = read();
            if (ch == '\\') {
                while (!available());
                ch = read();
                switch (ch) {
                    case 'R' :
                        print("\r\n-Reset-\r\n");
                        wifly_->reset();
                        break;
                    case 'C' :
                        print("\r\n-Command mode-\r\n");
                        if (wifly_->enterCommandMode()) {
                            println(F("CMD"));
                        }
                        break;
                    case 'H' :
                        print("\r\n-115200-\r\n");
                        wifly_->begin(115200);
                        wifly_->clear();
                        break;
                    case 'L' :
                        print("\r\n-9600-\r\n");
                        wifly_->begin(9600);
                        wifly_->clear();
                        break;
                    case 'r' :
                        wifly_->write('\r');
                    default :
                        wifly_->write('\\');
                        wifly_->write(ch);
                        break;
                }
            }
            else {
                wifly_->write(ch);
            }
        }
    }
}
//------------------------------------------------------------------------------
/**
 * Using the device ID, the username and the password, generate a string that
 * will be appended to every request sent to the reaDIYmate API.
 *
 * \param[out] buffer The location where the credential string should be written.
 * \param[out] bufferSize The maximum length of the string.
 */
void Configuration::getApiCredential(char* buffer, uint8_t bufferSize) {
    memset(buffer, 0x00, bufferSize);
    snprintf_P(
        buffer,
        bufferSize,
        API_CREDENTIAL_FORMAT,
        deviceId_,
        username_,
        password_
    );
}
//------------------------------------------------------------------------------
/**
 * Based on the value stored in the EEPROM, this method returns the default
 * position for the servo motor.
 *
 */
uint8_t Configuration::getServoDefaultPosition() {
    return eeprom_read_byte((const uint8_t*)EEPROM_SERVO_ORIGIN);
}
//------------------------------------------------------------------------------
/** Read the pusher key/secret/channel sent by the Companion */
bool Configuration::readPusher(char* buffer, uint8_t bufferSize) {
    JsonStream json = JsonStream(buffer, bufferSize);

    char key[22] = {0};
    char secret[22] = {0};
    char channel[22] = {0};

    json.getStringByName_P(WIZARD_KEY_KEY, key, 22);
    json.getStringByName_P(WIZARD_KEY_SECRET, secret, 22);
    json.getStringByName_P(WIZARD_KEY_CHANNEL, channel, 22);

    if (strlen(key) == 0) {
        return false;
    }

    if (strcmp(key_, key) != 0
    || strcmp(secret_, secret) != 0
    || strcmp(channel_, channel) != 0) {
        key_ = key;
        secret_ = secret;
        channel_ = channel;
        savePusher();
    }

    return true;
}
//------------------------------------------------------------------------------
/** Set the default servo position */
bool Configuration::readServoDefaultPosition(char* buffer, uint8_t bufferSize) {
    JsonStream json = JsonStream(buffer, bufferSize);
    int origin = json.getIntegerByName_P(WIZARD_KEY_ORIGIN);
    if (origin < 0) {
        return false;
    }
    eeprom_write_byte((uint8_t*)EEPROM_SERVO_ORIGIN, origin);
    return true;
 }
//------------------------------------------------------------------------------
/** Read the username and password sent by the Companion */
bool Configuration::readUserAndPass(char* buffer, uint8_t bufferSize) {
    JsonStream json = JsonStream(buffer, bufferSize);

    char newUsername[64] = {0};
    char newPassword[128] = {0};

    json.getStringByName_P(WIZARD_KEY_USER, newUsername, 64);
    json.getStringByName_P(WIZARD_KEY_PASSWORD, newPassword, 128);

    if (strlen(newUsername) == 0 || strlen(newPassword) == 0) {
        return false;
    }

    if (strcmp(username_, newUsername) != 0
    || strcmp(password_, newPassword) != 0) {
        username_ = newUsername;
        password_ = newPassword;
        saveUserAndPass();
    }

    return true;
}
//------------------------------------------------------------------------------
/** Read the Wi-Fi settings sent by the Companion and update the WiFly config */
bool Configuration::readWifiSettings(char* buffer, uint8_t bufferSize) {
    JsonStream json = JsonStream(buffer, bufferSize);

    // parse the individual settings one by one
    char mode[8] = {0};
    json.getStringByName_P(WIZARD_KEY_MODE, mode, 8);
    char ip[16] = {0};
    json.getStringByName_P(WIZARD_KEY_IP, ip, 16);
    char mask[16] = {0};
    json.getStringByName_P(WIZARD_KEY_NETMASK, mask, 16);
    char gateway[16] = {0};
    json.getStringByName_P(WIZARD_KEY_GATEWAY, gateway, 16);
    char ssid[32] = {0};
    json.getStringByName_P(WIZARD_KEY_SSID, ssid, 32);
    char passphrase[64] = {0};
    json.getStringByName_P(WIZARD_KEY_PASSPHRASE, passphrase, 64);

    // check the validity of the new settings
    bool dhcp = (strcmp_P(mode, WIZARD_DHCP) == 0);
    if (strlen(ssid) == 0 || strlen(passphrase) == 0) {
        return false;
    }
    if (dhcp == false && (strlen(ip) == 0 || strlen(mask) == 0 || strlen(gateway) == 0)) {
        return false;
    }

    // update the configuration of the Wi-Fi module
    if (dhcp == true) {
        return wifly_->setWlanConfig(ssid, passphrase);
    }
    else {
        return wifly_->setWlanConfig(ssid, passphrase, ip, mask, gateway);
    }
}
//------------------------------------------------------------------------------
/**
  * Restore the device ID from the EEPROM to the SRAM
  *
  * \note The device ID is the MAC address of the Wi-Fi module stored in ASCII.
  */
void Configuration::restoreDeviceId() {
    memset(deviceId_, 0x00, 13);
    eeprom_read_block((void*)deviceId_, (const void*)EEPROM_DEVICEID, 12);
}
//------------------------------------------------------------------------------
/** Restore username and password from the EEPROM to the SRAM */
void Configuration::restorePusher() {
    char buffer[21];

    // read key
    memset(buffer, 0x00, 21);
    eeprom_read_block((void*)buffer, (const void*)EEPROM_PUSHER_KEY, 21);
    key_ = (char*)calloc(strlen(buffer) + 1,sizeof(char));
    strcpy(key_, buffer);

    // read secret
    memset(buffer, 0x00, 21);
    eeprom_read_block((void*)buffer, (const void*)EEPROM_PUSHER_SECRET, 21);
    secret_ = (char*)calloc(strlen(buffer) + 1,sizeof(char));
    strcpy(secret_, buffer);

    // read channel
    memset(buffer, 0x00, 21);
    eeprom_read_block((void*)buffer, (const void*)EEPROM_PUSHER_CHANNEL, 21);
    channel_ = (char*)calloc(strlen(buffer) + 1,sizeof(char));
    strcpy(channel_, buffer);
}
//------------------------------------------------------------------------------
/** Restore username and password from the EEPROM to the SRAM */
void Configuration::restoreUserAndPass() {
    char buffer[64];

    // read username
    memset(buffer, 0x00, 64);
    eeprom_read_block((void*)buffer, (const void*)EEPROM_USERNAME, 32);
    username_ = (char*)calloc(strlen(buffer) + 1,sizeof(char));
    strcpy(username_, buffer);

    // read password
    memset(buffer, 0x00, 64);
    eeprom_read_block((void*)buffer, (const void*)EEPROM_PASSWORD, 64);
    password_ = (char*)calloc(strlen(buffer) + 1,sizeof(char));
    strcpy(password_, buffer);
}
//------------------------------------------------------------------------------
/** Save Pusher key/secret/channel from the SRAM to the EEPROM */
void Configuration::savePusher() {
    char buffer[21];

    // save key
    memset(buffer, 0x00, 21);
    strcpy(buffer, key_);
    eeprom_write_block((const void*)buffer, (void*)EEPROM_PUSHER_KEY, 21);

    // save secret
    memset(buffer, 0x00, 21);
    strcpy(buffer, secret_);
    eeprom_write_block((const void*)buffer, (void*)EEPROM_PUSHER_SECRET, 21);

    // save channel
    memset(buffer, 0x00, 21);
    strcpy(buffer, channel_);
    eeprom_write_block((const void*)buffer, (void*)EEPROM_PUSHER_CHANNEL, 21);
}
//------------------------------------------------------------------------------
/** Save username and password from the SRAM to the EEPROM */
void Configuration::saveUserAndPass() {
    char buffer[64];

    // save username
    memset(buffer, 0x00, 64);
    strcpy(buffer, username_);
    eeprom_write_block((const void*)buffer, (void*)EEPROM_USERNAME, 32);

    // save password
    memset(buffer, 0x00, 64);
    strcpy(buffer, password_);
    eeprom_write_block((const void*)buffer, (void*)EEPROM_PASSWORD, 64);
}
//------------------------------------------------------------------------------
/** Save the device ID the SRAM to the EEPROM */
void Configuration::saveDeviceId() {
    eeprom_write_block((const void*)deviceId_, (void*)EEPROM_DEVICEID, 12);
}
//------------------------------------------------------------------------------
/** Make sure the device ID is valid. */
bool Configuration::validateDeviceId() {
    DEBUG_LOG("Validating device ID");
    if (strrchr(deviceId_, 0xFF) == NULL && strlen(deviceId_) == 12) {
        return true;
    }
    wifly_->reset();
    if (!wifly_->enterCommandMode()) {
        DEBUG_LOG("Failed to enter command mode");
        return false;
    }
    wifly_->getDeviceId(deviceId_);
    if (strrchr(deviceId_, 0xFF) != NULL || strlen(deviceId_) != 12) {
        return false;
    }
    saveDeviceId();
    DEBUG_LOG("Device ID successfully renewed.");
    return true;
}
//------------------------------------------------------------------------------
/** Send the device ID to the Companion */
void Configuration::sendDeviceId() {
    print(deviceId_);
    write('\n');
}
//------------------------------------------------------------------------------
/**
 * Attempt to synchronize with the reaDIYmate Companion via a Serial port
 *
 * \param[in] timeout Timeout used to decide when to abort the synchronization.
 */
void Configuration::synchronize(uint16_t timeout) {
    char buffer[512] = {0};
    write_P(WIZARD_STARTUP);

    uint32_t deadline = millis() + timeout;
    while (millis() < deadline) {
        memset(buffer, 0x00, 512);
        int nBytes = readBytesUntil(COMMAND_END_CHAR, buffer, 511);
        buffer[nBytes] = COMMAND_END_CHAR;
        JsonStream json = JsonStream(buffer, nBytes);

        char cmd[16] = {0};
        int cmdLen = json.getStringByName_P(COMMAND_TYPE, cmd, 16);

        if (cmdLen > 0) {
            if (strcmp_P(cmd, COMMAND_DEVICEID) == 0) {
                if (validateDeviceId()) {
                    write_P(WIZARD_AOK);
                    sendDeviceId();
                }
                else {
                    DEBUG_LOG("Invalid device ID detected.");
                    write_P(WIZARD_ERR);
                }
                deadline = millis() + timeout;
            }
            else if (strcmp_P(cmd, COMMAND_AUTH) == 0) {
                DEBUG_LOG("Command received: set authentication.");
                if (readUserAndPass(buffer, nBytes)) {
                    DEBUG_LOG("Authentication settings accepted.");
                    write_P(WIZARD_AOK);
                }
                else {
                    DEBUG_LOG("Authentication settings refused.");
                    write_P(WIZARD_ERR);
                }
                deadline = millis() + timeout;
            }
            else if (strcmp_P(cmd, COMMAND_WLAN) == 0) {
                DEBUG_LOG("Command received: set WLAN config.");
                if (readWifiSettings(buffer, nBytes)) {
                    DEBUG_LOG("WLAN config accepted.");
                    write_P(WIZARD_AOK);
                }
                else {
                    DEBUG_LOG("WLAN config refused.");
                    write_P(WIZARD_ERR);
                }
                deadline = millis() + timeout;
            }
            else if (strcmp_P(cmd, COMMAND_PUSHER) == 0) {
                DEBUG_LOG("Command received: setup Pusher messaging.");
                if (readPusher(buffer, nBytes)) {
                    DEBUG_LOG("Pusher information accepted.");
                    write_P(WIZARD_AOK);
                }
                else {
                    DEBUG_LOG("Pusher information refused.");
                    write_P(WIZARD_ERR);
                }
                deadline = millis() + timeout;
            }
            else if (strcmp_P(cmd, COMMAND_BOOTLOADER) == 0) {
                DEBUG_LOG("Command received: enable Wi-Fi bootloader.");
                enableBootloader();
                DEBUG_LOG("Bootloader enabled.");
                write_P(WIZARD_AOK);
                deadline = millis() + timeout;
            }
            else if (strcmp_P(cmd, COMMAND_SERVO) == 0) {
                DEBUG_LOG("Command received: set servo origin.");
                if (readServoDefaultPosition(buffer, nBytes)) {
                    DEBUG_LOG("Servo origin set.");
                    write_P(WIZARD_AOK);
                }
                else {
                    DEBUG_LOG("Failed to set servo origin.");
                    write_P(WIZARD_ERR);
                }
                deadline = millis() + timeout;
            }
            else if (strcmp_P(cmd, COMMAND_FACTORY) == 0) {
                DEBUG_LOG("Command received: perform factory reset.");

                if (!wifly_->resetBaudrateAndFirmware()) {
                    DEBUG_LOG("Failed to reset the RN171 firmware.");
                    write_P(WIZARD_ERR);
                }
                else if (!wifly_->resetConfigToDefault()) {
                    DEBUG_LOG("Failed to set the RN171 default config.");
                    write_P(WIZARD_ERR);
                }
                else {
                    DEBUG_LOG("Erasing EEPROM...");
                    for (int i = 0; i < 4096; i++) {
                        eeprom_write_byte((uint8_t *)(i), 0x00);
                    }
                    DEBUG_LOG("EEPROM cleared.");

                    wifly_->getDeviceId(deviceId_);
                    saveDeviceId();
                    wifly_->begin(115200);
                    wifly_->clear();
                    DEBUG_LOG("Reset to default config performed.");
                    write_P(WIZARD_AOK);
                }
                deadline = millis() + timeout;
            }
            else if (strcmp_P(cmd, COMMAND_UPDATE) == 0) {
                DEBUG_LOG("Command received: update Wi-Fi firmware.");
                if (wifly_->updateFirmware()) {
                    DEBUG_LOG("Wi-Fi firmware updated.");
                    write_P(WIZARD_AOK);
                }
                else {
                    DEBUG_LOG("Firmware update failed.");
                    write_P(WIZARD_ERR);
                }
                deadline = millis() + timeout;
            }
            else if (strcmp_P(cmd, COMMAND_PROXY) == 0) {
                DEBUG_LOG("Command received: enter proxy mode.");
                write_P(WIZARD_AOK);
                enterProxyMode();
                deadline = millis() + timeout;
            }
            else {
                DEBUG_LOG("Command not recognized.");
            }
        }
    }
}
