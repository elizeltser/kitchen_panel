#!/usr/bin/env python3
"""
Screensaver converter: pick a photo, crop to 5:3 ratio, convert to
4-level dithered grayscale at 800×480, and save to server/data/screensavers/.
"""

import pathlib
import tkinter as tk
from tkinter import filedialog, messagebox
from PIL import Image, ImageChops, ImageFilter, ImageTk

REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent
SCREENSAVERS_DIR = REPO_ROOT / "server" / "data" / "screensavers"

OUT_W, OUT_H = 800, 480
MAX_DISPLAY_W, MAX_DISPLAY_H = 1200, 700

PALETTE_VALUES = [0, 85, 170, 255]
_PALETTE_BYTES = []
for v in PALETTE_VALUES:
    _PALETTE_BYTES.extend([v, v, v])
_PALETTE_BYTES.extend([0] * (768 - len(_PALETTE_BYTES)))


def _scale_to_fit(img_w: int, img_h: int, max_w: int, max_h: int) -> float:
    return min(max_w / img_w, max_h / img_h, 1.0)


def pick_file(root: tk.Tk) -> pathlib.Path | None:
    path = filedialog.askopenfilename(
        parent=root,
        title="Select image",
        filetypes=[("Images", "*.jpg *.jpeg *.png"), ("All files", "*.*")],
    )
    return pathlib.Path(path) if path else None


def process_image(img: Image.Image, crop_x: int, crop_y: int,
                  crop_w: int, crop_h: int) -> Image.Image:
    cropped = img.crop((crop_x, crop_y, crop_x + crop_w, crop_y + crop_h))
    resized = cropped.resize((OUT_W, OUT_H), Image.LANCZOS)
    gray = resized.convert("L")

    pal_img = Image.new("P", (1, 1))
    pal_img.putpalette(_PALETTE_BYTES)
    quantized = gray.convert("RGB").quantize(
        palette=pal_img, dither=Image.Dither.FLOYDSTEINBERG
    )
    return quantized.convert("L")


def process_image_contour(
    img: Image.Image,
    crop_x: int, crop_y: int, crop_w: int, crop_h: int,
    pre_blur: float,
    edge_threshold: int,
    edge_thickness: float,
    noise_amount: int,
) -> Image.Image:
    cropped = img.crop((crop_x, crop_y, crop_x + crop_w, crop_y + crop_h))
    resized = cropped.resize((OUT_W, OUT_H), Image.LANCZOS)
    gray = resized.convert("L")

    blurred = gray.filter(ImageFilter.GaussianBlur(radius=pre_blur)) if pre_blur > 0 else gray
    edges_raw = blurred.filter(ImageFilter.FIND_EDGES)
    if edge_thickness > 0:
        edges_raw = edges_raw.filter(ImageFilter.GaussianBlur(radius=edge_thickness))

    edge_map = edges_raw.point(lambda x: 0 if x > edge_threshold else 255)

    if noise_amount > 0:
        noise_img = Image.effect_noise((OUT_W, OUT_H), sigma=45)
        frac = noise_amount / 100.0
        dot_threshold = int(128 - (1 - frac) * 100)
        dots = noise_img.point(lambda x: 0 if x < dot_threshold else 255)
        # lighter() clears dots in bright source areas; keeps them in dark (shadow) areas
        masked_dots = ImageChops.lighter(dots, gray)
        result = ImageChops.darker(edge_map, masked_dots)
    else:
        result = edge_map

    pal_img = Image.new("P", (1, 1))
    pal_img.putpalette(_PALETTE_BYTES)
    quantized = result.convert("RGB").quantize(palette=pal_img, dither=Image.Dither.FLOYDSTEINBERG)
    return quantized.convert("L")


