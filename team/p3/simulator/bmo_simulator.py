
import ctypes
import pathlib
import time
import tkinter as tk
from PIL import Image, ImageTk

# Load the real P2 + P3 C code
DLL_PATH = pathlib.Path(__file__).with_name("bmo_simulator.dll")
dll = ctypes.CDLL(str(DLL_PATH.resolve()))

# State Machine functions
dll.simulator_init.argtypes = []
dll.simulator_init.restype = None

dll.simulator_send_event.argtypes = [ctypes.c_int]
dll.simulator_send_event.restype = None

dll.simulator_get_state.argtypes = []
dll.simulator_get_state.restype = ctypes.c_int

dll.simulator_get_face.argtypes = []
dll.simulator_get_face.restype = ctypes.c_int

dll.simulator_tick.argtypes = [ctypes.c_uint32]
dll.simulator_tick.restype = None

dll.simulator_get_screen.argtypes = []
dll.simulator_get_screen.restype = ctypes.POINTER(ctypes.c_uint16)

# D-Pad Input Handler functions
dll.simulator_send_direction.argtypes = [ctypes.c_int]
dll.simulator_send_direction.restype = None

dll.simulator_get_last_direction.argtypes = []
dll.simulator_get_last_direction.restype = ctypes.c_int

dll.simulator_get_direction_count.argtypes = []
dll.simulator_get_direction_count.restype = ctypes.c_uint32

# Values from bmo_state.h
STATES = ["SLEEP", "IDLE", "REACTING", "MENU", "APP"]

EVENTS = {
    "Wake": 0,
    "Sleep": 1,
    "Menu": 2,
    "Select": 3,
    "Back": 4,
    "Shake": 5,
    "Timeout": 6,
}

# Values from bmo_input.h
DIRECTIONS = {
    "UP": 0,
    "DOWN": 1,
    "LEFT": 2,
    "RIGHT": 3,
}

WIDTH = 480
HEIGHT = 320

# Initialize the simulator
dll.simulator_init()

# Start the simulator clock
start_time = time.monotonic()


def get_time_ms():
    elapsed = time.monotonic() - start_time
    return int(elapsed * 1000) & 0xFFFFFFFF


# Synchronize the initial state timer
dll.simulator_tick(get_time_ms())

root = tk.Tk()
root.title("BMO Simulator - P2 + P3 + D-Pad")
root.configure(bg="#202830")

# Simulated LCD screen
screen_label = tk.Label(root, bg="#202830")
screen_label.pack(padx=20, pady=(20, 10))

# Current state label
state_label = tk.Label(
    root,
    text="",
    font=("Arial", 14),
    bg="#202830",
    fg="white",
)
state_label.pack(pady=8)

# Existing State Machine buttons
button_frame = tk.Frame(root, bg="#202830")
button_frame.pack(pady=10)


def send_event(name):
    # Update timers before processing the event
    dll.simulator_tick(get_time_ms())

    # Send event to the real C State Machine
    dll.simulator_send_event(EVENTS[name])

    state = dll.simulator_get_state()

    print(
        f"Event: {name} -> "
        f"State: {STATES[state]}"
    )


for i, name in enumerate(
    ["Wake", "Sleep", "Shake", "Menu", "Select", "Back"]
):
    button = tk.Button(
        button_frame,
        text=name,
        font=("Arial", 12),
        width=10,
        command=lambda n=name: send_event(n),
    )
    button.grid(
        row=i // 3,
        column=i % 3,
        padx=8,
        pady=8,
    )

# D-Pad controls
dpad_frame = tk.Frame(root, bg="#202830")
dpad_frame.pack(pady=(5, 15))

dpad_title = tk.Label(
    dpad_frame,
    text="D-Pad Controls",
    font=("Arial", 12, "bold"),
    bg="#202830",
    fg="white",
)
dpad_title.grid(
    row=0,
    column=0,
    columnspan=3,
    pady=5,
)


def send_direction(name):
    # Update state timers
    dll.simulator_tick(get_time_ms())

    # Read callback count before sending input
    before = dll.simulator_get_direction_count()

    # Send direction to the real C Input Handler
    dll.simulator_send_direction(DIRECTIONS[name])

    # Read callback count after sending input
    after = dll.simulator_get_direction_count()
    state = dll.simulator_get_state()

    if after > before:
        result = "FORWARDED TO GAME"

        last_direction = dll.simulator_get_last_direction()

        direction_name = next(
            (
                key
                for key, value in DIRECTIONS.items()
                if value == last_direction
            ),
            "UNKNOWN",
        )

        print(
            f"D-Pad: {name} -> "
            f"State: {STATES[state]} -> "
            f"{result} ({direction_name})"
        )
    else:
        print(
            f"D-Pad: {name} -> "
            f"State: {STATES[state]} -> "
            f"IGNORED"
        )


dpad_buttons = [
    ("UP", 1, 1),
    ("LEFT", 2, 0),
    ("DOWN", 2, 1),
    ("RIGHT", 2, 2),
]

for name, row, column in dpad_buttons:
    button = tk.Button(
        dpad_frame,
        text=name,
        font=("Arial", 11, "bold"),
        width=8,
        command=lambda n=name: send_direction(n),
    )
    button.grid(
        row=row,
        column=column,
        padx=4,
        pady=4,
    )


# The LCD uses RGB565 pixels
def rgb565_to_rgb(pixel):
    r = ((pixel >> 11) & 0x1F) * 255 // 31
    g = ((pixel >> 5) & 0x3F) * 255 // 63
    b = (pixel & 0x1F) * 255 // 31
    return (r, g, b)


def update_screen():
    # Get real elapsed time
    now_ms = get_time_ms()

    # Update P3 timers and P2 face animation
    dll.simulator_tick(now_ms)

    # Display current state
    state = dll.simulator_get_state()

    state_label.config(
        text=f"Current State: {STATES[state]}"
    )

    # Read the simulated LCD framebuffer
    screen = dll.simulator_get_screen()

    # Convert RGB565 framebuffer to RGB image
    pixels = [
        rgb565_to_rgb(screen[i])
        for i in range(WIDTH * HEIGHT)
    ]

    image = Image.new("RGB", (WIDTH, HEIGHT))
    image.putdata(pixels)

    photo = ImageTk.PhotoImage(image)

    screen_label.config(image=photo)
    screen_label.image = photo

    # Schedule the next screen update
    root.after(33, update_screen)


update_screen()
root.mainloop()
