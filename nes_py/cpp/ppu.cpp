//  Program:      nes-py
//  File:         ppu.cpp
//  Description:  This class houses the logic and data for the PPU of an NES
//
//  Copyright (c) 2019 Christian Kauten. All rights reserved.
//

#include "ppu.hpp"
#include "log.hpp"

void PPU::reset() {
    m_longSprites = m_generateInterrupt = m_greyscaleMode = m_vblank = false;
    m_showBackground = m_showSprites = m_evenFrame = m_firstWrite = true;
    m_bgPage = m_sprPage = Low;
    m_dataAddress = 0;
    m_cycle = 0;
    m_scanline = 0;
    m_spriteDataAddress = 0;
    m_fineXScroll = 0;
    m_tempAddress = 0;
    //m_baseNameTable = 0x2000;
    m_dataAddrIncrement = 1;
    m_pipelineState = PreRender;
    m_scanlineSprites.reserve(8);
    m_scanlineSprites.resize(0);
}

void PPU::cycle(PictureBus& m_bus) {
    switch (m_pipelineState) {
        case PreRender:
            if (m_cycle == 1)
                m_vblank = m_sprZeroHit = false;
            else if (m_cycle == SCANLINE_VISIBLE_DOTS + 2 && m_showBackground && m_showSprites) {
                //Set bits related to horizontal position
                m_dataAddress &= ~0x41f; //Unset horizontal bits
                m_dataAddress |= m_tempAddress & 0x41f; //Copy
            }
            else if (m_cycle > 280 && m_cycle <= 304 && m_showBackground && m_showSprites) {
                //Set vertical bits
                m_dataAddress &= ~0x7be0; //Unset bits related to horizontal
                m_dataAddress |= m_tempAddress & 0x7be0; //Copy
            }
            // if (m_cycle > 257 && m_cycle < 320)
            //     m_spriteDataAddress = 0;
            // if rendering is on, every other frame is one cycle shorter
            if (m_cycle >= SCANLINE_END_CYCLE - (!m_evenFrame && m_showBackground && m_showSprites)) {
                m_pipelineState = Render;
                m_cycle = m_scanline = 0;
            }
            break;
        case Render:
            if (m_cycle > 0 && m_cycle <= SCANLINE_VISIBLE_DOTS) {
                NES_Byte bgColor = 0, sprColor = 0;
                bool bgOpaque = false, sprOpaque = true;
                bool spriteForeground = false;

                int x = m_cycle - 1;
                int y = m_scanline;

                if (m_showBackground) {
                    auto x_fine = (m_fineXScroll + x) % 8;
                    if (!m_hideEdgeBackground || x >= 8) {
                        // fetch tile
                        // mask off fine y
                        auto addr = 0x2000 | (m_dataAddress & 0x0FFF);
                        //auto addr = 0x2000 + x / 8 + (y / 8) * (SCANLINE_VISIBLE_DOTS / 8);
                        NES_Byte tile = m_bus.read(addr);

                        //fetch pattern
                        //Each pattern occupies 16 bytes, so multiply by 16
                        //Add fine y
                        addr = (tile * 16) + ((m_dataAddress >> 12/*y % 8*/) & 0x7);
                        //set whether the pattern is in the high or low page
                        addr |= m_bgPage << 12;
                        //Get the corresponding bit determined by (8 - x_fine) from the right
                        //bit 0 of palette entry
                        bgColor = (m_bus.read(addr) >> (7 ^ x_fine)) & 1;
                        //bit 1
                        bgColor |= ((m_bus.read(addr + 8) >> (7 ^ x_fine)) & 1) << 1;

                        //flag used to calculate final pixel with the sprite pixel
                        bgOpaque = bgColor;

                        //fetch attribute and calculate higher two bits of palette
                        addr = 0x23C0 | (m_dataAddress & 0x0C00) | ((m_dataAddress >> 4) & 0x38)
                                    | ((m_dataAddress >> 2) & 0x07);
                        auto attribute = m_bus.read(addr);
                        int shift = ((m_dataAddress >> 4) & 4) | (m_dataAddress & 2);
                        //Extract and set the upper two bits for the color
                        bgColor |= ((attribute >> shift) & 0x3) << 2;
                    }
                    //Increment/wrap coarse X
                    if (x_fine == 7) {
                        // if coarse X == 31
                        if ((m_dataAddress & 0x001F) == 31) {
                            // coarse X = 0
                            m_dataAddress &= ~0x001F;
                            // switch horizontal nametable
                            m_dataAddress ^= 0x0400;
                        }
                        else
                            // increment coarse X
                            m_dataAddress += 1;
                    }
                }

                if (m_showSprites && (!m_hideEdgeSprites || x >= 8)) {
                    for (auto i : m_scanlineSprites) {
                        NES_Byte spr_x =     m_spriteMemory[i * 4 + 3];

                        if (0 > x - spr_x || x - spr_x >= 8)
                            continue;

                        NES_Byte spr_y     = m_spriteMemory[i * 4 + 0] + 1,
                             tile      = m_spriteMemory[i * 4 + 1],
                             attribute = m_spriteMemory[i * 4 + 2];

                        int length = (m_longSprites) ? 16 : 8;

                        int x_shift = (x - spr_x) % 8, y_offset = (y - spr_y) % length;

                        if ((attribute & 0x40) == 0) //If NOT flipping horizontally
                            x_shift ^= 7;
                        if ((attribute & 0x80) != 0) //IF flipping vertically
                            y_offset ^= (length - 1);

                        NES_Address addr = 0;

                        if (!m_longSprites) {
                            addr = tile * 16 + y_offset;
                            if (m_sprPage == High) addr += 0x1000;
                        }
                        // 8 x 16 sprites
                        else {
                            //bit-3 is one if it is the bottom tile of the sprite, multiply by two to get the next pattern
                            y_offset = (y_offset & 7) | ((y_offset & 8) << 1);
                            addr = (tile >> 1) * 32 + y_offset;
                            addr |= (tile & 1) << 12; //Bank 0x1000 if bit-0 is high
                        }

                        sprColor |= (m_bus.read(addr) >> (x_shift)) & 1; //bit 0 of palette entry
                        sprColor |= ((m_bus.read(addr + 8) >> (x_shift)) & 1) << 1; //bit 1

                        if (!(sprOpaque = sprColor)) {
                            sprColor = 0;
                            continue;
                        }

                        sprColor |= 0x10; //Select sprite palette
                        sprColor |= (attribute & 0x3) << 2; //bits 2-3

                        spriteForeground = !(attribute & 0x20);

                        //Sprite-0 hit detection
                        if (!m_sprZeroHit && m_showBackground && i == 0 && sprOpaque && bgOpaque)
                            m_sprZeroHit = true;

                        break; //Exit the loop now since we've found the highest priority sprite
                    }
                }

                NES_Byte paletteAddr = bgColor;

                if ( (!bgOpaque && sprOpaque) || (bgOpaque && sprOpaque && spriteForeground) )
                    paletteAddr = sprColor;
                else if (!bgOpaque && !sprOpaque)
                    paletteAddr = 0;

                screen_buffer[y][x] = colors[m_bus.read_palette(paletteAddr)];

            }
            else if (m_cycle == SCANLINE_VISIBLE_DOTS + 1 && m_showBackground) {
                //Shamelessly copied from nesdev wiki
                if ((m_dataAddress & 0x7000) != 0x7000)  // if fine Y < 7
                    m_dataAddress += 0x1000;              // increment fine Y
                else {
                    m_dataAddress &= ~0x7000;             // fine Y = 0
                    int y = (m_dataAddress & 0x03E0) >> 5;    // let y = coarse Y
                    if (y == 29) {
                        y = 0;                                // coarse Y = 0
                        m_dataAddress ^= 0x0800;              // switch vertical nametable
                    }
                    else if (y == 31)
                        y = 0;                                // coarse Y = 0, nametable not switched
                    else
                        y += 1;                               // increment coarse Y
                    m_dataAddress = (m_dataAddress & ~0x03E0) | (y << 5);
                                                            // put coarse Y back into m_dataAddress
                }
            }
            else if (m_cycle == SCANLINE_VISIBLE_DOTS + 2 && m_showBackground && m_showSprites) {
                //Copy bits related to horizontal position
                m_dataAddress &= ~0x41f;
                m_dataAddress |= m_tempAddress & 0x41f;
            }

//                 if (m_cycle > 257 && m_cycle < 320)
//                     m_spriteDataAddress = 0;

            if (m_cycle >= SCANLINE_END_CYCLE) {
                //Find and index sprites that are on the next Scanline
                //This isn't where/when this indexing, actually copying in 2C02 is done
                //but (I think) it shouldn't hurt any games if this is done here

                m_scanlineSprites.resize(0);

                int range = 8;
                if (m_longSprites)
                    range = 16;

                NES_Byte j = 0;
                for (NES_Byte i = m_spriteDataAddress / 4; i < 64; ++i) {
                    auto diff = (m_scanline - m_spriteMemory[i * 4]);
                    if (0 <= diff && diff < range) {
                        m_scanlineSprites.push_back(i);
                        if (++j >= 8)
                            break;
                    }
                }

                ++m_scanline;
                m_cycle = 0;
            }

            if (m_scanline >= VISIBLE_SCANLINES)
                m_pipelineState = PostRender;

            break;
        case PostRender:
            if (m_cycle >= SCANLINE_END_CYCLE) {
                ++m_scanline;
                m_cycle = 0;
                m_pipelineState = VerticalBlank;
            }

            break;
        case VerticalBlank:
            if (m_cycle == 1 && m_scanline == VISIBLE_SCANLINES + 1) {
                m_vblank = true;
                if (m_generateInterrupt) m_vblankCallback();
            }

            if (m_cycle >= SCANLINE_END_CYCLE) {
                ++m_scanline;
                m_cycle = 0;
            }

            if (m_scanline >= FRAME_END_SCANLINE) {
                m_pipelineState = PreRender;
                m_scanline = 0;
                m_evenFrame = !m_evenFrame;
                // m_vblank = false;
            }

            break;
        default:
            LOG(Error) << "Well, this shouldn't have happened." << std::endl;
    }

    ++m_cycle;
}

