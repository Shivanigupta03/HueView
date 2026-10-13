import random
import numpy as np
from reportlab.pdfgen import canvas
from reportlab.lib.units import mm
from reportlab.lib.colors import HexColor


def hex_list(codes):
    return [HexColor(c) for c in codes]


class ColorblindTestPDF:
    def __init__(self, filename="colorblind_master_print_kit.pdf"):
        # Target trim size: 91.5mm x 60mm
        # Plus 3mm bleed on all 4 sides = 97.5mm x 66.0mm total canvas area
        self.bleed = 3.0 * mm
        self.trim_w = 91.5 * mm
        self.trim_h = 60.0 * mm

        self.page_w = self.trim_w + (2 * self.bleed)
        self.page_h = self.trim_h + (2 * self.bleed)

        # Initialize ReportLab canvas
        self.c = canvas.Canvas(filename, pagesize=(self.page_w, self.page_h))

        # Colour palettes (RGB; the print shop converts to CMYK).
        # Each list holds several shades. The shape and background use the SAME
        # range of lightness, so on the test plates the shape can only be found
        # by hue, not by being lighter or darker.

        # Control: dark shape on light background -> visible to everyone
        self.control_fg = hex_list(["#8B351C", "#93391E", "#9A3C20", "#A13F22"])
        self.control_bg = hex_list(["#939F99", "#97A49D", "#9BA8A1", "#9FADA6"])

        # Red-green: orange shape vs green background, chosen along the
        # red-green confusion line (look the same to protan/deutan vision)
        self.red_green_fg = hex_list(["#996F43", "#BC6E4A", "#C17B50", "#C78555", "#E0855A", "#E38E5E"])
        self.red_green_bg = hex_list(["#687E43", "#7B8749", "#63974E", "#779E54", "#8AA459", "#71B15C"])

        # Blue-yellow: grey-violet shape vs yellow-green background, chosen along
        # the blue-yellow confusion line (look the same to tritan vision)
        self.blue_yellow_fg = hex_list(["#7F7D79", "#8E8499", "#978F9C", "#A099A0", "#AB9FB6", "#B1A7B8"])
        self.blue_yellow_bg = hex_list(["#798246", "#848D57", "#8B9A34", "#95A34B", "#9EAA5F", "#A2B439"])

    def draw_crop_marks(self):
        """Draws professional printer alignment crop marks outside the trim zone."""
        self.c.setStrokeColor(HexColor("#A0A0A0"))
        self.c.setLineWidth(0.3)

        # X/Y bounds of the true trim card size
        x_left = self.bleed
        x_right = self.bleed + self.trim_w
        y_bottom = self.bleed
        y_top = self.bleed + self.trim_h

        # Top-Left Crop Marks
        self.c.line(x_left, y_top + self.bleed, x_left, y_top)
        self.c.line(x_left - self.bleed, y_top, x_left, y_top)

        # Top-Right Crop Marks
        self.c.line(x_right, y_top + self.bleed, x_right, y_top)
        self.c.line(x_right + self.bleed, y_top, x_right, y_top)

        # Bottom-Left Crop Marks
        self.c.line(x_left, y_bottom - self.bleed, x_left, y_bottom)
        self.c.line(x_left - self.bleed, y_bottom, x_left, y_bottom)

        # Bottom-Right Crop Marks
        self.c.line(x_right, y_bottom - self.bleed, x_right, y_bottom)
        self.c.line(x_right + self.bleed, y_bottom, x_right, y_bottom)

    @staticmethod
    def pack_dots(radius, r_max, r_min, gap, attempts=8000):
        """Random dots inside a circle that never overlap (largest dots placed first).
        Returns (x, y, r) relative to the plate centre."""
        cell = 2 * r_max + gap
        grid, dots = {}, []
        for r in np.linspace(r_max, r_min, 6):
            for _ in range(attempts):
                angle = random.uniform(0, 2 * np.pi)
                dist = (radius - r) * np.sqrt(random.uniform(0, 1))
                x, y = dist * np.cos(angle), dist * np.sin(angle)
                gx, gy = int(x // cell), int(y // cell)
                neighbours = (d for i in (gx - 1, gx, gx + 1) for j in (gy - 1, gy, gy + 1)
                                for d in grid.get((i, j), ()))
                if all((x - ox) ** 2 + (y - oy) ** 2 >= (r + orad + gap) ** 2
                        for ox, oy, orad in neighbours):
                    grid.setdefault((gx, gy), []).append((x, y, r))
                    dots.append((x, y, r))
        return dots

    @staticmethod
    def inside_shape(x, y, shape_type, s):
        """True if point (x, y), relative to the plate centre, is inside the shape of size s."""
        if shape_type == "circle":
            return x * x + y * y < s * s
        if shape_type == "square":
            return -s < x < s and -s < y < s
        if shape_type == "triangle":
            (x1, y1), (x2, y2), (x3, y3) = (0, s), (-s, -s), (s, -s)

            def sign(px, py, ax, ay, bx, by):
                return (px - bx) * (ay - by) - (ax - bx) * (py - by)

            b1 = sign(x, y, x1, y1, x2, y2) < 0.0
            b2 = sign(x, y, x2, y2, x3, y3) < 0.0
            b3 = sign(x, y, x3, y3, x1, y1) < 0.0
            return b1 == b2 == b3
        raise ValueError(f"Unknown shape: {shape_type}")

    def generate_matrix_plate(self, fg_colors, bg_colors, shape_type):
        """Draws a dot plate; dots inside the shape use fg_colors, the rest bg_colors."""
        center_x = self.page_w / 2.0
        center_y = self.page_h / 2.0

        matrix_radius = 26 * mm
        shape_radius = 12 * mm

        for x, y, dot_r in self.pack_dots(matrix_radius, r_max=1.5 * mm, r_min=0.5 * mm, gap=0.25 * mm):
            is_inside = self.inside_shape(x, y, shape_type, shape_radius)
            self.c.setFillColor(random.choice(fg_colors if is_inside else bg_colors))
            # Paint native PDF vectors directly to prevent resolution loss
            self.c.circle(center_x + x, center_y + y, dot_r, fill=1, stroke=0)

    def build_kit(self):
        # Page 1: Training Control Plate (Circle)
        self.generate_matrix_plate(self.control_fg, self.control_bg, "circle")
        self.draw_crop_marks()
        self.c.showPage()

        # Page 2: Red-Green Screen (Square)
        self.generate_matrix_plate(self.red_green_fg, self.red_green_bg, "square")
        self.draw_crop_marks()
        self.c.showPage()

        # Page 3: Blue-Yellow Screen (Triangle)
        self.generate_matrix_plate(self.blue_yellow_fg, self.blue_yellow_bg, "triangle")
        self.draw_crop_marks()
        self.c.showPage()

        # Save output compilation
        self.c.save()
        print("Success: Compiled production-ready multi-page master PDF with fixed shapes and bleed.")


if __name__ == "__main__":
    generator = ColorblindTestPDF()
    generator.build_kit()
