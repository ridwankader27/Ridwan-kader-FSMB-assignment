import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch, FancyArrowPatch

def box(ax, x, y, w, h, text, fc="#fff8e1"):
    ax.add_patch(FancyBboxPatch((x, y), w, h, boxstyle="round,pad=0.02,rounding_size=0.15", fc=fc, ec="black", lw=2.6))
    ax.text(x + w/2, y + h/2, text, ha="center", va="center", fontsize=12.5, fontweight="bold")

def arrow(ax, p, q, text=None, color="black", style="-|>"):
    ax.add_patch(FancyArrowPatch(p, q, arrowstyle=style, mutation_scale=20, lw=2.4, color=color))
    if text:
        ax.text((p[0]+q[0])/2, (p[1]+q[1])/2 + 0.18, text, ha="center", fontsize=11, fontweight="bold", color=color)

with plt.xkcd():
    fig, ax = plt.subplots(figsize=(15, 8.6))
    ax.set_xlim(0, 11); ax.set_ylim(0, 6.2); ax.axis("off")
    ax.text(5.5, 5.9, "Voltage Protector - block diagram (real product)", ha="center", fontsize=19, fontweight="bold")

    # power path (top)
    box(ax, 0.2, 3.9, 1.5, 1.0, "Grid\n(AC supply)", "#ffe0e0")
    box(ax, 2.6, 3.9, 1.5, 1.0, "Current sensor\n(ACS712 /\nshunt, isolated)", "#e3f2fd")
    box(ax, 5.0, 3.9, 1.5, 1.0, "Relay\n(contacts in\nseries)", "#e3f2fd")
    box(ax, 7.4, 3.9, 1.5, 1.0, "Protected\nappliance", "#ffe0e0")
    arrow(ax, (1.7, 4.4), (2.6, 4.4)); arrow(ax, (4.1, 4.4), (5.0, 4.4)); arrow(ax, (6.5, 4.4), (7.4, 4.4))
    # voltage sensor tap
    box(ax, 0.2, 2.3, 1.5, 0.9, "Voltage sensor\n(ZMPT101B\ntransformer)", "#e3f2fd")
    arrow(ax, (0.95, 3.9), (0.95, 3.2), "tap", style="-")

    # MCU
    box(ax, 3.6, 1.0, 3.2, 1.8, "ESP32 (firmware)\nADC sampling -> RMS\nfault logic (OV / OC,\ndelay, latch, hysteresis)\nsettings in NVS flash", "#e8f5e9")
    arrow(ax, (1.7, 2.75), (3.6, 2.2), "V (analog)")
    arrow(ax, (3.35, 3.9), (4.2, 2.8), "I (analog)")
    arrow(ax, (5.75, 2.8), (5.75, 3.9), "drive (GPIO26)", color="crimson")

    # HMI
    box(ax, 8.0, 1.9, 2.8, 1.0, "Local UI: 16x2 LCD +\nSELECT / UP / RESET buttons", "#f3e5f5")
    arrow(ax, (6.8, 2.2), (8.0, 2.4), "I2C", style="<|-|>")
    box(ax, 8.0, 0.4, 2.8, 1.0, "PC: serial CLI (USB)\nGET / SET / RESET / monitor", "#f3e5f5")
    arrow(ax, (6.8, 1.4), (8.0, 0.9), "UART", style="<|-|>")
    box(ax, 0.1, 0.4, 3.1, 0.9, "5 V isolated supply\n(AC-DC module -> 3.3 V LDO)", "#fffde7")
    arrow(ax, (3.2, 0.85), (3.6, 1.2), "power")
    ax.text(5.5, 0.15, "Prototype / simulation: V and I sensors replaced by potentiometers on GPIO34/35 (low voltage only).", ha="center", fontsize=11, fontweight="bold")
    fig.savefig("schematics/block_diagram.png", dpi=200, bbox_inches="tight")
