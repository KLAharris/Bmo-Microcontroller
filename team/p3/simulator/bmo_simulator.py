
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

# Read selected game from the real C Input Handler
dll.simulator_get_selected_game.argtypes = []
dll.simulator_get_selected_game.restype = ctypes.c_int

# Game Lifecycle Manager functions
dll.simulator_is_game_running.argtypes = []
dll.simulator_is_game_running.restype = ctypes.c_int

dll.simulator_get_active_game.argtypes = []
dll.simulator_get_active_game.restype = ctypes.c_int

# Main Menu functions from the real P3 C module
for _name in (
    "simulator_menu_get_screen",
    "simulator_menu_get_category",
    "simulator_menu_get_game",
    "simulator_menu_get_timer",
):
    _func = getattr(dll, _name)
    _func.argtypes = []
    _func.restype = ctypes.c_int

for _name in ("simulator_menu_set_category", "simulator_menu_set_game", "simulator_menu_set_timer"):
    _func = getattr(dll, _name)
    _func.argtypes = [ctypes.c_int]
    _func.restype = None

for _name in ("simulator_menu_open_category", "simulator_menu_show_main"):
    _func = getattr(dll, _name)
    _func.argtypes = []
    _func.restype = None

MENU_CATEGORIES = ("Games", "Timer", "Music")
MENU_SCREENS = ("Main Menu", "Games", "Timer", "Music")
MENU_GAMES = ("Snake", "Simon")
MENU_TIMERS = ("Stopwatch", "Countdown")

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

GAMES = ["SNAKE", "SIMON"]

WIDTH = 480
HEIGHT = 320

# Initialize the simulator
dll.simulator_init()

# Start the simulator clock
start_time = time.monotonic()


def get_time_ms():
    elapsed = time.monotonic() - start_time
    return int(elapsed * 1000) & 0xFFFFFFFF


def get_selected_game():
    game_index = dll.simulator_get_selected_game()

    if 0 <= game_index < len(GAMES):
        return game_index

    return 0


def get_game_status():
    running = bool(dll.simulator_is_game_running())

    if running:
        game_index = dll.simulator_get_active_game()

        if 0 <= game_index < len(GAMES):
            return True, GAMES[game_index]

        return True, "UNKNOWN"

    return False, "NONE"
# Synchronize the initial state timer
dll.simulator_tick(get_time_ms())

root = tk.Tk()
root.title("BMO Simulator - P2 + P3 + Game Lifecycle")
root.configure(bg="#202830")

# Simulated LCD screen
screen_label = tk.Label(root, bg="#202830")
screen_label.pack(padx=20, pady=(20, 10))

# Main Menu Preview: separate from the actual fake LCD framebuffer.
# Buttons below are test controls, NOT physical BMO button assignments.
menu_preview = tk.Frame(root, bg="#24333b", padx=12, pady=10)
menu_preview.pack(fill="x", padx=20, pady=(0, 8))

tk.Label(
    menu_preview,
    text="MAIN MENU PREVIEW (P3 C data / test controls only)",
    bg="#24333b", fg="#bdebdc", font=("Arial", 11, "bold"),
).pack(pady=(0, 6))

menu_status = tk.Label(
    menu_preview, text="", bg="#24333b", fg="white",
    font=("Arial", 12), justify="center",
)
menu_status.pack(pady=4)

menu_controls = tk.Frame(menu_preview, bg="#24333b")
menu_controls.pack(pady=4)


def menu_select_category(index):
    dll.simulator_menu_show_main()
    dll.simulator_menu_set_category(index)
    dll.simulator_menu_open_category()
    refresh_menu_preview()
def menu_select_game(index):
    dll.simulator_menu_set_game(index)
    refresh_menu_preview()


def menu_select_timer(index):
    dll.simulator_menu_set_timer(index)
    refresh_menu_preview()


def refresh_menu_preview():
    screen = dll.simulator_menu_get_screen()
    category = dll.simulator_menu_get_category()
    game = dll.simulator_menu_get_game()
    timer = dll.simulator_menu_get_timer()

    if not (0 <= screen < len(MENU_SCREENS)):
        menu_status.config(text="Invalid menu screen")
        return

    if screen == 0:
        detail = "Choose a category below"
    elif screen == 1:
        detail = "Game selection: " + MENU_GAMES[game] if 0 <= game < len(MENU_GAMES) else "Invalid game"
    elif screen == 2:
        detail = "Timer mode: " + MENU_TIMERS[timer] if 0 <= timer < len(MENU_TIMERS) else "Invalid timer"
    else:
        detail = "Music: P4 app (not connected in preview)"

    menu_status.config(text=f"{MENU_SCREENS[screen]}  |  {detail}")
    

for _i, _label in enumerate(MENU_CATEGORIES):
    tk.Button(
        menu_controls, text=_label, width=10,
        command=lambda i=_i: menu_select_category(i),
    ).grid(row=0, column=_i, padx=3, pady=3)

for _i, _label in enumerate(MENU_GAMES):
    tk.Button(
        menu_controls, text=_label, width=10,
        command=lambda i=_i: menu_select_game(i),
    ).grid(row=1, column=_i, padx=3, pady=3)

for _i, _label in enumerate(MENU_TIMERS):
    tk.Button(
        menu_controls, text=_label, width=10,
        command=lambda i=_i: menu_select_timer(i),
    ).grid(row=2, column=_i, padx=3, pady=3)

tk.Button(
    menu_controls, text="Main Menu", width=12,
    command=lambda: (dll.simulator_menu_show_main(), refresh_menu_preview()),
).grid(row=2, column=2, padx=3, pady=3)

refresh_menu_preview()

