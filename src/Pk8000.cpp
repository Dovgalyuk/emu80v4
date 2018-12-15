﻿/*
 *  Emu80 v. 4.x
 *  © Viktor Pykhonin <pyk@mail.ru>, 2016-2018
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <string.h>

#include "Pk8000.h"
#include "Emulation.h"
#include "Platform.h"
#include "Globals.h"
#include "EmuWindow.h"
#include "SoundMixer.h"
#include "Memory.h"
#include "AddrSpace.h"
#include "Cpu.h"
#include "Fdc1793.h"
#include "WavReader.h"

using namespace std;


Pk8000Core::Pk8000Core()
{
    // ...
}



Pk8000Core::~Pk8000Core()
{
    // ...
}


void Pk8000Core::reset()
{
    m_intReq = false;
}


void Pk8000Core::draw()
{
    //m_crtRenderer->renderFrame();
    m_window->drawFrame(m_crtRenderer->getPixelData());
    m_window->endDraw();
}


void Pk8000Core::inte(bool isActive)
{
    Cpu8080Compatible* cpu = static_cast<Cpu8080Compatible*>(m_platform->getCpu());
    if (isActive && m_intReq && cpu->getInte()) {
        m_intReq = false;
        cpu->intRst(7);
    }
}


void Pk8000Core::vrtc(bool isActive)
{
    if (isActive) {
        Cpu8080Compatible* cpu = static_cast<Cpu8080Compatible*>(m_platform->getCpu());
        m_intReq = true;
        if (cpu->getInte()) {
            m_intReq = false;
            cpu->intRst(7);
        }
    }
}


void Pk8000Core::attachCrtRenderer(Pk8000Renderer* crtRenderer)
{
    m_crtRenderer = crtRenderer;
}


bool Pk8000Core::setProperty(const string& propertyName, const EmuValuesList& values)
{
    if (PlatformCore::setProperty(propertyName, values))
        return true;

    if (propertyName == "crtRenderer") {
        attachCrtRenderer(static_cast<Pk8000Renderer*>(g_emulation->findObject(values[0].asString())));
        return true;
    }
    return false;
}


Pk8000Renderer::Pk8000Renderer()
{
    for (int i = 0; i < 4; i++) {
        m_screenMemoryBanks[i] = nullptr;
        m_screenMemoryRamBanks[i] = nullptr;
    }

    const int pixelFreq = 5; // MHz
    const int maxBufSize = 261 * 288; // 261 = 704 / 13.5 * pixelFreq

    m_sizeX = m_prevSizeX = 256;
    m_sizeY = m_prevSizeY = 192;
    m_aspectRatio = m_prevAspectRatio = 5184. / 704 / pixelFreq;
    m_bufSize = m_prevBufSize = m_sizeX * m_sizeY;
    m_pixelData = new uint32_t[maxBufSize];
    m_prevPixelData = new uint32_t[maxBufSize];
    memset(m_pixelData, 0, m_bufSize * sizeof(uint32_t));
    memset(m_prevPixelData, 0, m_prevBufSize * sizeof(uint32_t));
    m_ticksPerInt = g_emulation->getFrequency() / 1000000 * 312 * 64;
}


void Pk8000Renderer::operate()
{
    renderFrame();
    m_curClock += m_ticksPerInt;
    m_platform->getCore()->vrtc(true);
}


void Pk8000Renderer::attachScreenMemoryBank(int bank, Ram* screenMemoryBank)
{
    if (bank >= 0 && bank < 4) {
        m_screenMemoryBanks[bank] = screenMemoryBank->getDataPtr();
        m_screenMemoryRamBanks[bank] = screenMemoryBank;
    }
}


void Pk8000Renderer::setScreenBank(unsigned bank)
{
    if (bank < 4)
        m_bank = bank;
}


void Pk8000Renderer::setMode(unsigned mode)
{
    if (mode < 3)
        m_mode = mode;
}


void Pk8000Renderer::setColorReg(unsigned addr, uint8_t value)
{
    m_screenMemoryRamBanks[m_bank]->writeByte(0x400 + addr, value);
}


uint8_t Pk8000Renderer::getColorReg(unsigned addr)
{
    return m_screenMemoryBanks[m_bank][0x400 + addr];
}


void Pk8000Renderer::renderFrame()
{
    swapBuffers();

    if (m_showBorder || m_blanking) {
        for (unsigned i = 0; i < 261 * 288; i++)
            m_pixelData[i] = m_bgColor;
        m_sizeY = 288; }
    else
        m_sizeY = 192;

    int offsetX;
    int offsetY = m_showBorder ? 48 : 0;
    int offset;

    if (!m_blanking)
        switch (m_mode) {
        case 0:
            if (m_showBorder) {
                m_sizeX = 261;
                offsetX = 21;
            } else {
                m_sizeX = 240;
                offsetX = 0;
            }

            offset = offsetX + offsetY * m_sizeX;
            for (int row = 0; row < 24; row++)
                for (int pos = 0; pos < 40; pos++) {
                    uint8_t chr = m_screenMemoryBanks[m_bank][(m_txtBase & ~0x0400) + row * 64 + pos];
                    for (int line = 0; line < 8; line++) {
                        uint8_t bt = m_screenMemoryBanks[m_bank][m_sgBase + chr * 8 + line];
                        for (int i = 0; i < 6; i++) {
                            uint32_t color = bt & 0x80 ? m_fgColor : m_bgColor;
                            m_pixelData[offset + m_sizeX * 8 * row + m_sizeX * line + pos * 6 + i] = color;
                            bt <<= 1;
                        }
                    }
                }
            break;
        case 1:
            if (m_showBorder) {
                m_sizeX = 261;
                offsetX = 5;
            } else {
                m_sizeX = 256;
                offsetX = 0;
            }

            offset = offsetX + offsetY * m_sizeX;
            for (int row = 0; row < 24; row++)
                for (int pos = 0; pos < 32; pos++) {
                    uint8_t chr = m_screenMemoryBanks[m_bank][m_txtBase + row * 32 + pos];
                    unsigned colorCode = m_screenMemoryBanks[m_bank][0x400 + (chr >> 3)];
                    uint32_t fgColor = c_pk8000ColorPalette[colorCode & 0x0F];
                    uint32_t bgColor = c_pk8000ColorPalette[colorCode >> 4];
                    for (int line = 0; line < 8; line++) {
                        uint8_t bt = m_screenMemoryBanks[m_bank][m_sgBase + chr * 8 + line];
                        for (int i = 0; i < 8; i++) {
                            uint32_t color = bt & 0x80 ? fgColor : bgColor;
                            m_pixelData[offset + m_sizeX * 8 * row + m_sizeX * line + pos * 8 + i] = color;
                            bt <<= 1;
                        }
                    }
                }
            break;
        case 2:
            if (m_showBorder) {
                m_sizeX = 261;
                offsetX = 5;
            } else {
                m_sizeX = 256;
                offsetX = 0;
            }

            offset = offsetX + offsetY * m_sizeX;
            for (int part = 0; part < 3; part++)
                for (int row = 0; row < 8; row++)
                    for (int pos = 0; pos < 32; pos++) {
                        uint8_t chr = m_screenMemoryBanks[m_bank][m_sgBase + part * 256 + row * 32 + pos];
                        for (int line = 0; line < 8; line++) {
                            unsigned colorCode = m_screenMemoryBanks[m_bank][m_colBase + part * 0x800 + chr * 8 + line];
                            uint32_t fgColor = c_pk8000ColorPalette[colorCode & 0x0F];
                            uint32_t bgColor = c_pk8000ColorPalette[colorCode >> 4];
                            uint8_t bt = m_screenMemoryBanks[m_bank][m_grBase + part * 0x800 + chr * 8 + line];
                            for (int i = 0; i < 8; i++) {
                                uint32_t color = bt & 0x80 ? fgColor : bgColor;
                                m_pixelData[offset + m_sizeX * 8 * 8 * part + m_sizeX * 8 * row + m_sizeX * line + pos * 8 + i] = color;
                                bt <<= 1;
                            }
                        }
                    }

            break;
        default:
            break;
        }

    if (m_showBorder) {
        m_aspectRatio = double(m_sizeY) * 4 / 3 / m_sizeX;
    } else {
        m_aspectRatio = 576.0 * 9 / 704 / 5;
    }

}


void Pk8000Renderer::toggleCropping()
{
    m_showBorder = !m_showBorder;
}


bool Pk8000Renderer::setProperty(const string& propertyName, const EmuValuesList& values)
{
    if (EmuObject::setProperty(propertyName, values))
        return true;

    if (propertyName == "screenMemoryBank") {
        attachScreenMemoryBank(values[0].asInt(), static_cast<Ram*>(g_emulation->findObject(values[1].asString())));
        return true;
    } else if (propertyName == "visibleArea") {
        if (values[0].asString() == "yes" || values[0].asString() == "no") {
            m_showBorder = values[0].asString() == "yes";
            return true;
        }
    }

    return false;
}


string Pk8000Renderer::getPropertyStringValue(const string& propertyName)
{
    string res;

    res = EmuObject::getPropertyStringValue(propertyName);
    if (res != "")
        return res;

    if (propertyName == "visibleArea") {
        return m_showBorder ? "yes" : "no";
    } else if (propertyName == "crtMode") {
        switch (m_mode) {
        case 0:
            return "Mode 0: 40\u00D76\u00D724\u00D78@50.08Hz";
            break;
        case 1:
            return "Mode 1: 32\u00D78\u00D724\u00D78@50.08Hz";
            break;
        case 2:
            return "Mode 2: 256\u00D7192@50.08Hz";
            break;
        default:
            return "Mode 3";
        }
    }

    return "";
}


Pk8000Ppi8255Circuit1::Pk8000Ppi8255Circuit1()
{
    m_beepSoundSource = new GeneralSoundSource;
    m_tapeSoundSource = new GeneralSoundSource;

    for (int i = 0; i < 4; i++)
        m_addrSpaceMappers[i] = nullptr;
}


Pk8000Ppi8255Circuit1::~Pk8000Ppi8255Circuit1()
{
    delete m_beepSoundSource;
    delete m_tapeSoundSource;
}


void Pk8000Ppi8255Circuit1::attachAddrSpaceMapper(int bank, AddrSpaceMapper* addrSpaceMapper)
{
    if (bank >= 0 && bank < 4)
        m_addrSpaceMappers[bank] = addrSpaceMapper;
}


// port 80h
void Pk8000Ppi8255Circuit1::setPortA(uint8_t value)
{
    m_addrSpaceMappers[0]->setCurPage(value & 0x03);
    m_addrSpaceMappers[1]->setCurPage((value & 0x0C) >> 2);
    m_addrSpaceMappers[2]->setCurPage((value & 0x30) >> 4);
    m_addrSpaceMappers[3]->setCurPage((value & 0xC0) >> 6);
};


// port 81h
uint8_t Pk8000Ppi8255Circuit1::getPortB()
{
    if (m_kbd)
        return m_kbd->getMatrixRowState();
    else
        return 0xFF;

}


// port 82h
void Pk8000Ppi8255Circuit1::setPortC(uint8_t value)
{
    if (m_kbd)
        m_kbd->setMatrixRowNo(value & 0x0F);

    m_beepSoundSource->setValue(value & 0x80 ? 1 : 0);
    m_tapeSoundSource->setValue(value & 0x40 ? 1 : 0);
    m_platform->getCore()->tapeOut(value & 0x40);
}


bool Pk8000Ppi8255Circuit1::setProperty(const string& propertyName, const EmuValuesList& values)
{
    if (EmuObject::setProperty(propertyName, values))
        return true;

    if (propertyName == "bank") {
        attachAddrSpaceMapper(values[0].asInt(), static_cast<AddrSpaceMapper*>(g_emulation->findObject(values[1].asString())));
        return true;
    } else if (propertyName == "keyboard") {
        attachKeyboard(static_cast<Pk8000Keyboard*>(g_emulation->findObject(values[0].asString())));
        return true;
    }

    return false;
}


// port 84h
void Pk8000Ppi8255Circuit2::setPortA(uint8_t value)
{
    if (m_renderer) {
        if (value & 0x10)
            m_renderer->setMode(2);
        else
            m_renderer->setMode(value & 0x20 ? 0 : 1);
        m_renderer->setScreenBank((value & 0xc0) >> 6);
    }
}


// port 86h
void Pk8000Ppi8255Circuit2::setPortC(uint8_t value)
{
    if (m_renderer) {
        m_renderer->setBlanking(!(value & 0x10));
    }
}


bool Pk8000Ppi8255Circuit2::setProperty(const string& propertyName, const EmuValuesList& values)
{
    if (EmuObject::setProperty(propertyName, values))
        return true;

    if (propertyName == "crtRenderer") {
        attachCrtRenderer(static_cast<Pk8000Renderer*>(g_emulation->findObject(values[0].asString())));
        return true;
    }

    return false;
}


// ports A0-BFh
void Pk8000Mode1ColorMem::writeByte(int addr, uint8_t value)
{
    if (m_renderer)
        m_renderer->setColorReg(addr & 0x1F, value);
}


// ports A0-BFh
uint8_t Pk8000Mode1ColorMem::readByte(int addr)
{
    if (m_renderer)
        return m_renderer->getColorReg(addr & 0x1F);
    return 0;
}


bool Pk8000Mode1ColorMem::setProperty(const string& propertyName, const EmuValuesList& values)
{
    if (EmuObject::setProperty(propertyName, values))
        return true;

    if (propertyName == "crtRenderer") {
        attachCrtRenderer(static_cast<Pk8000Renderer*>(g_emulation->findObject(values[0].asString())));
        return true;
    }

    return false;
}


// port 88h
void Pk8000ColorSelector::writeByte(int, uint8_t value)
{
    m_value = value;
    if (m_renderer) {
        m_renderer->setFgColor(value & 0x0F);
        m_renderer->setBgColor(value >> 4);
    }
}


bool Pk8000ColorSelector::setProperty(const string& propertyName, const EmuValuesList& values)
{
    if (EmuObject::setProperty(propertyName, values))
        return true;

    if (propertyName == "crtRenderer") {
        attachCrtRenderer(static_cast<Pk8000Renderer*>(g_emulation->findObject(values[0].asString())));
        return true;
    }

    return false;
}


// port 90h
void Pk8000TxtBufSelector::writeByte(int, uint8_t value)
{
    m_value = value;
    if (m_renderer) {
        m_renderer->setTextBufferBase((value & 0x0F) << 10);
    }
}


bool Pk8000TxtBufSelector::setProperty(const string& propertyName, const EmuValuesList& values)
{
    if (EmuObject::setProperty(propertyName, values))
        return true;

    if (propertyName == "crtRenderer") {
        attachCrtRenderer(static_cast<Pk8000Renderer*>(g_emulation->findObject(values[0].asString())));
        return true;
    }

    return false;
}


// port 91h
void Pk8000SymGenBufSelector::writeByte(int, uint8_t value)
{
    m_value = value;
    if (m_renderer) {
        m_renderer->setSymGenBufferBase((value & 0x0E) << 10);
    }
}


bool Pk8000SymGenBufSelector::setProperty(const string& propertyName, const EmuValuesList& values)
{
    if (EmuObject::setProperty(propertyName, values))
        return true;

    if (propertyName == "crtRenderer") {
        attachCrtRenderer(static_cast<Pk8000Renderer*>(g_emulation->findObject(values[0].asString())));
        return true;
    }

    return false;
}


// port 93h
void Pk8000GrBufSelector::writeByte(int, uint8_t value)
{
    m_value = value;
    if (m_renderer) {
        m_renderer->setGraphicsBufferBase((value & 0x08) << 10);
    }
}


bool Pk8000GrBufSelector::setProperty(const string& propertyName, const EmuValuesList& values)
{
    if (EmuObject::setProperty(propertyName, values))
        return true;

    if (propertyName == "crtRenderer") {
        attachCrtRenderer(static_cast<Pk8000Renderer*>(g_emulation->findObject(values[0].asString())));
        return true;
    }

    return false;
}


// port 92h
void Pk8000ColBufSelector::writeByte(int, uint8_t value)
{
    m_value = value;
    if (m_renderer) {
        m_renderer->setColorBufferBase((value & 0x08) << 10);
    }
}


bool Pk8000ColBufSelector::setProperty(const string& propertyName, const EmuValuesList& values)
{
    if (EmuObject::setProperty(propertyName, values))
        return true;

    if (propertyName == "crtRenderer") {
        attachCrtRenderer(static_cast<Pk8000Renderer*>(g_emulation->findObject(values[0].asString())));
        return true;
    }

    return false;
}


bool Pk8000FileLoader::loadFile(const std::string& fileName, bool run)
{
    static const uint8_t headerSeq[8] = {0x1F, 0xA6, 0xDE, 0xBA, 0xCC, 0x13, 0x7D, 0x74};

    int fileSize;
    uint8_t* buf = palReadFile(fileName, fileSize, false);
    if (!buf)
        return false;

    if (fileSize < 39) {
        delete[] buf;
        return false;
    }

    uint8_t* ptr = buf;

    if (memcmp(ptr, headerSeq, 8) != 0) {
        delete[] buf;
        return false;
    }

    ptr += 8;
    fileSize -= 8;

    if (*ptr == 0xD0) {
        // Binary file
        for (int i = 0; i < 10; i++)
            if (*ptr != 0xD0) {
                delete[] buf;
                return false;
            }

        ptr += 16;
        fileSize -= 16;

        if (*ptr != headerSeq[0]) {
            ptr += 8;
            fileSize -= 8;
        }

        if (fileSize < 15 || memcmp(ptr, headerSeq, 8) != 0) {
            delete[] buf;
            return false;
        }

        ptr += 8;
        fileSize -= 8;

        uint16_t begAddr = (ptr[1] << 8) | ptr[0];
        uint16_t endAddr = (ptr[3] << 8) | ptr[2];
        uint16_t startAddr = (ptr[5] << 8) | ptr[4];

        ptr += 6;
        fileSize -= 6;

        endAddr--; // PK8000 feature?

        uint16_t progLen = endAddr - begAddr + 1;

        if (progLen > fileSize) {
            delete[] buf;
            return false;
        }

        for (unsigned addr = begAddr; addr <= endAddr; addr++)
            m_as->writeByte(addr, *ptr++);

        fileSize -= (endAddr - begAddr + 1);

        // Find next block
    /*    if (m_allowMultiblock && m_tapeRedirector && fileSize > 0)
            while (fileSize > 0 && (*ptr) != 0xE6) {
                ++ptr;
                --fileSize;
            }*/
        //if (fileSize > 0)
        //    --fileSize;

        if (run) {
            m_platform->reset();
            Cpu8080Compatible* cpu = dynamic_cast<Cpu8080Compatible*>(m_platform->getCpu());
            if (cpu) {
                cpu->disableHooks();
                g_emulation->exec((int64_t)cpu->getKDiv() * m_skipTicks);
                cpu->enableHooks();
                cpu->setPC(startAddr);
                /*if (m_allowMultiblock && m_tapeRedirector && fileSize > 0) {
                    m_tapeRedirector->assignFile(fileName, "r");
                    m_tapeRedirector->openFile();
                    m_tapeRedirector->assignFile("", "r");
                    m_tapeRedirector->setFilePos(fullSize - fileSize);
                }*/
            }
        }
    } else if (*ptr == 0xD3) {
        // Basic file
        for (int i = 0; i < 10; i++)
            if (*ptr != 0xD3) {
                delete[] buf;
                return false;
            }

        ptr += 16;
        fileSize -= 16;

        if (*ptr != headerSeq[0]) {
            ptr += 8;
            fileSize -= 8;
        }

        if (fileSize < 10 || memcmp(ptr, headerSeq, 8) != 0) {
            delete[] buf;
            return false;
        }

        ptr += 8;
        fileSize -= 8;

        if (fileSize >= 0xAF00) {
            delete[] buf;
            return false;
        }

        Cpu8080Compatible* cpu = dynamic_cast<Cpu8080Compatible*>(m_platform->getCpu());

        m_as->writeByte(0x4000, 0);
        m_as->writeByte(0x4001, 0);
        m_platform->reset();
        if (cpu) {
            cpu->disableHooks();
            g_emulation->exec((int64_t)cpu->getKDiv() * m_skipTicks);
            cpu->enableHooks();
        }

        uint16_t memLimit = 0x4000 + fileSize + 0x100;

        m_as->writeByte(0x4000, 0);
        for (unsigned addr = 0x4001; fileSize; addr++, fileSize--)
            m_as->writeByte(addr, *ptr++);

        if (cpu) {
            m_as->writeByte(0xF930, memLimit & 0xFF);
            m_as->writeByte(0xF931, memLimit >> 8);
            m_as->writeByte(0xF932, memLimit & 0xFF);
            m_as->writeByte(0xF933, memLimit >> 8);
            m_as->writeByte(0xF934, memLimit & 0xFF);
            m_as->writeByte(0xF935, memLimit >> 8);
            if (run) {
                uint16_t kbdBuf = m_as->readByte(0xFA2C) | (m_as->readByte(0xFA2D) << 8);
                m_as->writeByte(kbdBuf++, 0x52);
                m_as->writeByte(kbdBuf++, 0x55);
                m_as->writeByte(kbdBuf++, 0x4E);
                m_as->writeByte(kbdBuf++, 0x0D);
                m_as->writeByte(0xFA2A, kbdBuf & 0xFF);
                m_as->writeByte(0xFA2B, kbdBuf >> 8);
            }
            //cpu->setSP(0xF7FD);
            cpu->setPC(0x030D);
        }
    }

    return true;
}