class CropWindow(tk.Toplevel):
    """Shows the source image and lets the user position the crop box."""

    def __init__(self, parent: tk.Tk, img: Image.Image):
        super().__init__(parent)
        self.title("Position crop box — click or drag to move")
        self.resizable(False, False)
        self.grab_set()

        self._src = img
        self._result: dict | None = None

        iw, ih = img.size
        self._scale = _scale_to_fit(iw, ih, MAX_DISPLAY_W, MAX_DISPLAY_H)
        dw = int(iw * self._scale)
        dh = int(ih * self._scale)

        # Crop box dimensions in image coords (largest 5:3 rect that fits)
        self._crop_w = min(iw, ih * OUT_W // OUT_H)
        self._crop_h = self._crop_w * OUT_H // OUT_W

        # Box in display coords
        self._box_dw = int(self._crop_w * self._scale)
        self._box_dh = int(self._crop_h * self._scale)

        # Top-left of box in display coords (start at 0,0)
        self._bx = 0
        self._by = 0
        self._max_bx = dw - self._box_dw
        self._max_by = dh - self._box_dh

        self._tk_img = ImageTk.PhotoImage(
            img.resize((dw, dh), Image.LANCZOS)
        )

        self._canvas = tk.Canvas(self, width=dw, height=dh, cursor="crosshair")
        self._canvas.pack()
        self._canvas.create_image(0, 0, anchor="nw", image=self._tk_img)
        self._rect = self._canvas.create_rectangle(
            0, 0, self._box_dw, self._box_dh,
            outline="red", width=2
        )

        # -- Mode selection ------------------------------------------------ #
        self._mode_var = tk.StringVar(value="grayscale")
        mode_frame = tk.Frame(self)
        mode_frame.pack(fill="x", padx=10, pady=(6, 0))
        tk.Label(mode_frame, text="Output mode:").pack(side="left", padx=(0, 8))
        tk.Radiobutton(
            mode_frame, text="4-Level Grayscale",
            variable=self._mode_var, value="grayscale",
            command=self._on_mode_change,
        ).pack(side="left")
        tk.Radiobutton(
            mode_frame, text="Contour / Etching",
            variable=self._mode_var, value="contour",
            command=self._on_mode_change,
        ).pack(side="left")

        # -- Contour parameter sliders (hidden until contour mode selected) -- #
        self._pre_blur_var    = tk.DoubleVar(value=2.0)
        self._edge_thresh_var = tk.IntVar(value=30)
        self._edge_thick_var  = tk.DoubleVar(value=1.0)
        self._noise_var       = tk.IntVar(value=30)

        self._params_frame = tk.LabelFrame(self, text="Contour Parameters", padx=6, pady=4)
        _slider_defs = [
            ("Smoothing",      self._pre_blur_var,    0.0,  8.0, 0.5),
            ("Edge Detail",    self._edge_thresh_var, 5,   100, 1  ),
            ("Line Thickness", self._edge_thick_var,  0.0,  4.0, 0.5),
            ("Texture",        self._noise_var,        0,   100, 1  ),
        ]
        for row, (label, var, lo, hi, res) in enumerate(_slider_defs):
            tk.Label(self._params_frame, text=label, width=15, anchor="w").grid(
                row=row, column=0, padx=(4, 2), pady=2, sticky="w"
            )
            tk.Scale(
                self._params_frame, variable=var,
                from_=lo, to=hi, resolution=res,
                orient="horizontal", length=300,
            ).grid(row=row, column=1, padx=4, pady=2)

        self._btn_frame = tk.Frame(self)
        self._btn_frame.pack(fill="x", pady=6)
        tk.Label(self._btn_frame,
                 text=f"Crop size: {self._crop_w}×{self._crop_h} px  →  resized to {OUT_W}×{OUT_H}",
                 fg="gray").pack(side="left", padx=10)
        tk.Button(self._btn_frame, text="Confirm Crop", command=self._confirm,
                  bg="#4caf50", fg="white", padx=12).pack(side="right", padx=10)

        self._canvas.bind("<Button-1>", self._on_click)
        self._canvas.bind("<B1-Motion>", self._on_drag)
        self._drag_start: tuple[int, int] | None = None
        self._drag_box_start: tuple[int, int] | None = None

    # ------------------------------------------------------------------ #

    def _on_mode_change(self):
        if self._mode_var.get() == "contour":
            self._btn_frame.pack_forget()
            self._params_frame.pack(fill="x", padx=8, pady=(0, 4))
            self._btn_frame.pack(fill="x", pady=6)
        else:
            self._params_frame.pack_forget()

    def _clamp_box(self, bx: int, by: int) -> tuple[int, int]:
        return max(0, min(bx, self._max_bx)), max(0, min(by, self._max_by))

    def _redraw(self):
        x1, y1 = self._bx, self._by
        self._canvas.coords(self._rect, x1, y1, x1 + self._box_dw, y1 + self._box_dh)

    def _on_click(self, event: tk.Event):
        # Center the box on the click, or start a drag
        new_bx = event.x - self._box_dw // 2
        new_by = event.y - self._box_dh // 2
        self._bx, self._by = self._clamp_box(new_bx, new_by)
        self._redraw()
        self._drag_start = (event.x, event.y)
        self._drag_box_start = (self._bx, self._by)

    def _on_drag(self, event: tk.Event):
        if self._drag_start is None:
            return
        dx = event.x - self._drag_start[0]
        dy = event.y - self._drag_start[1]
        assert self._drag_box_start is not None
        self._bx, self._by = self._clamp_box(
            self._drag_box_start[0] + dx,
            self._drag_box_start[1] + dy,
        )
        self._redraw()

    def _confirm(self):
        ix = int(self._bx / self._scale)
        iy = int(self._by / self._scale)
        self._result = {
            "crop": (ix, iy, self._crop_w, self._crop_h),
            "mode": self._mode_var.get(),
            "params": {
                "pre_blur":       self._pre_blur_var.get(),
                "edge_threshold": self._edge_thresh_var.get(),
                "edge_thickness": self._edge_thick_var.get(),
                "noise_amount":   self._noise_var.get(),
            },
        }
        self.destroy()

    def get_result(self) -> dict | None:
        return self._result


class PreviewWindow(tk.Toplevel):
    """Displays the processed image and offers Save / Back."""

    def __init__(self, parent: tk.Tk, processed: Image.Image, mode: str = "grayscale"):
        super().__init__(parent)
        _mode_label = {"grayscale": "4-level grayscale", "contour": "contour / etching"}.get(mode, mode)
        self.title(f"Preview — {_mode_label} result")
        self.resizable(False, False)
        self.grab_set()

        self._action: str = "back"

        scale = _scale_to_fit(OUT_W, OUT_H, MAX_DISPLAY_W, MAX_DISPLAY_H)
        pw, ph = int(OUT_W * scale), int(OUT_H * scale)
        self._tk_img = ImageTk.PhotoImage(processed.resize((pw, ph), Image.NEAREST))

        tk.Label(self, image=self._tk_img).pack()

        btn_frame = tk.Frame(self)
        btn_frame.pack(fill="x", pady=6)
        tk.Button(btn_frame, text="Back", command=self._back, padx=12).pack(
            side="left", padx=10
        )
        tk.Button(btn_frame, text="Save", command=self._save,
                  bg="#2196f3", fg="white", padx=12).pack(side="right", padx=10)

    def _back(self):
        self._action = "back"
        self.destroy()

    def _save(self):
        self._action = "save"
        self.destroy()

    def get_action(self) -> str:
        return self._action


def save_file(parent: tk.Tk, img: Image.Image) -> bool:
    path = filedialog.asksaveasfilename(
        parent=parent,
        title="Save screensaver",
        initialdir=str(SCREENSAVERS_DIR),
        defaultextension=".png",
        filetypes=[("PNG", "*.png")],
    )
    if not path:
        return False
    img.save(path, format="PNG")
    messagebox.showinfo("Saved", f"Saved to:\n{path}", parent=parent)
    return True


def run():
    root = tk.Tk()
    root.withdraw()  # hide root window; use Toplevels only

    while True:
        # Step 1: pick file
        src_path = pick_file(root)
        if not src_path:
            break

        src_img = Image.open(src_path).convert("RGB")

        while True:
            # Step 2: crop
            crop_win = CropWindow(root, src_img)
            root.wait_window(crop_win)
            crop_result = crop_win.get_result()
            if crop_result is None:
                # Window closed without confirming → go back to file picker
                break

            # Step 3: process
            mode = crop_result["mode"]
            crop_coords = crop_result["crop"]
            if mode == "grayscale":
                processed = process_image(src_img, *crop_coords)
            else:
                processed = process_image_contour(src_img, *crop_coords, **crop_result["params"])

            # Step 4: preview
            preview = PreviewWindow(root, processed, mode=mode)
            root.wait_window(preview)
            action = preview.get_action()

            if action == "save":
                save_file(root, processed)
                # Ask whether to convert another
                another = messagebox.askyesno(
                    "Done", "Convert another image?", parent=root
                )
                if not another:
                    break
                # Start over from file picker
                break
            # action == "back" → loop back to crop

        else:
            continue
        break

    root.destroy()


if __name__ == "__main__":
    run()