# Current state label
state_label = tk.Label(
    root,
    text="",
    font=("Arial", 14),
    bg="#202830",
    fg="white",
)
state_label.pack(pady=(8, 4))
# Game Lifecycle status panel
lifecycle_frame = tk.Frame(root, bg="#202830")
lifecycle_frame.pack(pady=(0, 10))

game_running_label = tk.Label(
    lifecycle_frame,
    text="Game Running: NO",
    font=("Arial", 12, "bold"),
    bg="#202830",
    fg="#ffb86c",
)
game_running_label.pack(pady=2)

active_game_label = tk.Label(
    lifecycle_frame,
    text="Active Game: NONE",
    font=("Arial", 12),
    bg="#202830",
    fg="white",
)
active_game_label.pack(pady=2)

# Game selection panel
game_frame = tk.Frame(root, bg="#202830")
game_frame.pack(pady=(0, 10))

game_title = tk.Label(
    game_frame,
    text="SELECT GAME",
    font=("Arial", 12, "bold"),
    bg="#202830",
    fg="white",
)
game_title.pack(pady=(0, 6))

game_options_frame = tk.Frame(
    game_frame,
    bg="#202830",
)
game_options_frame.pack()

game_labels = []

for game_name in GAMES:
    label = tk.Label(
        game_options_frame,
        text=game_name,
        font=("Arial", 13, "bold"),
        width=13,
        height=3,
        bg="#34414d",
        fg="white",
        relief="solid",
        borderwidth=2,
    )
    label.pack(side=tk.LEFT, padx=8)
    game_labels.append(label)
game_hint = tk.Label(
    game_frame,
    text="LEFT / RIGHT: Choose Game    |    Select: Enter",
    font=("Arial", 10),
    bg="#202830",
    fg="#c6d3dc",
)
game_hint.pack(pady=(8, 0))

# Existing State Machine buttons
button_frame = tk.Frame(root, bg="#202830")
button_frame.pack(pady=10)


def send_event(name):
    # Update timers before processing the event
    dll.simulator_tick(get_time_ms())

    previous_state = dll.simulator_get_state()
    selected_game = GAMES[get_selected_game()]
    was_running, previous_game = get_game_status()

    # Send event to the real C State Machine
    dll.simulator_send_event(EVENTS[name])

    state = dll.simulator_get_state()
    is_running, active_game = get_game_status()

    print(
        f"Event: {name} -> "
        f"State: {STATES[state]}"
    )

    # Verify game start when entering APP from MENU
    if (
        name == "Select"
        and previous_state == STATES.index("MENU")
        and state == STATES.index("APP")
    ):
        print(
            f"Selected Game: {selected_game} -> "
            f"Game Running: {'YES' if is_running else 'NO'} -> "
            f"Active Game: {active_game}"
        )

        if is_running and active_game == selected_game:
            print(
                "Game Lifecycle: START CONFIRMED "
                "(game implementation not connected yet)"
            )
        else:
            print("Game Lifecycle: START CHECK FAILED")

    # Verify game stop when leaving APP
    if (
        previous_state == STATES.index("APP")
        and state != STATES.index("APP")
    ):
        print(
            f"Previous Game: {previous_game} -> "
            f"Game Running: {'YES' if is_running else 'NO'}"
        )

        if was_running and not is_running:
            print("Game Lifecycle: STOP CONFIRMED")
        else:
            print("Game Lifecycle: STOP CHECK FAILED")
for i, name in enumerate(["Wake", "Sleep", "Shake", "Menu", "Select", "Back"]):
    button = tk.Button(
        button_frame,
        text=name,
        font=("Arial", 12),
        width=10,
        command=lambda n=name: send_event(n),
    )
    button.grid(row=i // 3, column=i % 3)
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

    state = dll.simulator_get_state()

    # Read selected game and callback count before input
    before_game = get_selected_game()
    before_count = dll.simulator_get_direction_count()

    # Send direction to the real C Input Handler
    dll.simulator_send_direction(DIRECTIONS[name])

    # Read values after input
    after_game = get_selected_game()
    after_count = dll.simulator_get_direction_count()

    if state == STATES.index("MENU"):
        if after_game != before_game:
            print(
                f"D-Pad: {name} -> "
                f"MENU: {GAMES[before_game]} "
                f"-> {GAMES[after_game]}"
            )
        else:
            print(
                f"D-Pad: {name} -> "
                "MENU: NO CHANGE"
            )

    elif after_count > before_count:
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
            f"FORWARDED TO GAME ({direction_name})"
        )

    else:
        print(
            f"D-Pad: {name} -> "
            f"State: {STATES[state]} -> "
            "IGNORED"
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


def update_game_menu(state):
    selected_game = get_selected_game()

    # Show the selection panel only while in MENU
    if state == STATES.index("MENU"):
        if not game_frame.winfo_manager():
            game_frame.pack(
                after=lifecycle_frame,
                pady=(0, 10),
            )

        for index, label in enumerate(game_labels):
            if index == selected_game:
                label.config(
                    text=f"> {GAMES[index]} <\nSELECTED",
                    bg="#38a878",
                    fg="white",
                    borderwidth=3,
                )
            else:
                label.config(
                    text=GAMES[index],
                    bg="#34414d",
                    fg="white",
                    borderwidth=2,
                )

    else:
        if game_frame.winfo_manager():
            game_frame.pack_forget()


def update_game_lifecycle():
    is_running, active_game = get_game_status()

    game_running_label.config(
        text=f"Game Running: {'YES' if is_running else 'NO'}",
        fg="#38d995" if is_running else "#ffb86c",
    )

    active_game_label.config(
        text=f"Active Game: {active_game}"
    )


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
      # Update Game Lifecycle status
    update_game_lifecycle()

    # Update selected game display
    update_game_menu(state)

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

update_screen()
root.mainloop()
