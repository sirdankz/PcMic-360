from __future__ import annotations

import json
import os
import queue
import socket
import subprocess
import sys
import threading
import time
from pathlib import Path


def _load_audio_deps():
    try:
        import numpy as np  # type: ignore
        import sounddevice as sd  # type: ignore
        return np, sd
    except Exception:
        # Keep the same easy first-run behavior as the proven v7.02/v7.03 host.
        subprocess.check_call([
            sys.executable, "-m", "pip", "install",
            "--disable-pip-version-check", "numpy", "sounddevice"
        ])
        import numpy as np  # type: ignore
        import sounddevice as sd  # type: ignore
        return np, sd


np, sd = _load_audio_deps()

import tkinter as tk
from tkinter import ttk, messagebox


APP_NAME = "PcMic-360"
APP_VERSION = "GUI 1.5 • XEX v7.04"
RATE = 16000
SAMPLES = 320
BYTES = 640
MAGIC = b"\xA5\x70"
DEFAULT_PORT = 36000
DEFAULT_GATE = 220
DEFAULT_HANGOVER_MS = 180
GATE_MAX = 2000
DEFAULT_INPUT_LEVEL = 100.0
DEFAULT_GAIN = 1.0
AUDIO_FRESH_MS = 120
AUDIO_QUEUE_MAX = 3
STALE_FRAME_MS = 80
SEND_GAP_WARN_MS = 40
CAPTURE_JITTER_WARN_MS = 12

# Theme: the same dark navy / charcoal family used in the other 360 utilities.
C = {
    "bg": "#0B111B",
    "sidebar": "#0E1623",
    "panel": "#141C28",
    "panel2": "#182231",
    "border": "#263347",
    "border2": "#33445E",
    "text": "#F2F6FC",
    "subtext": "#98A6B8",
    "muted_text": "#718096",
    "accent": "#3B82F6",
    "accent_hover": "#5595F7",
    "accent_dim": "#1D4E89",
    "good": "#37C88A",
    "warn": "#F0B84B",
    "bad": "#E75A63",
    "track": "#202C3C",
    "level": "#4F91F2",
}


class GateSlider(tk.Canvas):
    """Small custom slider so the gate control matches the rest of the UI."""

    def __init__(self, master, value=DEFAULT_GATE, minimum=0, maximum=GATE_MAX,
                 command=None, **kwargs):
        super().__init__(master, height=34, bg=C["panel"], highlightthickness=0, **kwargs)
        self.minimum = minimum
        self.maximum = maximum
        self.value = float(value)
        self.command = command
        self.pad = 11
        self.bind("<Configure>", lambda _e: self.redraw())
        self.bind("<Button-1>", self._event_set)
        self.bind("<B1-Motion>", self._event_set)

    def _event_set(self, event):
        width = max(1, self.winfo_width() - self.pad * 2)
        frac = min(1.0, max(0.0, (event.x - self.pad) / width))
        raw = self.minimum + frac * (self.maximum - self.minimum)
        # 10 PCM RMS unit steps are precise enough while keeping a clean UI.
        self.value = round(raw / 10.0) * 10.0
        self.redraw()
        if self.command:
            self.command(int(self.value))

    def set(self, value):
        self.value = min(self.maximum, max(self.minimum, float(value)))
        self.redraw()

    def get(self):
        return int(self.value)

    def redraw(self):
        self.delete("all")
        w = max(1, self.winfo_width())
        y = 17
        x0 = self.pad
        x1 = w - self.pad
        usable = max(1, x1 - x0)
        frac = (self.value - self.minimum) / max(1, self.maximum - self.minimum)
        knob_x = x0 + usable * frac

        self.create_line(x0, y, x1, y, fill=C["track"], width=8, capstyle=tk.ROUND)
        if knob_x > x0:
            self.create_line(x0, y, knob_x, y, fill=C["accent"], width=8, capstyle=tk.ROUND)
        self.create_oval(knob_x - 7, y - 7, knob_x + 7, y + 7,
                         fill=C["text"], outline=C["accent"], width=3)


class ValueSlider(tk.Canvas):
    """Reusable themed slider for input level and digital microphone gain."""

    def __init__(self, master, value, minimum, maximum, step, command=None, **kwargs):
        super().__init__(master, height=34, bg=C["panel"], highlightthickness=0, **kwargs)
        self.minimum = float(minimum)
        self.maximum = float(maximum)
        self.step = float(step)
        self.value = float(value)
        self.command = command
        self.pad = 11
        self.bind("<Configure>", lambda _e: self.redraw())
        self.bind("<Button-1>", self._event_set)
        self.bind("<B1-Motion>", self._event_set)

    def _quantize(self, value):
        value = min(self.maximum, max(self.minimum, float(value)))
        if self.step > 0:
            value = round((value - self.minimum) / self.step) * self.step + self.minimum
        return min(self.maximum, max(self.minimum, value))

    def _event_set(self, event):
        width = max(1, self.winfo_width() - self.pad * 2)
        frac = min(1.0, max(0.0, (event.x - self.pad) / width))
        self.value = self._quantize(self.minimum + frac * (self.maximum - self.minimum))
        self.redraw()
        if self.command:
            self.command(self.value)

    def set(self, value):
        self.value = self._quantize(value)
        self.redraw()

    def get(self):
        return float(self.value)

    def redraw(self):
        self.delete("all")
        w = max(1, self.winfo_width())
        y = 17
        x0 = self.pad
        x1 = w - self.pad
        usable = max(1, x1 - x0)
        frac = (self.value - self.minimum) / max(1e-9, self.maximum - self.minimum)
        knob_x = x0 + usable * frac

        self.create_line(x0, y, x1, y, fill=C["track"], width=8, capstyle=tk.ROUND)
        if knob_x > x0:
            self.create_line(x0, y, knob_x, y, fill=C["accent"], width=8, capstyle=tk.ROUND)
        self.create_oval(knob_x - 7, y - 7, knob_x + 7, y + 7,
                         fill=C["text"], outline=C["accent"], width=3)


class LevelMeter(tk.Canvas):
    def __init__(self, master, **kwargs):
        super().__init__(master, height=16, bg=C["panel"], highlightthickness=0, **kwargs)
        self.level = 0.0
        self.gate = DEFAULT_GATE
        self.max_level = GATE_MAX
        self.bind("<Configure>", lambda _e: self.redraw())

    def set_values(self, level, gate):
        self.level = max(0.0, float(level))
        self.gate = max(0.0, float(gate))
        self.redraw()

    def redraw(self):
        self.delete("all")
        w = max(2, self.winfo_width())
        h = max(2, self.winfo_height())
        self.create_rectangle(0, 3, w, h - 3, fill=C["track"], outline="")
        frac = min(1.0, self.level / max(1.0, self.max_level))
        self.create_rectangle(0, 3, int(w * frac), h - 3, fill=C["level"], outline="")
        if self.gate > 0:
            gx = min(w - 1, int(w * min(1.0, self.gate / self.max_level)))
            self.create_line(gx, 1, gx, h - 1, fill=C["warn"], width=2)