void PPU::doDMA(const NES_Byte* page_ptr) {
    std::memcpy(m_spriteMemory.data() + m_spriteDataAddress, page_ptr, 256 - m_spriteDataAddress);
    if (m_spriteDataAddress)
        std::memcpy(m_spriteMemory.data(), page_ptr + (256 - m_spriteDataAddress), m_spriteDataAddress);
}

void PPU::control(NES_Byte ctrl) {
    m_generateInterrupt = ctrl & 0x80;
    m_longSprites = ctrl & 0x20;
    m_bgPage = static_cast<CharacterPage>(!!(ctrl & 0x10));
    m_sprPage = static_cast<CharacterPage>(!!(ctrl & 0x8));
    if (ctrl & 0x4)
        m_dataAddrIncrement = 0x20;
    else
        m_dataAddrIncrement = 1;
    //m_baseNameTable = (ctrl & 0x3) * 0x400 + 0x2000;

    //Set the nametable in the temp address, this will be reflected in the data address during rendering
    m_tempAddress &= ~0xc00;                 //Unset
    m_tempAddress |= (ctrl & 0x3) << 10;     //Set according to ctrl bits
}

void PPU::setMask(NES_Byte mask) {
    m_greyscaleMode = mask & 0x1;
    m_hideEdgeBackground = !(mask & 0x2);
    m_hideEdgeSprites = !(mask & 0x4);
    m_showBackground = mask & 0x8;
    m_showSprites = mask & 0x10;
}

