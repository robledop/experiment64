from PIL import Image, ImageDraw, ImageFont
import pathlib
text = "experiment 64"
font_path = "/usr/share/fonts/truetype/freefont/FreeMonoBold.ttf"
font_size = 32
font = ImageFont.truetype(font_path, font_size)
# measure text
bbox = font.getbbox(text)
text_width = bbox[2] - bbox[0]
text_height = bbox[3] - bbox[1]
padding = 10
width = text_width + padding * 2
height = text_height + padding * 2
img = Image.new("RGB", (width, height), (0, 0, 0))
draw = ImageDraw.Draw(img)
text_position = (padding - bbox[0], padding - bbox[1])
draw.text(text_position, text, font=font, fill=(0, 255, 102))
output_path = pathlib.Path("assets/logo.bmp")
img.save(output_path, format="BMP")
print(f"Saved {output_path} ({width}x{height})")