class PcMic360GUI:
    def __init__(self, root: tk.Tk):
        self.root = root
        self.root.title(APP_NAME)
        self.root.geometry("1120x960")
        self.root.minsize(980, 860)
        self.root.configure(bg=C["bg"])
        self._apply_window_icon()

        self.shutdown = threading.Event()
        self.want_connected = threading.Event()
        self.connected = threading.Event()
        self.banner_seen = threading.Event()
        self.installed = threading.Event()
        self.live_active = threading.Event()
        self.lane_locked = threading.Event()
        self.safe_to_unload = threading.Event()
        self.restore_pending = threading.Event()

        self.client_socket = None
        self.client_addr = None
        self.send_lock = threading.Lock()
        self.audio_queue = queue.Queue(maxsize=AUDIO_QUEUE_MAX)
        self.ui_queue = queue.Queue()

        self.audio_stream = None
        self.capture_thread = None
        self.capture_stop = threading.Event()
        self.sender_thread_started = False
        self.sent = 0
        self.drops = 0
        self.stale_frame_drops = 0
        self.capture_overflows = 0
        self.capture_jitter_events = 0
        self.send_gap_events = 0
        self.max_capture_jitter_ms = 0.0
        self.max_send_age_ms = 0.0
        self.max_send_gap_ms = 0.0
        self._last_capture_tick = 0.0
        self._last_send_tick = 0.0
        self.rms = 0.0
        self.gate_open = False
        self.gate_until = 0.0
        self.gate_threshold = DEFAULT_GATE
        self.input_level = DEFAULT_INPUT_LEVEL
        self.gain = DEFAULT_GAIN
        self.saved_device_name = ""
        self.saved_xbox_ip = ""
        self._load_settings()
        self.hangover_ms = DEFAULT_HANGOVER_MS
        self.base_muted = False
        self.hold_active = False
        self._keys_down = set()

        self.last_status = ""
        self.last_resolve = ""
        self.last_stop = ""
        self.status_fields = {}
        self.headset_resolved = False
        self.virtual_headset = False

        self.device_map = {}
        self.selected_device = None
        self.port = DEFAULT_PORT
        self.connection_thread_started = False
        self.global_hotkey_thread_started = False
        self.auto_install_scheduled = False

        self._make_styles()
        self._build_ui()
        self._refresh_devices()
        self._bind_hotkeys()

        self.root.protocol("WM_DELETE_WINDOW", self._on_close)
        self.root.after(50, self._process_ui_queue)
        self.root.after(60, self._refresh_live_ui)

        self._start_connection_thread()
        self._start_global_hotkey_thread()
        self._start_sender_thread()
        self._log("PcMic-360 ready. Enter the Xbox IP and click CONNECT. The voice hook installs automatically.", "info")

    # ---------- app resources / settings ----------
    @staticmethod
    def _resource_path(name):
        base = Path(getattr(sys, "_MEIPASS", Path(__file__).resolve().parent))
        return base / name

    def _apply_window_icon(self):
        try:
            icon = self._resource_path("PcMic-360.ico")
            if icon.exists():
                self.root.iconbitmap(default=str(icon))
        except Exception:
            pass

    @staticmethod
    def _settings_path():
        root = Path(os.environ.get("APPDATA", str(Path.home()))) / "PcMic-360"
        return root / "gui_settings.json"

    def _load_settings(self):
        try:
            p = self._settings_path()
            if not p.exists():
                legacy = Path(os.environ.get("APPDATA", str(Path.home()))) / "Xbox360VoiceUniversal" / "gui_settings.json"
                if legacy.exists():
                    p = legacy
                else:
                    return
            data = json.loads(p.read_text(encoding="utf-8"))
            gate = int(data.get("gate_rms", DEFAULT_GATE))
            self.gate_threshold = max(0, min(GATE_MAX, gate))
            self.input_level = max(0.0, min(100.0, float(data.get("input_level", DEFAULT_INPUT_LEVEL))))
            self.gain = max(0.0, min(4.0, float(data.get("mic_gain", DEFAULT_GAIN))))
            self.saved_device_name = str(data.get("device_name", ""))
            self.saved_xbox_ip = str(data.get("xbox_ip", ""))
        except Exception:
            self.gate_threshold = DEFAULT_GATE
            self.input_level = DEFAULT_INPUT_LEVEL
            self.gain = DEFAULT_GAIN
            self.saved_device_name = ""
            self.saved_xbox_ip = ""

    def _save_settings(self):
        try:
            p = self._settings_path()
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_text(json.dumps({
                "gate_rms": int(self.gate_threshold),
                "input_level": round(float(self.input_level), 1),
                "mic_gain": round(float(self.gain), 2),
                "device_name": self.saved_device_name,
                "xbox_ip": self.saved_xbox_ip,
            }, indent=2), encoding="utf-8")
        except Exception:
            pass

    # ---------- visual helpers ----------
    def _make_styles(self):
        style = ttk.Style()
        try:
            style.theme_use("clam")
        except Exception:
            pass
        style.configure("Dark.TCombobox",
                        fieldbackground=C["panel2"], background=C["panel2"],
                        foreground=C["text"], arrowcolor=C["subtext"],
                        bordercolor=C["border"], lightcolor=C["border"],
                        darkcolor=C["border"], padding=7)
        style.map("Dark.TCombobox",
                  fieldbackground=[("readonly", C["panel2"])],
                  selectbackground=[("readonly", C["panel2"])],
                  selectforeground=[("readonly", C["text"])])

    def _label(self, master, text, size=10, color=None, bold=False, **kwargs):
        return tk.Label(master, text=text, bg=master.cget("bg"), fg=color or C["text"],
                        font=("Segoe UI", size, "bold" if bold else "normal"), **kwargs)

    def _card(self, master, **kwargs):
        return tk.Frame(master, bg=C["panel"], highlightbackground=C["border"],
                        highlightthickness=1, bd=0, **kwargs)

    def _button(self, master, text, command, kind="secondary", width=None):
        palettes = {
            "primary": (C["accent"], C["accent_hover"], C["text"]),
            "secondary": (C["panel2"], C["border2"], C["text"]),
            "danger": ("#6A2730", "#7F303A", C["text"]),
            "good": ("#1D5D48", "#25715A", C["text"]),
        }
        bg, active, fg = palettes[kind]
        b = tk.Button(master, text=text, command=command, bg=bg, fg=fg,
                      activebackground=active, activeforeground=fg,
                      disabledforeground=C["muted_text"], relief=tk.FLAT,
                      bd=0, padx=14, pady=9, cursor="hand2",
                      font=("Segoe UI", 9, "bold"), width=width)
        return b

    def _pill(self, master, text, fg, bg):
        return tk.Label(master, text=text, bg=bg, fg=fg,
                        font=("Segoe UI", 8, "bold"), padx=9, pady=4)

    def _section_title(self, master, title, subtitle=None):
        wrap = tk.Frame(master, bg=C["panel"])
        self._label(wrap, title, 11, bold=True).pack(anchor="w")
        if subtitle:
            self._label(wrap, subtitle, 8, C["subtext"]).pack(anchor="w", pady=(2, 0))
        return wrap

    def _build_ui(self):
        # Header
        header = tk.Frame(self.root, bg=C["sidebar"], height=72,
                          highlightbackground=C["border"], highlightthickness=0)
        header.pack(fill="x")
        header.pack_propagate(False)

        brand = tk.Frame(header, bg=C["sidebar"])
        brand.pack(side="left", padx=24, pady=14)
        self._label(brand, "PCMIC-360", 17, bold=True).pack(anchor="w")
        self._label(brand, APP_VERSION, 8, C["subtext"]).pack(anchor="w")

        status_wrap = tk.Frame(header, bg=C["sidebar"])
        status_wrap.pack(side="right", padx=24, pady=18)
        self.connection_pill = self._pill(status_wrap, "DISCONNECTED", C["subtext"], C["panel2"])
        self.connection_pill.pack(side="left", padx=(0, 8))
        self.install_pill = self._pill(status_wrap, "HOOK NOT INSTALLED", C["subtext"], C["panel2"])
        self.install_pill.pack(side="left", padx=(0, 8))
        self.mic_pill = self._pill(status_wrap, "MIC IDLE", C["subtext"], C["panel2"])
        self.mic_pill.pack(side="left")

        body = tk.Frame(self.root, bg=C["bg"])
        body.pack(fill="both", expand=True, padx=18, pady=18)
        body.grid_columnconfigure(0, weight=3, uniform="main")
        body.grid_columnconfigure(1, weight=2, uniform="main")
        body.grid_rowconfigure(0, weight=1)

        left = tk.Frame(body, bg=C["bg"])
        right = tk.Frame(body, bg=C["bg"])
        left.grid(row=0, column=0, sticky="nsew", padx=(0, 9))
        right.grid(row=0, column=1, sticky="nsew", padx=(9, 0))

        # LEFT: microphone / mute / gate
        mic_card = self._card(left)
        mic_card.pack(fill="x", pady=(0, 12))
        self._section_title(mic_card, "Microphone", "16 kHz mono • 20 ms packets • PCM16-BE").pack(
            fill="x", padx=16, pady=(14, 10))

        device_row = tk.Frame(mic_card, bg=C["panel"])
        device_row.pack(fill="x", padx=16, pady=(0, 12))
        self._label(device_row, "Input device", 8, C["subtext"]).pack(anchor="w")
        chooser_row = tk.Frame(device_row, bg=C["panel"])
        chooser_row.pack(fill="x", pady=(5, 0))
        self.device_var = tk.StringVar()
        self.device_combo = ttk.Combobox(chooser_row, textvariable=self.device_var,
                                         state="readonly", style="Dark.TCombobox")
        self.device_combo.pack(side="left", fill="x", expand=True)
        self.device_combo.bind("<<ComboboxSelected>>", self._device_changed)
        self.refresh_mics_btn = self._button(chooser_row, "REFRESH", self._refresh_devices)
        self.refresh_mics_btn.pack(side="left", padx=(8, 0))

        mute_area = tk.Frame(mic_card, bg=C["panel"])
        mute_area.pack(fill="x", padx=16, pady=(0, 14))
        mute_area.grid_columnconfigure(0, weight=1)
        mute_area.grid_columnconfigure(1, weight=1)

        self.mute_btn = self._button(mute_area, "MUTE", self._toggle_mute, "danger")
        self.mute_btn.grid(row=0, column=0, sticky="ew", padx=(0, 6))

        self.hold_btn = self._button(mute_area, "HOLD TO MUTE", lambda: None, "secondary")
        self.hold_btn.grid(row=0, column=1, sticky="ew", padx=(6, 0))
        self.hold_btn.bind("<ButtonPress-1>", self._hold_press)
        self.hold_btn.bind("<ButtonRelease-1>", self._hold_release)
        self.hold_btn.bind("<Leave>", self._hold_release)
        self._label(mute_area, "M = toggle mute", 8, C["subtext"]).grid(row=1, column=0, pady=(6, 0))
        self._label(mute_area, "Space = hold / release", 8, C["subtext"]).grid(row=1, column=1, pady=(6, 0))

        processing_card = self._card(left)
        processing_card.pack(fill="x", pady=(0, 12))
        self._section_title(
            processing_card,
            "Mic Processing",
            "Input Level attenuates the mic; Gain digitally boosts it before the voice gate"
        ).pack(fill="x", padx=16, pady=(14, 8))

        input_head = tk.Frame(processing_card, bg=C["panel"])
        input_head.pack(fill="x", padx=16)
        self._label(input_head, "Mic Input Level", 8, C["subtext"]).pack(side="left")
        self.input_level_label = self._pill(
            input_head, f"{self.input_level:.0f}%", C["accent_hover"], "#1A3150")
        self.input_level_label.pack(side="right")
        self.input_level_slider = ValueSlider(
            processing_card, value=self.input_level, minimum=0, maximum=100, step=1,
            command=self._input_level_changed)
        self.input_level_slider.pack(fill="x", padx=16, pady=(2, 1))
        input_scale = tk.Frame(processing_card, bg=C["panel"])
        input_scale.pack(fill="x", padx=16)
        self._label(input_scale, "0% silent", 7, C["muted_text"]).pack(side="left")
        self._label(input_scale, "100% full input", 7, C["muted_text"]).pack(side="right")

        gain_head = tk.Frame(processing_card, bg=C["panel"])
        gain_head.pack(fill="x", padx=16, pady=(10, 0))
        self._label(gain_head, "Mic Gain", 8, C["subtext"]).pack(side="left")
        self.gain_value_label = self._pill(
            gain_head, f"{self.gain:.2f}×", C["accent_hover"], "#1A3150")
        self.gain_value_label.pack(side="right")
        self.gain_slider = ValueSlider(
            processing_card, value=self.gain, minimum=0.0, maximum=4.0, step=0.05,
            command=self._gain_changed)
        self.gain_slider.pack(fill="x", padx=16, pady=(2, 1))
        gain_scale = tk.Frame(processing_card, bg=C["panel"])
        gain_scale.pack(fill="x", padx=16, pady=(0, 12))
        self._label(gain_scale, "0.00×", 7, C["muted_text"]).pack(side="left")
        self._label(gain_scale, "1.00× normal", 7, C["muted_text"]).pack(side="left", padx=(80, 0))
        self._label(gain_scale, "4.00× boost", 7, C["muted_text"]).pack(side="right")

        gate_card = self._card(left)
        gate_card.pack(fill="x", pady=(0, 12))
        gate_head = tk.Frame(gate_card, bg=C["panel"])
        gate_head.pack(fill="x", padx=16, pady=(14, 4))
        gate_titles = self._section_title(gate_head, "Voice Gate", "Quiet input becomes exact digital silence")
        gate_titles.pack(side="left")
        gate_label = "GATE OFF" if self.gate_threshold <= 0 else f"RMS {self.gate_threshold}"
        self.gate_value_label = self._pill(gate_head, gate_label, C["warn"], "#3A311C")
        self.gate_value_label.pack(side="right")

        self.gate_slider = GateSlider(gate_card, value=self.gate_threshold, command=self._gate_changed)
        self.gate_slider.pack(fill="x", padx=16, pady=(4, 0))
        scale_labels = tk.Frame(gate_card, bg=C["panel"])
        scale_labels.pack(fill="x", padx=16)
        self._label(scale_labels, "0  OFF / most sensitive", 8, C["subtext"]).pack(side="left")
        self._label(scale_labels, f"{GATE_MAX}  less sensitive", 8, C["subtext"]).pack(side="right")

        meter_row = tk.Frame(gate_card, bg=C["panel"])
        meter_row.pack(fill="x", padx=16, pady=(12, 14))
        meter_top = tk.Frame(meter_row, bg=C["panel"])
        meter_top.pack(fill="x")
        self._label(meter_top, "Live mic level", 8, C["subtext"]).pack(side="left")
        self.rms_label = self._label(meter_top, "RMS 0", 8, C["subtext"], bold=True)
        self.rms_label.pack(side="right")
        self.level_meter = LevelMeter(meter_row)
        self.level_meter.pack(fill="x", pady=(5, 4))
        self.gate_state_label = self._label(meter_row, "GATE CLOSED", 9, C["subtext"], bold=True)
        self.gate_state_label.pack(anchor="e")

        controls_card = self._card(left)
        controls_card.pack(fill="both", expand=True)
        self._section_title(controls_card, "Xbox Controls",
                            "The XEX still performs all dynamic v7.03 discovery").pack(
            fill="x", padx=16, pady=(14, 10))

        control_grid = tk.Frame(controls_card, bg=C["panel"])
        control_grid.pack(fill="x", padx=16, pady=(0, 10))
        for i in range(2):
            control_grid.grid_columnconfigure(i, weight=1)

        self.live_btn = self._button(control_grid, "START LIVE MIC", self._toggle_live, "good")
        self.live_btn.grid(row=0, column=0, columnspan=2, sticky="ew", pady=(0, 8))
        self.report_btn = self._button(control_grid, "REPORT STATUS", self._report_status)
        self.report_btn.grid(row=1, column=0, sticky="ew", padx=(0, 4), pady=4)
        self.restore_btn = self._button(control_grid, "RESTORE / SAFE UNLOAD", self._restore, "danger")
        self.restore_btn.grid(row=1, column=1, sticky="ew", padx=(4, 0), pady=4)

        self.control_hint = self._label(controls_card,
            "Load PcMic-360.xex → enter the Xbox IP → CONNECT. The voice hook installs automatically, then start live mic.",
            8, C["subtext"], wraplength=600, justify="left")
        self.control_hint.pack(fill="x", padx=16, pady=(2, 14))

        # RIGHT: Xbox connection / status diagnostics
        connection_card = self._card(right)
        connection_card.pack(fill="x", pady=(0, 12))
        self._section_title(connection_card, "Xbox Connection",
                            "v7.03 listens on the console; this PC connects to it").pack(
            fill="x", padx=16, pady=(14, 9))

        conn_row = tk.Frame(connection_card, bg=C["panel"])
        conn_row.pack(fill="x", padx=16, pady=(0, 8))
        self.xbox_ip_var = tk.StringVar(value=self.saved_xbox_ip)
        self.xbox_ip_entry = tk.Entry(
            conn_row, textvariable=self.xbox_ip_var, bg=C["panel2"], fg=C["text"],
            insertbackground=C["text"], relief=tk.FLAT, bd=0,
            highlightbackground=C["border"], highlightcolor=C["accent"],
            highlightthickness=1, font=("Segoe UI", 10), width=18)
        self.xbox_ip_entry.pack(side="left", fill="x", expand=True, ipady=7)
        self.connect_btn = self._button(conn_row, "CONNECT", self._toggle_connection, "primary", width=12)
        self.connect_btn.pack(side="left", padx=(8, 0))

        conn_meta = tk.Frame(connection_card, bg=C["panel"])
        conn_meta.pack(fill="x", padx=16, pady=(0, 13))
        self._label(conn_meta, "Xbox IP (example 192.168.1.186)", 8, C["subtext"]).pack(side="left")
        self._label(conn_meta, "TCP 36000", 8, C["subtext"], bold=True).pack(side="right")

        status_card = self._card(right)
        status_card.pack(fill="x", pady=(0, 12))
        self._section_title(status_card, "Runtime Status", "Dynamic resolver + virtual headset").pack(
            fill="x", padx=16, pady=(14, 10))

        stats = tk.Frame(status_card, bg=C["panel"])
        stats.pack(fill="x", padx=16, pady=(0, 14))
        stats.grid_columnconfigure(1, weight=1)
        self.stat_labels = {}
        rows = [
            ("Console", "Waiting..."),
            ("Hook", "Not installed"),
            ("Voice lane", "Discovering"),
            ("Headset", "Waiting"),
            ("Packets", "0"),
            ("Direct writes", "0"),
        ]
        for r, (name, value) in enumerate(rows):
            self._label(stats, name.upper(), 8, C["subtext"], bold=True).grid(
                row=r, column=0, sticky="w", pady=5)
            lbl = self._label(stats, value, 9, C["text"], bold=True)
            lbl.grid(row=r, column=1, sticky="e", pady=5)
            self.stat_labels[name] = lbl

        activity_card = self._card(right)
        activity_card.pack(fill="x", pady=(0, 12))
        self._section_title(activity_card, "Audio Activity", "PC host counters").pack(
            fill="x", padx=16, pady=(14, 9))
        activity = tk.Frame(activity_card, bg=C["panel"])
        activity.pack(fill="x", padx=16, pady=(0, 14))
        activity.grid_columnconfigure(0, weight=1)
        activity.grid_columnconfigure(1, weight=1)
        self.sent_value = self._big_stat(activity, 0, "FRAMES SENT")
        self.drop_value = self._big_stat(activity, 1, "STALE / QUEUE DROPS")
        self.audio_health_label = self._label(
            activity,
            "Capture overflow 0  •  capture jitter 0  •  send gaps 0  •  max age 0 ms",
            8, C["subtext"], wraplength=590, justify="left")
        self.audio_health_label.grid(row=1, column=0, columnspan=2, sticky="ew", pady=(10, 0))

        log_card = self._card(right)
        log_card.pack(fill="both", expand=True)
        log_head = tk.Frame(log_card, bg=C["panel"])
        log_head.pack(fill="x", padx=14, pady=(12, 8))
        self._label(log_head, "Event Log", 11, bold=True).pack(side="left")
        self._button(log_head, "CLEAR", self._clear_log, width=7).pack(side="right")

        log_wrap = tk.Frame(log_card, bg=C["panel"])
        log_wrap.pack(fill="both", expand=True, padx=14, pady=(0, 14))
        self.log_text = tk.Text(log_wrap, bg="#0C131E", fg=C["text"],
                                insertbackground=C["text"], relief=tk.FLAT, bd=0,
                                font=("Consolas", 9), wrap="word", padx=9, pady=8,
                                selectbackground=C["accent_dim"], height=13)
        scroll = tk.Scrollbar(log_wrap, command=self.log_text.yview, bg=C["panel2"],
                              troughcolor=C["panel"], relief=tk.FLAT)
        self.log_text.configure(yscrollcommand=scroll.set, state="disabled")
        self.log_text.pack(side="left", fill="both", expand=True)
        scroll.pack(side="right", fill="y")
        self.log_text.tag_configure("info", foreground=C["subtext"])
        self.log_text.tag_configure("good", foreground=C["good"])
        self.log_text.tag_configure("warn", foreground=C["warn"])
        self.log_text.tag_configure("bad", foreground=C["bad"])
        self.log_text.tag_configure("xex", foreground="#A9C8FF")

    def _big_stat(self, master, col, caption):
        box = tk.Frame(master, bg=C["panel2"], highlightbackground=C["border"], highlightthickness=1)
        box.grid(row=0, column=col, sticky="ew", padx=(0, 5) if col == 0 else (5, 0))
        value = self._label(box, "0", 17, bold=True)
        value.pack(pady=(10, 0))
        self._label(box, caption, 7, C["subtext"], bold=True).pack(pady=(0, 10))
        return value

    # ---------- logging / UI thread ----------
    def _log(self, text, kind="info"):
        self.ui_queue.put(("log", text, kind))

    def _set_pill(self, pill, text, fg, bg):
        pill.configure(text=text, fg=fg, bg=bg)

    def _process_ui_queue(self):
        try:
            while True:
                item = self.ui_queue.get_nowait()
                action = item[0]
                if action == "log":
                    _, text, kind = item
                    stamp = time.strftime("%H:%M:%S")
                    self.log_text.configure(state="normal")
                    self.log_text.insert("end", f"[{stamp}] {text}\n", kind)
                    self.log_text.see("end")
                    self.log_text.configure(state="disabled")
                elif action == "connecting":
                    _, ip = item
                    self._set_pill(self.connection_pill, "CONNECTING", C["warn"], "#3A311C")
                    self.stat_labels["Console"].configure(text=f"{ip}:{self.port}", fg=C["warn"])
                    self.connect_btn.configure(text="DISCONNECT", bg="#6A2730", activebackground="#7F303A")
                    self.xbox_ip_entry.configure(state="disabled")
                elif action == "connected":
                    _, ip, port = item
                    self._set_pill(self.connection_pill, "CONNECTED", C["good"], "#193D31")
                    self.stat_labels["Console"].configure(text=f"{ip}:{port}", fg=C["good"])
                    self.connect_btn.configure(text="DISCONNECT", bg="#6A2730", activebackground="#7F303A")
                    self.xbox_ip_entry.configure(state="disabled")
                    self.control_hint.configure(text="Connected to v7.03. Installing the voice hook automatically…")
                elif action == "disconnected":
                    retrying = bool(item[1]) if len(item) > 1 else False
                    if retrying:
                        self._set_pill(self.connection_pill, "RECONNECTING", C["warn"], "#3A311C")
                        self.connect_btn.configure(text="DISCONNECT", bg="#6A2730", activebackground="#7F303A")
                        self.xbox_ip_entry.configure(state="disabled")
                    else:
                        self._set_pill(self.connection_pill, "DISCONNECTED", C["subtext"], C["panel2"])
                        self.connect_btn.configure(text="CONNECT", bg=C["accent"], activebackground=C["accent_hover"])
                        self.xbox_ip_entry.configure(state="normal")
                    self.stat_labels["Console"].configure(text="Waiting...", fg=C["subtext"])
                    self._set_install_state(False)
                    self._stop_audio_stream(local_only=True)
                elif action == "hotkey_toggle_mute":
                    self._toggle_mute()
                elif action == "hotkey_hold_press":
                    self._hold_press()
                elif action == "hotkey_hold_release":
                    self._hold_release()
                elif action == "installed":
                    self._set_install_state(True)
                    self.control_hint.configure(text="Voice hook installed automatically. Select your mic and start live mic.")
                elif action == "auto_install":
                    self._auto_install_hook()
                elif action == "status":
                    self._refresh_status_fields()
        except queue.Empty:
            pass
        if not self.shutdown.is_set():
            self.root.after(50, self._process_ui_queue)

    def _clear_log(self):
        self.log_text.configure(state="normal")
        self.log_text.delete("1.0", "end")
        self.log_text.configure(state="disabled")

    # ---------- devices ----------
    def _refresh_devices(self):
        try:
            devices = sd.query_devices()
            names = []
            self.device_map = {}
            default_input = None
            try:
                default_pair = sd.default.device
                if isinstance(default_pair, (tuple, list)):
                    default_input = int(default_pair[0])
                else:
                    default_input = int(default_pair)
            except Exception:
                default_input = None

            for i, d in enumerate(devices):
                if d["max_input_channels"] > 0:
                    label = f"{i}  •  {d['name']}"
                    names.append(label)
                    self.device_map[label] = i
                    if self.saved_device_name and self.saved_device_name.lower() in d["name"].lower():
                        self.selected_device = i
                        self.device_var.set(label)
                    elif not self.saved_device_name and i == default_input:
                        self.selected_device = i
                        self.device_var.set(label)

            if not names:
                raise RuntimeError("No input devices found")
            self.device_combo["values"] = names
            if not self.device_var.get():
                self.device_var.set(names[0])
                self.selected_device = self.device_map[names[0]]
            self._log(f"Microphones refreshed: {len(names)} input device(s).", "info")
        except Exception as e:
            self._log(f"Microphone enumeration failed: {e}", "bad")
            messagebox.showerror(APP_NAME, f"Could not enumerate microphones:\n\n{e}")

    def _device_changed(self, _event=None):
        if self.live_active.is_set():
            # Device switches mid-stream are error-prone; keep the working stream.
            self._log("Stop live mic before changing the input device.", "warn")
            return
        label = self.device_var.get()
        self.selected_device = self.device_map.get(label)
        self.saved_device_name = label.split("•", 1)[-1].strip() if "•" in label else label
        self._save_settings()
        self._log(f"Input device selected: {label}", "info")

    # ---------- network ----------
    @staticmethod
    def _valid_ipv4(value):
        try:
            socket.inet_aton(value)
            return value.count(".") == 3
        except OSError:
            return False

    def _toggle_connection(self):
        if self.want_connected.is_set():
            self.want_connected.clear()
            self._disconnect_socket()
            self.ui_queue.put(("disconnected", False))
            self._log("Xbox connection stopped by user.", "info")
            return

        ip = self.xbox_ip_var.get().strip()
        if not self._valid_ipv4(ip):
            messagebox.showerror(APP_NAME, "Enter a valid Xbox IPv4 address, for example 192.168.1.186.")
            return

        self.saved_xbox_ip = ip
        self._save_settings()
        # A deliberate new connection begins a fresh control session.
        self.restore_pending.clear()
        self.safe_to_unload.clear()
        self.auto_install_scheduled = False
        self.want_connected.set()
        self.ui_queue.put(("connecting", ip))
        self._log(f"Connecting to Xbox {ip}:{self.port}...", "info")

    def _start_connection_thread(self):
        if self.connection_thread_started:
            return
        self.connection_thread_started = True
        threading.Thread(target=self._connection_loop, daemon=True).start()

    def _disconnect_socket(self):
        c = self.client_socket
        self.client_socket = None
        if c is not None:
            try:
                c.shutdown(socket.SHUT_RDWR)
            except Exception:
                pass
            try:
                c.close()
            except Exception:
                pass

    def _connection_loop(self):
        last_error = None
        last_error_log = 0.0
        while not self.shutdown.is_set():
            if not self.want_connected.is_set():
                time.sleep(0.10)
                continue

            ip = self.saved_xbox_ip.strip()
            if not ip:
                self.want_connected.clear()
                self.ui_queue.put(("disconnected", False))
                continue

            try:
                c = socket.create_connection((ip, self.port), timeout=3.0)
                c.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
                c.settimeout(0.5)
                self.client_socket = c
                self.client_addr = (ip, self.port)
                self.connected.set()
                self.banner_seen.clear()
                self.installed.clear()
                self.lane_locked.clear()
                self.auto_install_scheduled = False
                # Do NOT clear SAFE_TO_UNLOAD here. v1.1 did that on an
                # automatic reconnect and could erase a valid unload ACK
                # before the delayed restore check ran. The latch is reset
                # only by a deliberate new connect/install session.
                self.ui_queue.put(("connected", ip, self.port))
                self._log(f"Connected to Xbox {ip}:{self.port}", "good")
                last_error = None
                self._rx_loop(c)
            except Exception as e:
                now = time.monotonic()
                text = str(e) or e.__class__.__name__
                if text != last_error or now - last_error_log >= 5.0:
                    self._log(f"Xbox connection pending: {text}", "warn")
                    last_error = text
                    last_error_log = now
            finally:
                self.connected.clear()
                self.banner_seen.clear()
                self.installed.clear()
                self.lane_locked.clear()
                self._disconnect_socket()
                self.client_addr = None
                retry = self.want_connected.is_set() and not self.shutdown.is_set()
                self.ui_queue.put(("disconnected", retry))
                if retry:
                    time.sleep(1.0)

    def _rx_loop(self, c):
        buf = b""
        while not self.shutdown.is_set() and self.want_connected.is_set():
            try:
                d = c.recv(65536)
            except socket.timeout:
                continue
            except OSError as e:
                if not self.shutdown.is_set() and self.want_connected.is_set():
                    self._log(f"Xbox connection ended: {e}", "warn")
                return
            if not d:
                if not self.shutdown.is_set() and self.want_connected.is_set():
                    self._log("Xbox closed the connection; reconnecting...", "warn")
                return
            buf += d
            while b"\n" in buf:
                raw, buf = buf.split(b"\n", 1)
                text = raw.rstrip(b"\r").decode("ascii", "replace")
                self._handle_xex_line(text)

    def _handle_xex_line(self, text):
        self._log("XEX → " + text, "xex")

        if "VOICE_DECODE_UNIVERSAL_SYSTEM_MIC_V7_" in text and "HELLO" in text:
            self.banner_seen.set()
            version = "v7.x"
            try:
                token = next(t for t in text.split() if t.startswith("VOICE_DECODE_UNIVERSAL_SYSTEM_MIC_V7_"))
                version = "v" + token.rsplit("_V", 1)[1].replace("_", ".")
            except Exception:
                pass
            self._log(f"PcMic-360 {version} XEX detected.", "good")
            if not self.auto_install_scheduled and not self.safe_to_unload.is_set():
                self.auto_install_scheduled = True
                self.ui_queue.put(("auto_install",))

        if text.startswith("V700_RESOLVE"):
            self.last_resolve = text
        if text.startswith("V702_HEADSET_RESOLVE") and "ok=1" in text:
            self.headset_resolved = True
        if text.startswith("V702_VIRTUAL_HEADSET") and "ok=1" in text:
            self.virtual_headset = True

        if text.startswith("V700_INSTALL"):
            if "ok=1" in text:
                self.installed.set()
                self.ui_queue.put(("installed",))
            else:
                self.installed.clear()

        if text.startswith("V700_START ok=1"):
            mode = self._parse_fields(text).get("mode", "")
            if mode == "2":
                self.live_active.set()

        if text.startswith("V700_STOP"):
            self.last_stop = text
            self.live_active.clear()

        if text.startswith("V700_STATUS"):
            self.last_status = text
            self.status_fields = self._parse_fields(text)
            if self.status_fields.get("installed") == "1":
                if not self.installed.is_set():
                    self.installed.set()
                    self.ui_queue.put(("installed",))
            elif self.status_fields.get("installed") == "0":
                self.installed.clear()
            if self.status_fields.get("locked") == "1":
                self.lane_locked.set()
            else:
                self.lane_locked.clear()
            self.ui_queue.put(("status",))

        if text.startswith("V700_DISCOVERY") and "locked=1" in text:
            self.lane_locked.set()
            self.ui_queue.put(("status",))

        if "SAFE_TO_UNLOAD" in text:
            self.safe_to_unload.set()
            self._log("Original gateways restored — SAFE TO UNLOAD.", "good")
            if self.restore_pending.is_set():
                # Q intentionally closes the current Xbox client after restore.
                # Suppress v7.03 auto-reconnect so the unload ACK cannot be
                # overwritten by a fresh HELLO/session before the GUI reports it.
                self.restore_pending.clear()
                self.want_connected.clear()
                self._log("Restore complete. Auto-reconnect paused for safe unload.", "good")

    @staticmethod
    def _parse_fields(line):
        out = {}
        for token in line.split()[1:]:
            if "=" in token:
                k, v = token.split("=", 1)
                out[k.strip()] = v.strip().strip(",")
        return out

    def _send(self, data):
        c = self.client_socket
        if not c or not self.connected.is_set():
            raise ConnectionError("Xbox is not connected")
        with self.send_lock:
            c.sendall(data)

    def _cmd(self, command):
        try:
            self._send((command + "\n").encode("ascii"))
            return True
        except Exception as e:
            self._log(f"Command {command} failed: {e}", "bad")
            return False

    # ---------- Xbox commands ----------
    def _auto_install_hook(self):
        # Ask the XEX for its current state first. On a reconnect the gateways
        # may already be installed, and sending O again should be unnecessary.
        if self.connected.is_set():
            self._cmd("P")
        self.root.after(300, self._auto_install_hook_now)

    def _auto_install_hook_now(self):
        if self.installed.is_set():
            self._log("Voice hook is already installed; automatic install skipped.", "good")
            return
        if not self.connected.is_set() or not self.banner_seen.is_set():
            self.auto_install_scheduled = False
            return
        if self.safe_to_unload.is_set() or self.restore_pending.is_set():
            return
        self.restore_pending.clear()
        self.safe_to_unload.clear()
        self._set_pill(self.install_pill, "INSTALLING HOOK", C["warn"], "#3A311C")
        self.stat_labels["Hook"].configure(text="Installing…", fg=C["warn"])
        if self._cmd("O"):
            self._log("Automatically installing dynamic voice + virtual-headset gateways…", "info")
            self.root.after(5000, self._install_timeout_check)
        else:
            self.auto_install_scheduled = False

    def _install_timeout_check(self):
        if self.installed.is_set():
            return
        if not self.connected.is_set():
            return
        self._set_pill(self.install_pill, "HOOK PENDING", C["warn"], "#3A311C")
        self.stat_labels["Hook"].configure(text="Pending…", fg=C["warn"])
        self._log("Automatic hook install has not confirmed yet. Check the resolver lines in the log.", "warn")

    def _toggle_live(self):
        if self.live_active.is_set() or self.audio_stream is not None:
            self._stop_live()
        else:
            self._start_live()

    def _start_live(self):
        if not self.installed.is_set():
            self._log("Install the voice hook before starting live microphone audio.", "warn")
            return
        try:
            self._clear_audio_queue()
            self._reset_audio_health()
            self.audio_stream = sd.InputStream(
                samplerate=RATE,
                blocksize=SAMPLES,
                channels=1,
                dtype="int16",
                device=self.selected_device,
                callback=None,
                latency="low",
            )
            self.audio_stream.start()
            if not self._cmd("S"):
                raise RuntimeError("Could not send live-mode command to Xbox")
            self.live_active.set()
            self._start_capture_thread()
            self.live_btn.configure(text="STOP LIVE MIC", bg="#6A2730", activebackground="#7F303A")
            self.device_combo.configure(state="disabled")
            self.refresh_mics_btn.configure(state="disabled")
            self._log("Live PC microphone started.", "good")
        except Exception as e:
            self._stop_audio_stream(local_only=True)
            self._log(f"Could not start microphone: {e}", "bad")
            messagebox.showerror(APP_NAME, f"Could not start microphone:\n\n{e}")

    def _stop_live(self):
        if self.connected.is_set():
            self._cmd("T")
        self.live_active.clear()
        self._stop_audio_stream(local_only=True)
        self._clear_audio_queue()
        self.live_btn.configure(text="START LIVE MIC", bg="#1D5D48", activebackground="#25715A")
        self.device_combo.configure(state="readonly")
        self.refresh_mics_btn.configure(state="normal")
        self._log(
            f"Live microphone stopped. Audio health: overflows={self.capture_overflows}, "
            f"staleDrops={self.stale_frame_drops}, maxCaptureJitter={self.max_capture_jitter_ms:.1f}ms, "
            f"maxSendGap={self.max_send_gap_ms:.1f}ms, maxFrameAge={self.max_send_age_ms:.1f}ms.",
            "info")

    def _report_status(self):
        if self._cmd("P"):
            self._log("Requested current v7.03 status.", "info")

    def _restore(self):
        if not self.connected.is_set():
            self._log("No Xbox connection to restore.", "warn")
            return
        self._stop_audio_stream(local_only=True)
        self.live_active.clear()
        self.safe_to_unload.clear()
        self.restore_pending.set()
        if self._cmd("Q"):
            self._log("Restore requested. Waiting for SAFE_TO_UNLOAD...", "warn")
            self.root.after(4500, self._restore_check)
        else:
            self.restore_pending.clear()

    def _restore_check(self):
        if self.safe_to_unload.is_set():
            messagebox.showinfo(
                APP_NAME,
                "Original gateways restored.\n\nSAFE TO UNLOAD received.\n\n"
                "Auto-reconnect has been paused so you can unload the XEX safely."
            )
        else:
            self._log("SAFE_TO_UNLOAD not seen yet. Reboot before unloading if it never arrives.", "warn")

    # ---------- audio ----------
    def _start_sender_thread(self):
        if self.sender_thread_started:
            return
        self.sender_thread_started = True
        threading.Thread(target=self._sender_loop, name="PcMic-NetSend", daemon=True).start()

    def _register_mmcss_audio_thread(self):
        """Best-effort Windows MMCSS registration for the capture/processing thread."""
        if sys.platform != "win32":
            return None
        try:
            import ctypes
            task_index = ctypes.c_ulong(0)
            avrt = ctypes.WinDLL("avrt.dll")
            avrt.AvSetMmThreadCharacteristicsW.argtypes = [ctypes.c_wchar_p, ctypes.POINTER(ctypes.c_ulong)]
            avrt.AvSetMmThreadCharacteristicsW.restype = ctypes.c_void_p
            handle = avrt.AvSetMmThreadCharacteristicsW("Pro Audio", ctypes.byref(task_index))
            if handle:
                return (avrt, handle)
        except Exception as exc:
            self._log(f"MMCSS Pro Audio priority unavailable: {exc}", "warn")
        return None

    @staticmethod
    def _revert_mmcss(token):
        if not token:
            return
        try:
            import ctypes
            avrt, handle = token
            avrt.AvRevertMmThreadCharacteristics.argtypes = [ctypes.c_void_p]
            avrt.AvRevertMmThreadCharacteristics.restype = int
            avrt.AvRevertMmThreadCharacteristics(ctypes.c_void_p(handle))
        except Exception:
            pass

    def _reset_audio_health(self):
        self.drops = 0
        self.stale_frame_drops = 0
        self.capture_overflows = 0
        self.capture_jitter_events = 0
        self.send_gap_events = 0
        self.max_capture_jitter_ms = 0.0
        self.max_send_age_ms = 0.0
        self.max_send_gap_ms = 0.0
        self._last_capture_tick = 0.0
        self._last_send_tick = 0.0

    def _start_capture_thread(self):
        if self.capture_thread is not None and self.capture_thread.is_alive():
            return
        self.capture_stop.clear()
        self.capture_thread = threading.Thread(
            target=self._capture_loop, name="PcMic-Capture", daemon=True)
        self.capture_thread.start()

    def _capture_loop(self):
        token = self._register_mmcss_audio_thread()
        expected_ms = (SAMPLES * 1000.0) / RATE
        try:
            while (not self.shutdown.is_set() and
                   not self.capture_stop.is_set() and
                   self.audio_stream is not None):
                stream = self.audio_stream
                try:
                    indata, overflowed = stream.read(SAMPLES)
                except Exception as exc:
                    if not self.capture_stop.is_set() and not self.shutdown.is_set():
                        self._log(f"Microphone capture read failed: {exc}", "bad")
                    break

                now = time.monotonic()
                if self._last_capture_tick:
                    interval_ms = (now - self._last_capture_tick) * 1000.0
                    jitter_ms = abs(interval_ms - expected_ms)
                    if jitter_ms > self.max_capture_jitter_ms:
                        self.max_capture_jitter_ms = jitter_ms
                    if jitter_ms >= CAPTURE_JITTER_WARN_MS:
                        self.capture_jitter_events += 1
                self._last_capture_tick = now

                if overflowed:
                    self.capture_overflows += 1
                    if self.capture_overflows <= 3 or self.capture_overflows % 25 == 0:
                        self._log(
                            f"PortAudio input overflow #{self.capture_overflows}: captured samples were discarded; dropping queued voice backlog.",
                            "warn")
                    # This means PortAudio discarded microphone samples.  Do not
                    # try to conceal it by replaying old queued voice frames.
                    self._clear_audio_queue()

                self._process_audio_block(indata, now)
        finally:
            self._revert_mmcss(token)

    def _process_audio_block(self, indata, capture_tick):
        try:
            # Processing is intentionally outside PortAudio's real-time callback.
            # Defaults (100% and 1.00x) preserve the original level.
            mono = indata[:, 0].astype(np.float64, copy=False)
            level_scale = max(0.0, min(1.0, float(self.input_level) / 100.0))
            gain_scale = max(0.0, min(4.0, float(self.gain)))
            combined_scale = level_scale * gain_scale
            mono = np.clip(np.rint(mono * combined_scale), -32768, 32767).astype(np.int16)

            rms = float(np.sqrt(np.mean(mono.astype(np.float64) ** 2)))
            self.rms = rms

            now = capture_tick
            threshold = self.gate_threshold
            if threshold <= 0:
                gate_open = True
            else:
                if rms >= threshold:
                    self.gate_until = now + max(0, self.hangover_ms) / 1000.0
                gate_open = now < self.gate_until

            effective_muted = self.base_muted ^ self.hold_active
            previous_open = self.gate_open
            self.gate_open = gate_open and not effective_muted

            if previous_open and not self.gate_open:
                self._clear_audio_queue()

            if effective_muted or not gate_open:
                mono = np.zeros_like(mono, dtype=np.int16)

            frame = mono.byteswap().tobytes()
            if len(frame) != BYTES:
                return
            self._enqueue_latest_frame(capture_tick, frame)
        except Exception as exc:
            self._log(f"Audio processing error: {exc}", "warn")

    def _enqueue_latest_frame(self, capture_tick, frame):
        item = (capture_tick, frame)
        try:
            self.audio_queue.put_nowait(item)
            return
        except queue.Full:
            pass

        # Live voice should prefer the newest audio.  Throw away stale backlog
        # rather than replaying hundreds of milliseconds after a scheduler or
        # TCP stall.
        removed = 0
        while True:
            try:
                self.audio_queue.get_nowait()
                removed += 1
            except queue.Empty:
                break
        self.drops += removed
        self.stale_frame_drops += removed
        try:
            self.audio_queue.put_nowait(item)
        except queue.Full:
            self.drops += 1
            self.stale_frame_drops += 1

    def _sender_loop(self):
        while not self.shutdown.is_set():
            try:
                item = self.audio_queue.get(timeout=0.1)
            except queue.Empty:
                continue

            # If more than one frame accumulated, keep newest only.  This is the
            # core anti-crackle/anti-burst rule for the 20 ms voice stream.
            newest = item
            skipped = 0
            while True:
                try:
                    newest = self.audio_queue.get_nowait()
                    skipped += 1
                except queue.Empty:
                    break
            if skipped:
                self.drops += skipped
                self.stale_frame_drops += skipped

            if not self.live_active.is_set() or not self.connected.is_set():
                continue

            captured_at, frame = newest
            now = time.monotonic()
            age_ms = max(0.0, (now - captured_at) * 1000.0)
            if age_ms > self.max_send_age_ms:
                self.max_send_age_ms = age_ms
            if age_ms > STALE_FRAME_MS:
                self.drops += 1
                self.stale_frame_drops += 1
                continue

            if self._last_send_tick:
                gap_ms = (now - self._last_send_tick) * 1000.0
                if gap_ms > self.max_send_gap_ms:
                    self.max_send_gap_ms = gap_ms
                if gap_ms >= SEND_GAP_WARN_MS:
                    self.send_gap_events += 1
            try:
                self._send(MAGIC + frame)
                self.sent += 1
                self._last_send_tick = time.monotonic()
            except Exception:
                # Network loop will surface the actual disconnect.  Never sleep
                # and then replay old audio; the queue will keep only fresh data.
                self.drops += 1
                self.stale_frame_drops += 1

    def _stop_audio_stream(self, local_only=False):
        self.capture_stop.set()
        stream = self.audio_stream
        self.audio_stream = None
        if stream is not None:
            try:
                # abort() wakes a blocking read immediately; stop() can wait for
                # pending buffers and makes UI stop/restore feel sluggish.
                stream.abort()
            except Exception:
                try:
                    stream.stop()
                except Exception:
                    pass
            try:
                stream.close()
            except Exception:
                pass
        t = self.capture_thread
        self.capture_thread = None
        if t is not None and t.is_alive() and t is not threading.current_thread():
            try:
                t.join(timeout=0.35)
            except Exception:
                pass
        self.live_active.clear()

    def _clear_audio_queue(self):
        while True:
            try:
                self.audio_queue.get_nowait()
            except queue.Empty:
                break

    # ---------- mute / gate ----------
    def _effective_muted(self):
        return bool(self.base_muted ^ self.hold_active)

    def _queue_immediate_silence(self):
        # Drop any already-queued speech and put one silence frame at the front
        # of the new queue. The normal 20 ms callback then continues silence.
        self._clear_audio_queue()
        if self.live_active.is_set():
            try:
                self.audio_queue.put_nowait((time.monotonic(), b"\x00" * BYTES))
            except queue.Full:
                pass

    def _toggle_mute(self, _event=None):
        was_muted = self._effective_muted()
        self.base_muted = not self.base_muted
        self.hold_active = False
        if self._effective_muted() and not was_muted:
            self._queue_immediate_silence()
        self._update_mute_controls()
        self._log("Microphone muted." if self.base_muted else "Microphone unmuted.",
                  "warn" if self.base_muted else "good")

    def _hold_press(self, _event=None):
        if self.hold_active:
            return "break"
        was_muted = self._effective_muted()
        self.hold_active = True
        if self._effective_muted() and not was_muted:
            self._queue_immediate_silence()
        self._update_mute_controls()
        return "break"

    def _hold_release(self, _event=None):
        if not self.hold_active:
            return "break"
        was_muted = self._effective_muted()
        self.hold_active = False
        if self._effective_muted() and not was_muted:
            self._queue_immediate_silence()
        self._update_mute_controls()
        return "break"

    def _update_mute_controls(self):
        effective = self._effective_muted()
        if self.base_muted:
            self.mute_btn.configure(text="UNMUTE", bg="#1D5D48", activebackground="#25715A")
            self.hold_btn.configure(text="HOLD TO UNMUTE")
        else:
            self.mute_btn.configure(text="MUTE", bg="#6A2730", activebackground="#7F303A")
            self.hold_btn.configure(text="HOLD TO MUTE")

        if self.hold_active:
            self.hold_btn.configure(text="TEMP UNMUTED" if not effective else "TEMP MUTED")

    def _input_level_changed(self, value):
        self.input_level = max(0.0, min(100.0, float(value)))
        self.input_level_label.configure(text=f"{self.input_level:.0f}%")
        self._save_settings()

    def _gain_changed(self, value):
        self.gain = max(0.0, min(4.0, float(value)))
        self.gain_value_label.configure(text=f"{self.gain:.2f}×")
        self._save_settings()

    def _gate_changed(self, value):
        self.gate_threshold = int(value)
        text = "GATE OFF" if value <= 0 else f"RMS {value}"
        fg = C["subtext"] if value <= 0 else C["warn"]
        bg = C["panel2"] if value <= 0 else "#3A311C"
        self.gate_value_label.configure(text=text, fg=fg, bg=bg)
        self._save_settings()

    # ---------- status refresh ----------
    def _set_install_state(self, is_installed):
        if is_installed:
            self._set_pill(self.install_pill, "HOOK INSTALLED", C["good"], "#193D31")
            self.stat_labels["Hook"].configure(text="Installed", fg=C["good"])
        else:
            self._set_pill(self.install_pill, "HOOK NOT INSTALLED", C["subtext"], C["panel2"])
            self.stat_labels["Hook"].configure(text="Not installed", fg=C["subtext"])

    def _refresh_status_fields(self):
        f = self.status_fields
        if f.get("installed") == "1":
            self._set_install_state(True)
        locked = f.get("locked") == "1" or self.lane_locked.is_set()
        lane = f.get("lane", "0")
        byte_count = f.get("bytes", "0x0")
        if locked:
            self.stat_labels["Voice lane"].configure(text=f"Locked • lane {lane} • {byte_count}", fg=C["good"])
        else:
            self.stat_labels["Voice lane"].configure(text="Discovering", fg=C["warn"])

        physical = f.get("physicalHeadset")
        virtual = f.get("virtualHeadset")
        if physical == "1":
            self.stat_labels["Headset"].configure(text="Physical", fg=C["good"])
        elif virtual == "1" or self.virtual_headset:
            self.stat_labels["Headset"].configure(text="Virtual • no wire", fg=C["good"])
        else:
            self.stat_labels["Headset"].configure(text="Waiting", fg=C["subtext"])

        self.stat_labels["Packets"].configure(text=f.get("submits", "0"))
        self.stat_labels["Direct writes"].configure(text=f.get("directVirtualWrites", "0"))

    def _refresh_live_ui(self):
        effective_muted = self._effective_muted()
        self.level_meter.set_values(self.rms, self.gate_threshold)
        self.rms_label.configure(text=f"RMS {self.rms:.0f}")
        self.sent_value.configure(text=f"{self.sent:,}")
        self.drop_value.configure(text=f"{self.drops:,}", fg=C["bad"] if self.drops else C["text"])
        self.audio_health_label.configure(
            text=(f"Capture overflow {self.capture_overflows}  •  "
                  f"capture jitter {self.capture_jitter_events} (max {self.max_capture_jitter_ms:.1f} ms)  •  "
                  f"send gaps {self.send_gap_events} (max {self.max_send_gap_ms:.1f} ms)  •  "
                  f"max frame age {self.max_send_age_ms:.1f} ms"),
            fg=C["bad"] if self.capture_overflows else (C["warn"] if self.send_gap_events or self.stale_frame_drops else C["subtext"]))

        if effective_muted:
            self.gate_state_label.configure(text="MUTED", fg=C["bad"])
        elif self.gate_threshold <= 0:
            self.gate_state_label.configure(text="GATE OFF", fg=C["subtext"])
        elif self.gate_open:
            self.gate_state_label.configure(text="GATE OPEN", fg=C["good"])
        else:
            self.gate_state_label.configure(text="GATE CLOSED", fg=C["subtext"])

        if self.live_active.is_set():
            if effective_muted:
                self._set_pill(self.mic_pill, "MIC MUTED", C["bad"], "#43242A")
            elif self.gate_open or self.gate_threshold <= 0:
                self._set_pill(self.mic_pill, "VOICE ACTIVE", C["good"], "#193D31")
            else:
                self._set_pill(self.mic_pill, "MIC LIVE", C["accent_hover"], "#1A3150")
        else:
            self._set_pill(self.mic_pill, "MIC IDLE", C["subtext"], C["panel2"])

        # Keep button text/state in sync if the XEX changes mode itself.
        if not self.live_active.is_set() and self.audio_stream is None:
            if self.live_btn.cget("text") != "START LIVE MIC":
                self.live_btn.configure(text="START LIVE MIC", bg="#1D5D48", activebackground="#25715A")
        self._refresh_status_fields()

        if not self.shutdown.is_set():
            self.root.after(60, self._refresh_live_ui)

    # ---------- hotkeys / close ----------
    def _start_global_hotkey_thread(self):
        if self.global_hotkey_thread_started or sys.platform != "win32":
            return
        self.global_hotkey_thread_started = True
        threading.Thread(target=self._windows_global_hotkey_loop, daemon=True).start()

    def _windows_global_hotkey_loop(self):
        # No third-party keyboard package is required. GetAsyncKeyState lets M
        # and Space work while the user is looking at another PC window. To
        # avoid hijacking normal typing, system-wide handling is active only
        # while Live Mic is running.
        try:
            import ctypes
            get_key = ctypes.windll.user32.GetAsyncKeyState
        except Exception:
            return

        VK_M = 0x4D
        VK_SPACE = 0x20
        prev_m = False
        prev_space = False

        while not self.shutdown.is_set():
            active = self.live_active.is_set()
            m_down = bool(get_key(VK_M) & 0x8000) if active else False
            space_down = bool(get_key(VK_SPACE) & 0x8000) if active else False

            if active:
                if m_down and not prev_m:
                    self.ui_queue.put(("hotkey_toggle_mute",))
                if space_down and not prev_space:
                    self.ui_queue.put(("hotkey_hold_press",))
                elif not space_down and prev_space:
                    self.ui_queue.put(("hotkey_hold_release",))
            elif prev_space:
                self.ui_queue.put(("hotkey_hold_release",))

            prev_m = m_down
            prev_space = space_down
            time.sleep(0.015)

    def _bind_hotkeys(self):
        # Put our hotkey bindtag FIRST so ttk widgets/comboboxes cannot swallow
        # M or Space before the application sees them.
        tag = "VoiceUniversalHotkeys"
        self.root.bind_class(tag, "<KeyPress>", self._global_key_press)
        self.root.bind_class(tag, "<KeyRelease>", self._global_key_release)

        def install(widget):
            tags = widget.bindtags()
            if tag not in tags:
                widget.bindtags((tag,) + tags)
            for child in widget.winfo_children():
                install(child)

        install(self.root)
        self.root.bind("<FocusOut>", self._hotkey_focus_lost, add="+")

    def _global_key_press(self, event):
        # While live on Windows, the system-wide watcher owns these keys so a
        # focused GUI does not process the same physical press twice.
        if sys.platform == "win32" and self.live_active.is_set():
            return None
        key = str(getattr(event, "keysym", "")).lower()
        if key not in ("m", "space"):
            return None
        if key in self._keys_down:
            return "break"
        self._keys_down.add(key)
        if key == "m":
            self._toggle_mute()
        else:
            self._hold_press(event)
        return "break"

    def _global_key_release(self, event):
        if sys.platform == "win32" and self.live_active.is_set():
            return None
        key = str(getattr(event, "keysym", "")).lower()
        if key not in ("m", "space"):
            return None
        self._keys_down.discard(key)
        if key == "space":
            self._hold_release(event)
        return "break"

    def _hotkey_focus_lost(self, _event=None):
        # Windows can miss a Space release if the user Alt-Tabs while holding
        # it. Never leave temporary mute/unmute stuck in that case.
        self._keys_down.clear()
        if self.hold_active:
            self._hold_release()

    def _on_close(self):
        if self.installed.is_set() and self.connected.is_set() and not self.safe_to_unload.is_set():
            choice = messagebox.askyesnocancel(
                APP_NAME,
                "The v7.03 voice gateways are still installed.\n\n"
                "Yes: restore them and close.\n"
                "No: close the GUI without restoring.\n"
                "Cancel: keep the GUI open."
            )
            if choice is None:
                return
            if choice:
                self._stop_audio_stream(local_only=True)
                self._cmd("Q")
                # Give the message a brief chance to leave before closing sockets.
                time.sleep(0.12)

        self.want_connected.clear()
        self.shutdown.set()
        self._stop_audio_stream(local_only=True)
        try:
            if self.client_socket:
                self.client_socket.shutdown(socket.SHUT_RDWR)
        except Exception:
            pass
        try:
            if self.client_socket:
                self.client_socket.close()
        except Exception:
            pass
        self.root.destroy()


def main():
    root = tk.Tk()
    app = PcMic360GUI(root)
    root.mainloop()


if __name__ == "__main__":
    main()
