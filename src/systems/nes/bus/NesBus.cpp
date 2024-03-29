//
// Created by junds on 09-09-2021.
//

#include "NesBus.h"

NesBus::NesBus() : platform("WDNES", 256, 240, &controllerBuffer), cpu(), ppu() {
    cartridge = nullptr;
    cpu.connectBus(this);
    ppu.connectPlatform(&platform);
    ram.resize(0x0800);
    ioRegisters.resize(0x0020);
    expansionROM.resize(0x1FE0);
    sram.resize(0x2000);
    patternTables.resize(0x2000);
    nameTables.resize(0x0F00);
    palettes.resize(0x0020);
    std::fill(controllerBuffer.begin(), controllerBuffer.end(), 0x00);
}

NesBus::~NesBus() = default;

void NesBus::loadCartridge(std::shared_ptr<Cartridge> const& newCartridge) {
    cartridge = newCartridge;
    ppu.loadCartridge(newCartridge);
}

void NesBus::reset() {
    cartridge->reset();
    cpu.reset();
    ppu.reset();
    totalCyclesPerformed = 0;
}

void NesBus::powerUp() {
    bool quit = false;
    std::chrono::steady_clock::time_point bgn = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();

    float const msTimeGap = (1.0 / 30.0) * 1000;    // Chose duration (in milliseconds) of a frame at constant 30 fps
    while (!quit) {
        clock();
        end = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(end - bgn).count() >= msTimeGap) {
            quit = platform.processInput();
            bgn = end;
        }
    }
}

auto isControllerAddress = [](uint16_t const& address) {
    return address == 0x4016 || address == 0x4017;
};

auto isDmaAddress = [](uint16_t const& address) {
    return address == 0x4014;
};

uint8_t NesBus::readCPUMemory(uint16_t address) {
    if (address < 0x2000)
        return ram[address % 0x0800];

    if (address < 0x4000)
        return ppu.readIO(address);

    if (address < 0x4020) {
        auto const& i = address % 0x0020;
        if (isControllerAddress(address)) {
            auto controllerState = ioRegisters[i];
            ioRegisters[i] = (controllerState << 1);
            return controllerState >> 7;
        } else {
            return ioRegisters[i];
        }
    }

    if (address < 0x6000)
        return expansionROM[address % 0x1FE0];

    if (address < 0x8000)
        return sram[address % 0x2000];

    return cartridge->readPRGMemory(address);
}

void NesBus::writeCPUMemory(uint16_t address, uint8_t value) {
    if (address < 0x2000)
        ram[address % 0x0800] = value;

    else if (address < 0x4000)
        ppu.writeIO(address, value);

    else if (address < 0x4020) {
        if (isDmaAddress(address)) {
            dma.page = value;
            dma.address = 0x00;
            dma.transfer = true;
        } else {
            ioRegisters[address % 0x0020] = isControllerAddress(address) ? controllerBuffer[address % 0x0002] : value;
        }
    }

    else if (address < 0x6000)
        expansionROM[address % 0x1FE0] = value;

    else if (address < 0x8000)
        sram[address % 0x2000] = value;

    else
        cartridge->writePRGMemory(address, value);
}

void NesBus::clock() {
    ppu.clock();
    if (totalCyclesPerformed % 3 == 0) {
        if (dma.transfer) {
            if (dma.dummy) {
                // Wait for clock sync
                if (totalCyclesPerformed % 2 == 1) {
                    dma.dummy = false;
                }
            } else {
                if (totalCyclesPerformed % 2 == 0) {
                    dma.data = readCPUMemory((dma.page << 8) | dma.address);
                } else {
                    ppu.oamp[dma.address++] = dma.data;
                    // Only 256 fetch are performed so once address (uint8_t) reaches zero a wrap around has occurred which means 256 bytes have been read and written
                    if (dma.address == 0x00) {
                        dma.transfer = false;
                        dma.dummy = true;
                    }
                }
            }
        } else {
            cpu.clock();
        }
    }

    if (ppu.nmi) {
        ppu.nmi = false;
        cpu.nmi();
    }

    totalCyclesPerformed++;
}
