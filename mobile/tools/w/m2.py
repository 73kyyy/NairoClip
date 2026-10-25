from PIL import Image
sh = Image.new("RGB", (700, 560), (255,255,255))
for r in range(4):
    for c in range(5):
        im = Image.open("g_%d_%d.png" % (r, c)).resize((136, 136), Image.LANCZOS)
        sh.paste(im, (c*140, r*140))
sh.save("gsheet.png")
print("ok")