Pk8000Keyboard::Pk8000Keyboard()
{
    resetKeys();
}


void Pk8000Keyboard::resetKeys()
{
    for (int i = 0; i < 10; i++)
        m_keys[i] = 0;
    m_rowNo = 0;
}


void Pk8000Keyboard::processKey(EmuKey key, bool isPressed)
{
    if (key == EK_NONE)
        return;

    int i, j;

    // Основная матрица
    for (i = 0; i < 10; i++)
        for (j = 0; j < 8; j++)
            if (key == m_keyMatrix[i][j])
                goto found;
    return;

    found:
    if (isPressed)
        m_keys[i] |= (1 << j);
    else
        m_keys[i] &= ~(1 << j);
}


void Pk8000Keyboard::setMatrixRowNo(uint8_t row)
{
    m_rowNo = row < 10 ? row : 0;
}


uint8_t Pk8000Keyboard::getMatrixRowState()
{
    return ~m_keys[m_rowNo];
}


uint8_t Pk8000InputRegister2::readByte(int)
{
    return g_emulation->getWavReader()->getCurValue() ? 0x80 : 0x00;
}


EmuKey Pk8000KbdLayout::translateKey(PalKeyCode keyCode)
{
    EmuKey key = translateCommonKeys(keyCode);
    if (key != EK_NONE)
        return key;

    switch (keyCode) {
    case PK_KP_1:
        return EK_HOME;
    case PK_PGUP:
        return EK_CLEAR;
    case PK_LCTRL:
    case PK_RCTRL:
        return EK_CTRL;

    case PK_F6:
        return EK_UNDSCR;
    case PK_F10:
        return EK_GRAPH;
    case PK_F8:
        return EK_FIX;
    case PK_F12:
        return EK_STOP;
    case PK_F11:
        return EK_SEL;
    case PK_KP_MUL:
        return EK_INS;
    case PK_KP_DIV:
        return EK_DEL;
    case PK_KP_7:
        return EK_SHOME; //7
    case PK_KP_9:
        return EK_SEND;  // 9
    case PK_KP_3:
        return EK_END;   // 3
    case PK_KP_PERIOD:
        return EK_PHOME; // .
    case PK_KP_0:
        return EK_PEND;  // 0
    case PK_KP_5:
        return EK_MENU;  // 5

    default:
        return translateCommonKeys(keyCode);
    }
}


