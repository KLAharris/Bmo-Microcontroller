
import ctypes
import pathlib
import time
import tkinter as tk
from PIL import Image, ImageTk

# Load the real P2 + P3 C code
DLL_PATH = pathlib.Path(__file__).with_name("bmo_simulator.dll")
dll = ctypes.CDLL(str(DLL_PATH.resolve()))

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
root.title("BMO Simulator - P2 + P3")
root.configure(bg="#202830")

screen_label = tk.Label(root, bg="#202830")
screen_label.pack(padx=20, pady=(20, 10))

state_label = tk.Label(
    root,
    text="",
    font=("Arial", 14),
    bg="#202830",
    fg="white",
)
state_label.pack(pady=8)

button_frame = tk.Frame(root, bg="#202830")
button_frame.pack(pady=15)

def send_event(name):
    # Update time before processing the event
    dll.simulator_tick(get_time_ms())

    dll.simulator_send_event(EVENTS[name])

    print(
        f"Event: {name} -> "
        f"State: {STATES[dll.simulator_get_state()]}"
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
        pady=8
    )

# The LCD uses RGB565 pixels
def rgb565_to_rgb(pixel):
    r = ((pixel >> 11) & 0x1F) * 255 // 31
    g = ((pixel >> 5) & 0x3F) * 255 // 63
    b = (pixel & 0x1F) * 255 // 31
    return (r, g, b)

def update_screen():
    # Get real elapsed time in milliseconds
    now_ms = get_time_ms()

    # Update P3 timers and P2 face animation
    dll.simulator_tick(now_ms)

    # Display the current state
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