NES_Byte PPU::getStatus() {
    NES_Byte status = m_sprZeroHit << 6 | m_vblank << 7;
    //m_dataAddress = 0;
    m_vblank = false;
    m_firstWrite = true;
    return status;
}

void PPU::setDataAddress(NES_Byte addr) {
    //m_dataAddress = ((m_dataAddress << 8) & 0xff00) | addr;
    if (m_firstWrite) {
        //Unset the upper byte
        m_tempAddress &= ~0xff00;
        m_tempAddress |= (addr & 0x3f) << 8;
        m_firstWrite = false;
    }
    else {
        //Unset the lower byte;
        m_tempAddress &= ~0xff;
        m_tempAddress |= addr;
        m_dataAddress = m_tempAddress;
        m_firstWrite = true;
    }
}

NES_Byte PPU::getData(PictureBus& m_bus) {
    auto data = m_bus.read(m_dataAddress);
    m_dataAddress += m_dataAddrIncrement;

    // Reads are delayed by one byte/read when address is in this range
    if (m_dataAddress < 0x3f00)
        //Return from the data buffer and store the current value in the buffer
        std::swap(data, m_dataBuffer);

    return data;
}

void PPU::setData(PictureBus& m_bus, NES_Byte data) {
    m_bus.write(m_dataAddress, data);
    m_dataAddress += m_dataAddrIncrement;
}

void PPU::setScroll(NES_Byte scroll) {
    if (m_firstWrite) {
        m_tempAddress &= ~0x1f;
        m_tempAddress |= (scroll >> 3) & 0x1f;
        m_fineXScroll = scroll & 0x7;
        m_firstWrite = false;
    }
    else {
        m_tempAddress &= ~0x73e0;
        m_tempAddress |= ((scroll & 0x7) << 12) |
                         ((scroll & 0xf8) << 2);
        m_firstWrite = true;
    }
}
