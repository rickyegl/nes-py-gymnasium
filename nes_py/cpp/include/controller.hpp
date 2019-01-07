//  Program:      nes-py
//  File:         controller.hpp
//  Description:  This class houses the logic and data for an NES controller
//
//  Copyright (c) 2019 Christian Kauten. All rights reserved.
//

#ifndef CONTROLLER_H
#define CONTROLLER_H

#include "common.hpp"

/// A standard NES controller
class Controller {

private:
    /// whether strobe is on
    bool is_strobe;
    /// the emulation of the buttons on the controller
    NES_Byte joypad_buttons;
    /// the state of the buttons
    NES_Byte joypad_bits;

public:
    /// Initialize a new controller
    Controller() : is_strobe(true), joypad_buttons(0), joypad_bits(0) { };

    /// The buttons on the controller
    enum Buttons {
        A,
        B,
        Select,
        Start,
        Up,
        Down,
        Left,
        Right,
        TotalButtons,
    };

    /// Write buttons to the virtual controller
    ///
    /// @param buttons the button bitmap to write to the controller
    ///
    void write_buttons(NES_Byte buttons) { joypad_buttons = buttons; };

    /// Strobe the controller
    void strobe(NES_Byte b);

    /// Read the controller state
    ///
    /// @return a state from the controller
    ///
    NES_Byte read();

};

#endif // CONTROLLER_H