EmuKey Pk8000KbdLayout::translateUnicodeKey(unsigned unicodeKey, bool& shift, bool& lang)
{
    EmuKey key = translateCommonUnicodeKeys(unicodeKey, shift, lang);
    if (key >= EK_0 && key <= EK_9)
        shift = !shift;
    if (unicodeKey == L'_') {
        key = EK_UNDSCR;
        shift = true;
        lang = false;
    }
    return key;
}


void Pk8000FddControlRegister::writeByte(int, uint8_t value)
{
    m_fdc->setDrive(value & 0x40 ? 1 : 0); // пока так
    m_fdc->setHead((value & 0x10) >> 4);
}


bool Pk8000FddControlRegister::setProperty(const string& propertyName, const EmuValuesList& values)
{
    if (EmuObject::setProperty(propertyName, values))
        return true;

    if (propertyName == "fdc") {
        attachFdc1793(static_cast<Fdc1793*>(g_emulation->findObject(values[0].asString())));
        return true;
    }

    return false;
}


uint8_t Pk8000FdcStatusRegisters::readByte(int addr)
{
    //return (m_bytes[addr & 0x3] & 0x7E) | (m_fdc->getDrq() ? 0 : 1) | (m_fdc->getIrq() ? 0 : 0x80);
    return m_bytes[(m_fdc->getIrq() ? 0x01 : 0) | (m_fdc->getDrq() ? 0x02 : 0)];
}


bool Pk8000FdcStatusRegisters::setProperty(const string& propertyName, const EmuValuesList& values)
{
    if (EmuObject::setProperty(propertyName, values))
        return true;

    if (propertyName == "fdc") {
        attachFdc1793(static_cast<Fdc1793*>(g_emulation->findObject(values[0].asString())));
        return true;
    }

    return false;
}