# Install first by typing this in your terminal (not inside the Python file):
# pip install pillow numpy
import random
import numpy as np
from PIL import Image, ImageDraw

SUPERSAMPLE = 3 # draw 3x bigger, then shrink, so dot edges are smooth on screen


def pack_dots(center, radius, r_max, r_min, gap, attempts=8000):
    """Random dots inside a circle that never overlap (largest dots placed first)."""
    cell = 2 * r_max + gap
    grid, dots = {}, []
    for r in np.linspace(r_max, r_min, 6):
        for _ in range(attempts):
            angle = random.uniform(0, 2 * np.pi)
            dist = (radius - r) * np.sqrt(random.uniform(0, 1))
            x, y = center + dist * np.cos(angle), center + dist * np.sin(angle)
            gx, gy = int(x // cell), int(y // cell)
            neighbours = (d for i in (gx - 1, gx, gx + 1) for j in (gy - 1, gy, gy + 1)
                            for d in grid.get((i, j), ()))
            if all((x - ox) ** 2 + (y - oy) ** 2 >= (r + orad + gap) ** 2
                    for ox, oy, orad in neighbours):
                grid.setdefault((gx, gy), []).append((x, y, r))
                dots.append((x, y, r))
    return dots


def generate_screen_plate(filename, fg_colors, bg_colors, shape_type="circle"):
    """
    Generates a 1080x1080 pixel image optimized for digital LCD/OLED screens.

    fg_colors: list of sRGB tuples for the hidden shape
    bg_colors: list of sRGB tuples for the background dots
    shape_type: "circle", "square", or "triangle"
    """
    canvas_size = 1080
    s = SUPERSAMPLE
    big = canvas_size * s
    # Use a neutral dark gray background instead of black to minimize screen glare
    image = Image.new("RGB", (big, big), (30, 30, 30))
    draw = ImageDraw.Draw(image)


    matrix_center = big // 2
    matrix_radius = 450 * s
    shape_radius = 180 * s

    # 1. First Pass: mask of the target shape (white = inside the shape)
    mask_img = Image.new("L", (big, big), 0)
    mask_draw = ImageDraw.Draw(mask_img)

    if shape_type == "circle":
        mask_draw.ellipse([matrix_center - shape_radius, matrix_center - shape_radius,
                            matrix_center + shape_radius, matrix_center + shape_radius], fill=255)
    elif shape_type == "square":
        mask_draw.rectangle([matrix_center - shape_radius, matrix_center - shape_radius,
                                matrix_center + shape_radius, matrix_center + shape_radius], fill=255)
    elif shape_type == "triangle":
        x1, y1 = matrix_center, matrix_center - shape_radius
        x2, y2 = matrix_center - shape_radius, matrix_center + shape_radius
        x3, y3 = matrix_center + shape_radius, matrix_center + shape_radius
        mask_draw.polygon([x1, y1, x2, y2, x3, y3], fill=255)
    mask_data = np.array(mask_img)

    # 2. Second Pass: non-overlapping dots; each dot takes the colour of its group
    for x, y, dot_r in pack_dots(matrix_center, matrix_radius, r_max=15 * s, r_min=5 * s, gap=3 * s):
        inside = mask_data[int(y), int(x)] > 127
        color = random.choice(fg_colors if inside else bg_colors)
        draw.ellipse([x - dot_r, y - dot_r, x + dot_r, y + dot_r], fill=color)

    image = image.resize((canvas_size, canvas_size), Image.LANCZOS)
    image.save(filename, "PNG")
    print(f"Generated digital screen asset: {filename}")


# sRGB colour sets. The shape and background in each test use the SAME range of
# brightness, so the shape can only be found by hue, not by being lighter/darker.

# Control: dark red shape on light grey -> visible to everyone
srgb_control_fg = [(139, 53, 28), (147, 57, 30), (154, 60, 32), (161, 63, 34)]
srgb_control_bg = [(147, 159, 153), (151, 164, 157), (155, 168, 161), (159, 173, 166)]

# Red-green: orange vs green, on the red-green confusion line
srgb_red_green_fg = [(153, 111, 67), (188, 110, 74), (193, 123, 80),
                    (199, 133, 85), (224, 133, 90), (227, 142, 94)]
srgb_red_green_bg = [(104, 126, 67), (123, 135, 73), (99, 151, 78),
                    (119, 158, 84), (138, 164, 89), (113, 177, 92)]
# Blue-yellow: grey-violet vs yellow-green, on the blue-yellow confusion line
srgb_blue_yellow_fg = [(127, 125, 121), (142, 132, 153), (151, 143, 156),
                    (160, 153, 160), (171, 159, 182), (177, 167, 184)]
srgb_blue_yellow_bg = [(121, 130, 70), (132, 141, 87), (139, 154, 52),
                    (149, 163, 75), (158, 170, 95), (162, 180, 57)]

if __name__ == "__main__":
    generate_screen_plate("screen_plate1_control.png", srgb_control_fg, srgb_control_bg, "circle")
    generate_screen_plate("screen_plate2_redgreen.png", srgb_red_green_fg,
srgb_red_green_bg, "square")
    generate_screen_plate("screen_plate3_blueyellow.png", srgb_blue_yellow_fg,
srgb_blue_yellow_bg, "triangle")
