"""
Module for programmatic inputs.

Currently only for GameCube and Wiimote buttons,
no Wii IR (pointing), acceleration or extensions data yet.
"""
from typing import TypedDict


class GCInputs(TypedDict):
    Left: bool
    Right: bool
    Down: bool
    Up: bool
    Z: bool
    R: bool
    L: bool
    A: bool
    B: bool
    X: bool
    Y: bool
    Start: bool
    StickX: int  # 0-255, 128 is neutral
    StickY: int  # 0-255, 128 is neutral
    CStickX: int  # 0-255, 128 is neutral
    CStickY: int  # 0-255, 128 is neutral
    TriggerLeft: int  # 0-255
    TriggerRight: int  # 0-255
    AnalogA: int  # 0-255
    AnalogB: int  # 0-255
    Connected: bool


class WiiInputs(TypedDict):
    Left: bool
    Right: bool
    Down: bool
    Up: bool
    Plus: bool
    Minus: bool
    One: bool
    Two: bool
    A: bool
    B: bool
    Home: bool


def get_gc_buttons(controller_id: int, /) -> GCInputs:
    """
    Retrieves the current input map for the given GameCube controller.
    :param controller_id: 0-based index of the controller
    :return: dictionary describing the current input map
    """


def set_gc_buttons(controller_id: int, inputs: GCInputs, /):
    """
    Sets the current input map for the given GameCube controller.
    The override will hold for the current frame.
    :param controller_id: 0-based index of the controller
    :param inputs: dictionary describing the input map
    """


def get_wii_buttons(controller_id: int, /) -> WiiInputs:
    """
    Retrieves the current input map for the given Wii controller.
    :param controller_id: 0-based index of the controller
    :return: dictionary describing the current input map
    """


def set_wii_buttons(controller_id: int, inputs: WiiInputs, /):
    """
    Sets the current input map for the given Wii controller.
    The override will hold for the current frame.
    :param controller_id: 0-based index of the controller
    :param inputs: dictionary describing the input map
    """
